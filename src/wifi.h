#ifndef WIFI_H
#define WIFI_H

#include <esp_wifi.h>
#include <stdint.h>

void wifi_init(void);
void wifi_set_mode(wifi_mode_t);
void wifi_set_ssid_pass(uint8_t *ssid, uint8_t *pass); // null-terminated

#endif
