#ifndef WIFI_H
#define WIFI_H

#include <esp_wifi.h>
#include <stdint.h>

void wifi_init(void);
void wifi_reconnect(void);
size_t wifi_command(uint8_t *data, size_t len);

#endif
