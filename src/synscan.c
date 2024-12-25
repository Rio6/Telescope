#include "synscan.h"
#include <driver/uart.h>
#include <freertos/queue.h>

typedef enum {
   SS_IF_UART = 0,
   SS_IF_BLE,
   SS_IF_UDP,
   SS_IF_COUNT,
} ss_interface_E;

typedef enum {
   SS_ERR_UNKNOWN_COMMAND = 0,
   SS_ERR_LENGTH,
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
   uint8_t len;
   uint8_t header;
   uint8_t data[8];

   ss_parser_status_E status;
} ss_parser_S;

static ss_parser_S parsers[SS_IF_COUNT] = {0};

static QueueHandle_t uart_queue;

static void ss_parse(ss_parser_S *parser, uint8_t byte);
static size_t ss_response(ss_parser_S *parser, uint8_t *resp);
static uint8_t from_hex(uint8_t hex);
static uint8_t to_hex(uint8_t num);

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
      ss_error_E error = SS_OK;

      if(parser->status != SS_PARSED) continue;

      // handle command
      // https://inter-static.skywatcher.com/downloads/skywatcher_motor_controller_command_set.pdf
      switch(parser->data[0]) {
         case 'E': // set position
         case 'F': // init done
         case 'G': // set motion mode
         case 'S': // set goto target
         case 'I': // set step period (T1)
         case 'J': // start motion
         case 'K': // stop motion
         case 'L': // instant stop
         case 'O': // aux switch
         case 'P': // AutoGuide speed
         case 'a': // inquire counts per revolution
         case 'b': // inquire timer frequency
         case 'h': // inquire goto target
         case 'i': // inquire step period
         case 'j': // inquire position
         case 'f': // inquire status
         case 'g': // inquire high speed ratio
         case 'D': // inquire axis position
         case 'e': // inquire motor board version
         default:
            error = SS_ERR_UNKNOWN_COMMAND;
            break;
      }

      parser->status = SS_HANDLED;
   }

   // send serial
   if(uart_parser->status == SS_HANDLED) {
      uint8_t resp[8] = {0};
      uint8_t len = ss_response(uart_parser, resp);
      uart_write_bytes(UART_NUM_0, resp, len);
      uart_parser->status = SS_PARSING;
   }

   // send ble
   // send udp
}

static void ss_parse(ss_parser_S *parser, uint8_t byte) {
   if(parser->status != SS_PARSING) {
      return;
   }

   if(parser->len >= sizeof(parser->data)) {
      // shouldn't happen, overflow buffer
      parser->len = 0;
   }

   if(byte == ':') {
      parser->len = 0;
   } else if(byte == '\r') {
      parser->status = SS_PARSED;
   } else {
      parser->data[parser->len++] = byte;
   }
}

static size_t ss_response(ss_parser_S *parser, uint8_t *resp) {
   if(parser->error != SS_OK) {
      resp[0] = '!';
      resp[1] = '0' + parser->error;
      resp[2] = '\r';
      return 3;
   }

   uint8_t len = 0;
   resp[len++] = '=';
   for(int i = 0; i < parser->len; i++) {
      //resp[len++] = to_hex(parser->data[i]);
   }
   resp[len++] = '\r';
   return len;
}

static uint8_t from_hex(uint8_t hex) {
   if(hex >= '0' && hex <= '9') {
      return hex - '0';
   }
   if(hex >= 'a' && hex <= 'f') {
      return hex - 'a' + 10;
   }
   if(hex >= 'A' && hex <= 'F') {
      return hex - 'A' + 10;
   }
   return 0;
}

static uint8_t to_hex(uint8_t num) {
   if(hex >= '0' && hex <= '9') {
      return hex - '0';
   }
   if(hex >= 'a' && hex <= 'f') {
      return hex - 'a' + 10;
   }
   if(hex >= 'A' && hex <= 'F') {
      return hex - 'A' + 10;
   }
   return 0;
}
