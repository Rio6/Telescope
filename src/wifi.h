#ifndef WIFI_H
#define WIFI_H

#include <esp_wifi.h>
#include <stdint.h>

typedef struct {
    char ap_ssid[32];
    char ap_pass[64];
    char sta_ssid[32];
    char sta_pass[64];
    bool enable_ap;
    bool enable_sta;
} wifi_config_S;

void wifi_init(void);
void wifi_reconnect(void);
void wifi_get_config(wifi_config_S*);
void wifi_set_config(wifi_config_S*);

#endif
