#include "wifi.h"

#include <esp_wifi.h>
#include <esp_mac.h>
#include <esp_smartconfig.h>
#include <nvs_flash.h>

#include <string.h>

typedef struct {
    wifi_ap_config_t ap;
    wifi_sta_config_t sta;
    esp_netif_ip_info_t static_ip;
    wifi_mode_t mode;
    bool sta_dhcp;
} wifi_config_S;

static nvs_handle_t nvs;
static wifi_config_S wifi_config = {
    .ap = {
        .ssid = "ESPScope",
        .password = "88888888",
        .channel = 1,
        .authmode = WIFI_AUTH_WPA2_PSK,
        .max_connection = 4,
    },
    .sta = {
        .failure_retry_cnt = 20,
    },
    .mode = WIFI_MODE_AP,
};
static esp_netif_t *sta_netif, *ap_netif;

static uint8_t bssid[6] = {0};

static void event_handler(void *arg, esp_event_base_t event, int32_t event_id, void *event_data) {
    if(event_id == WIFI_EVENT_STA_CONNECTED) {
        memcpy(bssid, ((wifi_event_sta_connected_t *) event)->bssid, sizeof(bssid));
    }
}

void wifi_init(void) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_open("wifi", NVS_READWRITE, &nvs));
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_init());

    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    ap_netif = esp_netif_create_default_wifi_ap();
    assert(ap_netif);

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    wifi_init_config.nvs_enable = false;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_init(&wifi_init_config));

    size_t config_len = sizeof(wifi_config);
    esp_err_t err = nvs_get_blob(nvs, "config", &wifi_config, &config_len);
    if(err != ESP_ERR_NVS_NOT_FOUND) {
        ESP_ERROR_CHECK_WITHOUT_ABORT(err);
    }

    esp_event_handler_instance_t event_handler_instance;
    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &event_handler, NULL, &event_handler_instance
    ));

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_start());
    wifi_reconnect();
}

// reconnect with config
void wifi_reconnect(void) {
    esp_wifi_disconnect();
    esp_wifi_set_mode(wifi_config.mode);

    if(wifi_config.mode & WIFI_MODE_AP) {
        esp_wifi_set_config(WIFI_IF_AP, (wifi_config_t*) &wifi_config.ap);
    }

    if(wifi_config.mode & WIFI_MODE_STA) {
        esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t*) &wifi_config.sta);
        if(wifi_config.sta_dhcp) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcpc_start(sta_netif));
        } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcpc_stop(sta_netif));
            esp_netif_ip_info_t ip = {0};
            // TODO
            //ip.ip.addr = ipaddr_addr(EXAMPLE_STATIC_IP_ADDR);
            //ip.netmask.addr = ipaddr_addr(EXAMPLE_STATIC_NETMASK_ADDR);
            //ip.gw.addr = ipaddr_addr(EXAMPLE_STATIC_GW_ADDR);
            ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_ip_info(sta_netif, &ip));
        }
    }

    ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
}

void wifi_configure_ap(uint8_t *ssid, uint8_t ssid_len, uint8_t *pass, uint8_t pass_len, uint8_t channel, wifi_auth_mode_t authmode, uint8_t max_conn, bool hidden) {
    memcpy(wifi_config.ap.ssid, ssid, ssid_len);
    memcpy(wifi_config.ap.password, pass, pass_len);
    wifi_config.ap.channel = channel;
    wifi_config.ap.authmode = authmode;
    wifi_config.ap.max_connection = max_conn;
    wifi_config.ap.ssid_hidden = hidden;
}

void wifi_configure_sta(uint8_t *ssid, uint8_t ssid_len, uint8_t *pass, uint8_t pass_len, uint8_t *bssid, uint8_t bssid_len) {
    memcpy(wifi_config.sta.ssid, ssid, ssid_len);
    memcpy(wifi_config.sta.password, pass, pass_len);
    if(bssid && bssid_len) {
        memcpy(wifi_config.sta.bssid, bssid, sizeof(wifi_config.sta.bssid));
        wifi_config.sta.bssid_set = true;
    } else {
        wifi_config.sta.bssid_set = false;
    }
}

void wifi_save(void) {
    ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_blob(nvs, "config", &wifi_config, sizeof(wifi_config)));
}

// https://ieee-sensors.org/wp-content/uploads/2018/05/4a-esp8266_at_instruction_set_en.pdf
size_t wifi_command(uint8_t *data, size_t len, size_t max_len) {
    size_t resp_len = 0;

    uint8_t *under = memchr(data, '_', len);
    uint8_t *query = memchr(data, '?', len);
    bool persist = !query && under && memcmp(under, "_DEF", 4) == 0;

    // make a null terminated copy of AT command, excluding trailing ?
    uint8_t cmd[16] = {0};
    if(query)
        memcpy(cmd, data+1, query-data-1);
    else
        memcpy(cmd, data+1, len-1);

#define WIFI_CMD(target) (memcmp(cmd, target, sizeof(target)-1) == 0)

    if(query && WIFI_CMD("CWMODE")) {
        resp_len = snprintf((char*) data, max_len,
            "+%s:%d\r\nOK\r\n", cmd, wifi_config.mode);
        goto wifi_command_end;
    }

    if(query && WIFI_CMD("CWJAP")) {
        int rssi = 0;
        uint8_t channel = 0;
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_sta_get_rssi(&rssi));
        ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_get_channel(&channel, NULL));
        resp_len = snprintf((char*) data, max_len,
            "+%s:\"%.32s\",\"" MACSTR "\",%d,%d\r\nOK\r\n",
            cmd, wifi_config.sta.ssid, MAC2STR(bssid), channel, rssi);
        goto wifi_command_end;
    }

    if(query && WIFI_CMD("CWDHCP")) {
        resp_len = snprintf((char*) data, max_len,
            "+%s:%d\r\nOK\r\n", cmd, wifi_config.sta_dhcp << 1 | 1);
        goto wifi_command_end;
    }

    if(query && WIFI_CMD("CIPSTA")) {
        esp_netif_ip_info_t ip_info = {0};
        esp_netif_get_ip_info(ap_netif, &ip_info);
        resp_len = snprintf((char*) data, max_len,
            "+%s:ip:\"" IPSTR "\"\r\n"
            "+%s:gateway:\"" IPSTR "\"\r\n"
            "+%s:netmask:\"" IPSTR "\"\r\nOK\r\n",
            cmd, IP2STR(&ip_info.ip),
            cmd, IP2STR(&ip_info.gw),
            cmd, IP2STR(&ip_info.netmask));
        goto wifi_command_end;
    }

    if(query && WIFI_CMD("CWSAP")) {
        // TODO escape
        resp_len = snprintf((char*) data, max_len,
            "+%s:\"%.32s\",\"%.64s\",%d,%d,%d,%d\r\nOK\r\n",
            cmd, wifi_config.ap.ssid, wifi_config.ap.password,
            wifi_config.ap.channel, wifi_config.ap.authmode,
            wifi_config.ap.max_connection, wifi_config.ap.ssid_hidden);
        goto wifi_command_end;
    }

    if(memcmp(data, "+WIFICONN", 9) == 0) {
        wifi_reconnect();
        memcpy(data, "OK\r\n", 4);
        return 2;
    }

    if(memcmp(data, "+WIFISAVE", 9) == 0) {
        esp_err_t err = nvs_set_blob(nvs, "config", &wifi_config, sizeof(wifi_config));
        if(err == ESP_OK) {
            memcpy(data, "OK\r\n", 4);
            return 2;
        } else {
            const char *msg = esp_err_to_name(err);
            memcpy(data, msg, strlen(msg));
            return 4;
        }
    }

#undef WIFI_CMD

wifi_command_end:
    return resp_len < max_len ? resp_len : max_len;
}
