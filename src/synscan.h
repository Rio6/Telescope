#ifndef SYNSCAN_H
#define SYNSCAN_H

#include <stdint.h>
#include <stddef.h>

typedef enum {
   SS_PARSING,
   SS_PARSED,
} ss_parser_status_E;

typedef struct {
   ss_parser_status_E status;
   uint8_t plen;
   uint8_t channel;
   union {
      struct {
         uint8_t header;
         uint8_t payload[64];
      };
      uint8_t data[65]; // larger than ssid and pass in wifi_config_S
   };
} ss_parser_S;

size_t ss_handle_byte(ss_parser_S*, uint8_t);

#endif
