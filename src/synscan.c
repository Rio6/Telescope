// implements https://inter-static.skywatcher.com/downloads/skywatcher_motor_controller_command_set.pdf
#include "synscan.h"
#include "stepper.h"
#include "wifi.h"

#include <esp_log.h>
#include <freertos/queue.h>
#include <stdint.h>
#include <string.h>

typedef enum {
   SS_ERR_UNKNOWN_COMMAND = 0,
   SS_ERR_COMMAND_LENGTH,
   SS_ERR_NOT_STOPPED,
   SS_ERR_INVAID_CHAR,
   SS_ERR_NOT_INIT,
   SS_ERR_SLEEPING,
   SS_OK,
} ss_error_E;

static void ss_parse(ss_parser_S *parser, uint8_t byte);
static uint32_t ss_get_payload(ss_parser_S *parser);
static void ss_construct_resp(ss_parser_S *parser, ss_error_E error, uint32_t payload, size_t plen);
static stepper_E ss_get_stepper(ss_parser_S *parser, bool start);
static uint8_t hexify(uint8_t num);
static uint8_t unhexify(uint8_t hex);

size_t ss_handle_byte(ss_parser_S *parser, uint8_t byte) {
   if(parser->status == SS_PARSING) {
      ss_parse(parser, byte);
   }

   if(parser->status != SS_PARSED) return 0;

   // handle command
   #define SS_CHECK(MAX_C, L) \
   if(parser->channel > (MAX_C) || parser->plen != (L)) { \
      ss_construct_resp(parser, SS_ERR_COMMAND_LENGTH, 0, 0); \
      break; \
   };

   switch(parser->header) {
      case '+': { // AT command
         // reset
         if(memcmp(parser->payload, "RST", 3) == 0) {
            esp_restart();
         }

         size_t resp_len = 0;

         if(memcmp(parser->payload, "LOG=", 4) == 0) {
            esp_log_level_t log_level = *(parser->payload+4) - '0';
            if(log_level > ESP_LOG_VERBOSE) {
               log_level = ESP_LOG_VERBOSE;
            }
            esp_log_level_set("*", log_level);
         } else {
            resp_len = wifi_command(parser->data, parser->plen+1);
         }

         if(resp_len) {
            parser->data[resp_len] = '\r';
            parser->plen = resp_len;
         } else { // not handled by wifi_command, ignore because SynScan also sends them
            memcpy(parser->data, "OK\r", 3);
            parser->plen = 2;
         }
         break;
      }

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

      case 'E': { // set position
         SS_CHECK(2, 6);
         ss_error_E error = SS_OK;
         uint32_t count = ss_get_payload(parser);
         for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
            if(stepper_busy(stepper)) {
               error = SS_ERR_NOT_STOPPED;
               break;
            }
            stepper_set_count(stepper, count);
         }
         ss_construct_resp(parser, error, 0, 0);
         break;
      }

      case 'G': { // set motion mode
         SS_CHECK(3, 2);
         ss_error_E error = SS_OK;
         uint32_t payload = ss_get_payload(parser);
         stepper_mode_E mode = payload & 1;
         stepper_dir_E dir = (payload >> 4) & 1;
         for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
            if(stepper_busy(stepper)) {
               error = SS_ERR_NOT_STOPPED;
               break;
            }
            stepper_set_mode_dir(stepper, mode, dir);
         }
         ss_construct_resp(parser, error, 0, 0);
         break;
      }

      case 'S': { // set goto target
         SS_CHECK(2, 6);
         ss_error_E error = SS_OK;
         uint32_t target = ss_get_payload(parser);
         for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
            if(stepper_busy(stepper)) {
               error = SS_ERR_NOT_STOPPED;
               break;
            }
            stepper_set_target(stepper, target);
         }
         ss_construct_resp(parser, error, 0, 0);
         break;
      }

      case 'H': { // set goto target increment
         SS_CHECK(3, 6);
         ss_error_E error = SS_OK;
         uint32_t increment = ss_get_payload(parser);
         for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
            if(stepper_busy(stepper)) {
               error = SS_ERR_NOT_STOPPED;
               break;
            }
            uint32_t count = stepper_get_count(stepper);
            stepper_set_target(stepper, count + increment);
         }
         ss_construct_resp(parser, error, 0, 0);
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

      case 'K': // stop motion, applies brake steps (once implemented)
      case 'L': // instant stop
         SS_CHECK(3, 0);
         for(stepper_E stepper = ss_get_stepper(parser, true); stepper != ss_get_stepper(parser, false); stepper++) {
            stepper_stop(stepper);
         }
         ss_construct_resp(parser, SS_OK, 0, 0);
         break;

      case 'h': { // inquire goto target
         SS_CHECK(2, 0);
         uint32_t target = stepper_get_target(ss_get_stepper(parser, true));
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
         uint32_t target = stepper_get_count(ss_get_stepper(parser, true));
         ss_construct_resp(parser, SS_OK, target, 6);
         break;
      }

      // not implemented
      case 'g': // inquire high speed ratio
         SS_CHECK(2, 0);
         ss_construct_resp(parser, SS_OK, 1, 2);
         break;

      case 'M': // set brake point increment
         SS_CHECK(3, 6);
         ss_construct_resp(parser, SS_OK, 0, 0);
         break;

      case 'O': // aux switch
         SS_CHECK(3, 0);
         ss_construct_resp(parser, SS_OK, 0, 0);
         break;

      default:
         ss_construct_resp(parser, SS_ERR_UNKNOWN_COMMAND, 0, 0);
         break;
   }
   #undef SS_CHECK

   parser->status = SS_PARSING;
   parser->channel = 0;

   return parser->plen + 1;
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
   for(int i = parser->plen-1; i >= 0; i--) {
      num = (num << 4) | unhexify(parser->payload[i]);
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
   if(start) {
      if(parser->channel == 2)
         return STEPPER_DE;
      return STEPPER_RA;
   } else {
      if(parser->channel == 1)
         return STEPPER_DE;
      return STEPPER_COUNT;
   }
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
