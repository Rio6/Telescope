#include "wifi.h"

#include <esp_wifi.h>
#include <esp_smartconfig.h>
#include <esp_log.h>
#include <nvs_flash.h>

#include <string.h>

static nvs_handle_t nvs;
static wifi_config_S current_config = {0};

static void event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    if(event_base == WIFI_EVENT) {
        switch(event_id) {
            case WIFI_EVENT_STA_START:
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
            case WIFI_EVENT_STA_CONNECTED:
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                ESP_ERROR_CHECK(esp_wifi_connect());
                break;
        }
    } else if(event_base == IP_EVENT) {
        switch(event_id) {
            case IP_EVENT_STA_GOT_IP:
                ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
                ESP_LOGI("wifi", "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
                break;
        }
    }
}

void wifi_init(void) {
    ESP_ERROR_CHECK(nvs_open("wifi", NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK(esp_netif_init());

    assert(esp_netif_create_default_wifi_sta());
    assert(esp_netif_create_default_wifi_ap());

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_init_config.nvs_enable = false;
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   ESP_EVENT_ANY_ID, &event_handler, NULL));

    size_t config_len = sizeof(current_config);
    esp_err_t err = nvs_get_blob(nvs, "config", &current_config, &config_len);
    if(err == ESP_ERR_NVS_NOT_FOUND) {
        memset(&current_config, 0, sizeof(current_config));
        memcpy(current_config.ap_ssid, "ESPScope", sizeof(current_config.ap_ssid));
        memcpy(current_config.ap_pass, "88888888", sizeof(current_config.ap_pass));
        current_config.enable_ap = true;
    } else {
        ESP_ERROR_CHECK(err);
    }

    wifi_reconnect();
}

// reconnect with config
void wifi_reconnect(void) {
    esp_wifi_stop();

    if(current_config.enable_ap && current_config.enable_sta) {
        esp_wifi_set_mode(WIFI_MODE_APSTA);
    } else if(current_config.enable_ap) {
        esp_wifi_set_mode(WIFI_MODE_AP);
    } else if(current_config.enable_sta) {
        esp_wifi_set_mode(WIFI_MODE_STA);
    }

    if(current_config.enable_ap) {
        wifi_config_t wifi_config = {0};
        memcpy(wifi_config.ap.ssid, current_config.ap_ssid, sizeof(wifi_config.ap.ssid));
        memcpy(wifi_config.ap.password, current_config.ap_pass, sizeof(wifi_config.ap.password));
        esp_wifi_set_config(WIFI_IF_AP, &wifi_config);
    }

    if(current_config.enable_sta) {
        wifi_config_t wifi_config = {0};
        memcpy(wifi_config.sta.ssid, current_config.sta_ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, current_config.sta_pass, sizeof(wifi_config.sta.password));
        esp_wifi_set_config(WIFI_IF_STA, &wifi_config);
    }

    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_get_config(wifi_config_S *config) {
    memcpy(config, &current_config, sizeof(*config));
}

void wifi_set_config(wifi_config_S *config) {
    memcpy(&current_config, config, sizeof(current_config));
}
