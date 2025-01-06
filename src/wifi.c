#include "wifi.h"
#include <esp_wifi.h>
#include <esp_smartconfig.h>
#include <esp_log.h>
#include <string.h>

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
    ESP_ERROR_CHECK(esp_netif_init());

    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT,   ESP_EVENT_ANY_ID, &event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "ssid",
            .password = "password",
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void wifi_set_mode(wifi_mode_t mode) {
    if(mode != WIFI_MODE_STA && mode != WIFI_MODE_AP) {
        ESP_LOGE("wifi", "invalid mode %d", mode);
        return;
    }

    wifi_interface_t intf = mode == WIFI_MODE_STA ? WIFI_IF_STA : WIFI_IF_AP;
    wifi_mode_t old_mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&old_mode);
    if(old_mode != mode) {
        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_mode(mode));

        // we only use ssid and password field of config, which is common between ap and sta
        wifi_config_t wifi_config;
        esp_wifi_get_config(intf, &wifi_config);

        ESP_ERROR_CHECK(esp_wifi_set_config(intf, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}

void wifi_set_ssid_pass(uint8_t *ssid, uint8_t *pass) {
    wifi_mode_t mode = WIFI_MODE_NULL;
    esp_wifi_get_mode(&mode);

    if(mode != WIFI_MODE_STA && mode != WIFI_MODE_AP) {
        ESP_LOGE("wifi", "invalid mode when setting ssid pass %d", mode);
        return;
    }

    wifi_interface_t intf = mode == WIFI_MODE_STA ? WIFI_IF_STA : WIFI_IF_AP;
    wifi_config_t wifi_config = {0};
    esp_wifi_get_config(intf, &wifi_config);

    // ssid and password are common between ap and sta
    if(
        strcmp((char*) wifi_config.ap.ssid,     (char*) ssid) != 0 ||
        strcmp((char*) wifi_config.ap.password, (char*) pass) != 0
    ) {

        memcpy(&wifi_config.ap.ssid,     ssid, sizeof(wifi_config.ap.ssid));
        memcpy(&wifi_config.ap.password, pass, sizeof(wifi_config.ap.password));

        ESP_ERROR_CHECK(esp_wifi_stop());
        ESP_ERROR_CHECK(esp_wifi_set_config(intf, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_start());
    }
}
