#include "synscan.h"
#include "stepper.h"

#include <driver/uart.h>
#include <freertos/queue.h>
#include <string.h>

typedef enum {
   SS_IF_UART = 0,
   SS_IF_BLE,
   SS_IF_UDP,
   SS_IF_COUNT,
} ss_interface_E;

typedef enum {
   SS_ERR_UNKNOWN_COMMAND = 0,
   SS_ERR_COMMAND_LENGTH,
   SS_ERR_NOT_STOPPED,
   SS_ERR_INVAID_CHAR,
   SS_ERR_NOT_INIT,
   SS_ERR_SLEEPING,
   SS_OK,
} ss_error_E;

typedef enum {
   SS_PARSING,
   SS_PARSED,
   SS_HANDLED,
} ss_parser_status_E;

typedef struct {
   ss_parser_status_E status;
   uint8_t plen;
   uint8_t channel;
   union {
      struct {
         uint8_t header;
         uint8_t payload[7];
      };
      uint8_t data[8];
   };
} ss_parser_S;

static ss_parser_S parsers[SS_IF_COUNT] = {0};

static QueueHandle_t uart_queue;

static void ss_parse(ss_parser_S *parser, uint8_t byte);
static uint32_t ss_get_payload(ss_parser_S *parser);
static void ss_construct_resp(ss_parser_S *parser, ss_error_E error, uint32_t payload, size_t plen);
static stepper_E ss_get_stepper(ss_parser_S *parser, bool start);
static uint8_t hexify(uint8_t num);
static uint8_t unhexify(uint8_t hex);

void ss_init(void) {
   // uart setup
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   uart_param_config(UART_NUM_0, &uart_config);
   uart_driver_install(UART_NUM_0, 1024, 1024, 10, &uart_queue, 0);

   // ble setup
   // udp setup
}

void ss_task(void) {
   // read serial
   ss_parser_S *uart_parser = &parsers[SS_IF_UART];
   size_t uart_len = 0;
   uart_get_buffered_data_len(UART_NUM_0, &uart_len);
   for(size_t i = 0; i < uart_len && uart_parser->status == SS_PARSING; i++) {
      uint8_t byte = 0;
      uart_read_bytes(UART_NUM_0, &byte, 1, portMAX_DELAY);
      ss_parse(uart_parser, byte);
   }

   // read ble
   // read udp

   for(ss_interface_E i = 0; i < SS_IF_COUNT; i++) {
      ss_parser_S *parser = &parsers[i];
      if(parser->status != SS_PARSED) continue;

      // handle command
      // https://inter-static.skywatcher.com/downloads/skywatcher_motor_controller_command_set.pdf

      #define SS_CHECK(MAX_C, L) \
      if(parser->channel > (MAX_C) || parser->plen != (L)) { \
         ss_construct_resp(parser, SS_ERR_COMMAND_LENGTH, 0, 0); \
         break; \
      };

      switch(parser->header) {
         case '+': // AT command, ignore
            memcpy(parser->data, "OK\r", 3);
            parser->plen = 2;
            break;

         case 'F': // initialization done
            SS_CHECK(3, 0);
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;

         case 'e': // inquire motor board version
            SS_CHECK(3, 0);
            ss_construct_resp(parser, SS_OK, 0x000030, 6); // 3.0
            break;

         case 'a': { // inquire counts per revolution
            SS_CHECK(2, 0);
            uint32_t cpr = stepper_cpr(ss_get_stepper(parser, true));
            ss_construct_resp(parser, SS_OK, cpr, 6);
            break;
         }

         case 'f': { // inquire status
            SS_CHECK(2, 0);
            stepper_E stepper   = ss_get_stepper(parser, true);
            stepper_mode_E mode = stepper_get_mode(stepper);
            stepper_dir_E dir   = stepper_get_dir(stepper);
            bool busy           = stepper_busy(stepper);

            uint16_t status = (mode << 0)
                            | (dir  << 1)
                            | (busy << 4)
                            | (1    << 6); // init done

            ss_construct_resp(parser, SS_OK, status, 4);
            break;
         }

         case 'b': // inquire timer frequency
            SS_CHECK(1, 0);
            ss_construct_resp(parser, SS_OK, STEPPER_FREQ, 4);
            break;

         case 'E': // set position
            SS_CHECK(3, 6);
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;

         case 'G': { // set motion mode
            SS_CHECK(3, 2);
            uint32_t payload = ss_get_payload(parser);
            stepper_mode_E mode = payload & 1;
            stepper_dir_E dir = (payload >> 4) & 1;
            for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
               stepper_set_mode_dir(stepper, mode, dir);
            }
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;
         }

         case 'S': { // set goto target
            SS_CHECK(3, 6);
            uint32_t target = ss_get_payload(parser);
            for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
               stepper_set_goal(stepper, target);
            }
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;
         }

         case 'I': { // set step period (T1)
            SS_CHECK(3, 6);
            uint32_t period = ss_get_payload(parser);
            for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
               stepper_set_period(stepper, period);
            }
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;
         }

         case 'J': // start motion
            SS_CHECK(3, 0);
            for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
               stepper_start(stepper);
            }
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;

         case 'K': // stop motion
         case 'L': // instant stop
            SS_CHECK(3, 0);
            for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
               stepper_stop(stepper);
            }
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;

         case 'h': { // inquire goto target
            SS_CHECK(2, 0);
            uint32_t target = stepper_get_goal(ss_get_stepper(parser, true));
            ss_construct_resp(parser, SS_OK, target, 6);
            break;
         }

         case 'i': { // inquire step period
            SS_CHECK(2, 0);
            uint32_t target = stepper_get_period(ss_get_stepper(parser, true));
            ss_construct_resp(parser, SS_OK, target, 6);
            break;
         }

         case 'j':    // inquire position
         case 'D':  { // inquire axis position, not sure what the difference is
            SS_CHECK(2, 0);
            uint32_t target = stepper_count(ss_get_stepper(parser, true));
            ss_construct_resp(parser, SS_OK, target, 6);
            break;
         }

         case 'g': // inquire high speed ratio
            SS_CHECK(2, 0);
            ss_construct_resp(parser, SS_OK, 1, 2);
            break;

         case 'O': // aux switch
            SS_CHECK(3, 0);
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;

         case 'P': // AutoGuide speed
            SS_CHECK(3, 0);
            ss_construct_resp(parser, SS_OK, 0, 0);
            break;

         default:
            ss_construct_resp(parser, SS_ERR_UNKNOWN_COMMAND, 0, 0);
            break;
      }
      #undef SS_CHECK_LEN

      parser->status = SS_HANDLED;
   }

   // send serial
   if(uart_parser->status == SS_HANDLED) {
      uart_write_bytes(UART_NUM_0, uart_parser->data, uart_parser->plen+1);
      uart_parser->status = SS_PARSING;
   }

   // send ble
   // send udp
}

static void ss_parse(ss_parser_S *parser, uint8_t byte) {
   if(parser->status != SS_PARSING) {
      return;
   }

   if(parser->plen >= sizeof(parser->payload)) {
      parser->plen = 0;
   }

   if((byte == '_' || byte == '\r') && (parser->header != 0 && parser->channel != 0)) {
      parser->status = SS_PARSED;

   } else if(byte == ':') { // start of command
      parser->plen = 0;
      parser->header = 0;
      parser->channel = 0;

   } else if(byte == '+') { // AT commands
      parser->plen = 0;
      parser->header = '+';
      parser->channel = 3; // arbitrary

   } else if(parser->header == 0) { // header
      parser->header = byte;

   } else if(parser->channel == 0) { // channel
      parser->channel = unhexify(byte);

   } else { // data
      parser->payload[parser->plen++] = byte;
   }
}

static uint32_t ss_get_payload(ss_parser_S *parser) {
   uint32_t num = 0;
   for(int i = 0; i < parser->plen; i++) {
      num = (num << 4) | unhexify(parser->data[i]);
   }
   return num;
}

static void ss_construct_resp(ss_parser_S *parser, ss_error_E error, uint32_t payload, size_t plen) {
   if(error != SS_OK) {
      parser->header = '!';
      parser->payload[0] = '0' + error;
      parser->payload[1] = '\r';
      parser->plen = 2;
   } else {
      if(plen > 6) plen = 6;
      parser->header = '=';
      parser->plen = 0;
      while(parser->plen < plen) {
         parser->payload[parser->plen++] = hexify(payload & 0xF);
         payload >>= 4;
      }
      parser->payload[parser->plen++] = '\r';
   }
}

static stepper_E ss_get_stepper(ss_parser_S *parser, bool start) {
   if(parser->channel == 1)
     return STEPPER_RA;

   if(parser->channel == 2)
      return STEPPER_DE;

   if(start)
      return STEPPER_RA;

   return STEPPER_DE;
}

static uint8_t hexify(uint8_t num) {
   num = num % 16;
   if(num < 10) {
      return num + '0';
   }
   return num - 10 + 'A';
}

static uint8_t unhexify(uint8_t hex) {
   if('0' <= hex && hex <= '9') {
      return hex - '0';
   }
   if('a' <= hex && hex <= 'f') {
      return hex - 'a' + 10;
   }
   if('A' <= hex && hex <= 'F') {
      return hex - 'A' + 10;
   }
   return 0;
}
