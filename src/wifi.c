#include "wifi.h"

#include <esp_wifi.h>
#include <esp_smartconfig.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <string.h>

typedef struct {
    char ap_ssid[32];
    char ap_pass[64];
    char sta_ssid[32];
    char sta_pass[64];
    bool enable_ap;
    bool enable_sta;
} wifi_config_S;

static nvs_handle_t nvs;
static wifi_config_S wifi_config = {0};
static esp_netif_t *sta_netif, *ap_netif;

void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_open("wifi", NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(esp_netif_init());

    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_init_config.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    size_t config_len = sizeof(wifi_config);
    esp_err_t err = nvs_get_blob(nvs, "config", &wifi_config, &config_len);
    if(err == ESP_ERR_NVS_NOT_FOUND) {
        memset(&wifi_config, 0, sizeof(wifi_config));
        strncpy(wifi_config.ap_ssid, "ESPScope", sizeof(wifi_config.ap_ssid));
        strncpy(wifi_config.ap_pass, "88888888", sizeof(wifi_config.ap_pass));
        wifi_config.enable_ap = true;
    } else {
        ESP_ERROR_CHECK(err);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
    wifi_reconnect();
}

// reconnect with config
void wifi_reconnect(void) {
    esp_wifi_disconnect();

    if(wifi_config.enable_ap && wifi_config.enable_sta) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    } else if(wifi_config.enable_ap) {
        esp_wifi_set_mode(WIFI_MODE_AP);
    } else if(wifi_config.enable_sta) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    } else {
        esp_wifi_set_mode(WIFI_MODE_NULL);
    }

    if(wifi_config.enable_ap) {
        wifi_config_t esp_wifi_config = {
            .ap = {
                .authmode = strnlen(wifi_config.ap_ssid, sizeof(wifi_config.ap_ssid)) >= 8
                    ? WIFI_AUTH_WPA2_PSK
                    : WIFI_AUTH_OPEN,
                .max_connection = 4,
            }
        };
        memcpy(esp_wifi_config.ap.ssid, wifi_config.ap_ssid, sizeof(esp_wifi_config.ap.ssid));
        memcpy(esp_wifi_config.ap.password, wifi_config.ap_pass, sizeof(esp_wifi_config.ap.password));
        esp_wifi_set_config(WIFI_IF_AP, &esp_wifi_config);
    }

    if(wifi_config.enable_sta) {
        wifi_config_t esp_wifi_config = {
            .sta = {
                .failure_retry_cnt = 20,
            }
        };
        memcpy(esp_wifi_config.sta.ssid, wifi_config.sta_ssid, sizeof(esp_wifi_config.sta.ssid));
        memcpy(esp_wifi_config.sta.password, wifi_config.sta_pass, sizeof(esp_wifi_config.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &esp_wifi_config);
    }

    ESP_ERROR_CHECK(esp_wifi_connect());
}

static size_t min(size_t a, size_t b) {
   return a < b ? a : b;
}

size_t wifi_command(uint8_t *data, size_t len) {
    if(memcmp(data, "+APSSID?", 8) == 0) {
        size_t resp_len = strnlen(wifi_config.ap_ssid, sizeof(wifi_config.ap_ssid));
        memcpy(data, wifi_config.ap_ssid, resp_len);
        return resp_len;
    }

    else if(memcmp(data, "+APPASS?", 8) == 0) {
        size_t resp_len = strnlen(wifi_config.ap_pass, sizeof(wifi_config.ap_pass));
        memcpy(data, wifi_config.ap_pass, resp_len);
        return resp_len;
    }

    else if(memcmp(data, "+STASSID?", 9) == 0) {
        size_t resp_len = strnlen(wifi_config.sta_ssid, sizeof(wifi_config.sta_ssid));
        memcpy(data, wifi_config.sta_ssid, resp_len);
        return resp_len;
    }

    else if(memcmp(data, "+STAPASS?", 9) == 0) {
        size_t resp_len = strnlen(wifi_config.sta_pass, sizeof(wifi_config.sta_pass));
        memcpy(data, wifi_config.sta_pass, resp_len);
        return resp_len;
    }

    else if(memcmp(data, "+APIP?", 6) == 0) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(ap_netif, &ip_info);
        return sprintf((char*) data, IPSTR, IP2STR(&ip_info.ip));
    }

    else if(memcmp(data, "+STAIP?", 7) == 0) {
        esp_netif_ip_info_t ip_info;
        esp_netif_get_ip_info(sta_netif, &ip_info);
        return sprintf((char*) data, IPSTR, IP2STR(&ip_info.ip));
    }

    else if(memcmp(data, "+APSSID=", 8) == 0) {
        size_t mlen = min(len-8, sizeof(wifi_config.ap_ssid));
        if(mlen == 0) {
            wifi_config.enable_ap = false;
            memset(&wifi_config.ap_ssid, 0, sizeof(wifi_config.ap_ssid));
        } else {
            wifi_config.enable_ap = true;
            memcpy(&wifi_config.ap_ssid, data+8, mlen);
            memset(&wifi_config.ap_ssid[0] + mlen, 0, sizeof(wifi_config.ap_ssid) - mlen);
        }
        memcpy(data, "OK", 2);
        return 2;
    }

    else if(memcmp(data, "+APPASS=", 8) == 0) {
        size_t mlen = min(len-8, sizeof(wifi_config.ap_pass));
        memcpy(&wifi_config.ap_pass, data+8, mlen);
        memset(&wifi_config.ap_pass[0] + mlen, 0, sizeof(wifi_config.ap_pass) - mlen);
        memcpy(data, "OK", 2);
        return 2;
    }

    else if(memcmp(data, "+STASSID=", 9) == 0) {
        size_t mlen = min(len-9, sizeof(wifi_config.sta_ssid));
        if(mlen == 0) {
            wifi_config.enable_sta = false;
            memset(&wifi_config.sta_ssid, 0, sizeof(wifi_config.sta_ssid));
        } else {
            wifi_config.enable_sta = true;
            memcpy(&wifi_config.sta_ssid, data+9, mlen);
            memset(&wifi_config.sta_ssid[0] + mlen, 0, sizeof(wifi_config.sta_ssid) - mlen);
        }
        memcpy(data, "OK", 2);
        return 2;
    }

    else if(memcmp(data, "+STAPASS=", 9) == 0) {
        size_t mlen = min(len-9, sizeof(wifi_config.sta_pass));
        memcpy(&wifi_config.sta_pass, data+9, mlen);
        memset(&wifi_config.sta_pass[0] + mlen, 0, sizeof(wifi_config.sta_pass) - mlen);
        memcpy(data, "OK", 2);
        return 2;
    }

    else if(memcmp(data, "+WIFICONN", 9) == 0) {
        wifi_reconnect();
        memcpy(data, "OK", 2);
        return 2;
    }

    else if(memcmp(data, "+WIFISAVE", 9) == 0) {
        esp_err_t err = nvs_set_blob(nvs, "config", &wifi_config, sizeof(wifi_config));
        if(err == ESP_OK) {
            memcpy(data, "OK", 2);
            return 2;
        } else {
            memcpy(data, "FAIL", 4);
            return 4;
        }
    }

    return 0;
}
