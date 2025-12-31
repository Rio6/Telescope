#include "wifi.h"

#include <esp_wifi.h>
#include <esp_mac.h>
#include <nvs_flash.h>
#include <lwip/ip4_addr.h>

#include <ctype.h>
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
   .sta_dhcp = true,
};
static esp_netif_t *sta_netif, *ap_netif;

static uint8_t bssid[6] = {0};

static const uint8_t DELAY_COUNT = 100;
static uint8_t save_count = 0;
static uint8_t conn_count = 0;

// dst length needs to be 2x of src + 3 (including surrounding quotes and terminating null)
// synscan seems to escape them with forward slash /
static void escape_string(uint8_t *dst, uint8_t *src, size_t src_len) {
   *dst++ = '"';
   for(int i = 0; i < src_len; i++) {
      if(!src[i]) break;
      if(src[i] == ',' || src[i] == '/' || src[i] == '"') {
         *dst++ = '/';
      }
      *dst++ = src[i];
   }
   *dst++ = '"';
   *dst = '\0';
}

// this one can be done in place, returns pointer to next string segment
static uint8_t *unescape_string(uint8_t *str, size_t max_len) {
   uint8_t *dst = str;
   bool escaping = false;
   int i;
   for(i = 0; i < max_len; i++) {
      if(!escaping) {
         if(str[i] == '/') {
            escaping = true;
            continue;
         }
         if(str[i] == '"') {
            if(i == 0) continue;
            i++;
            break;
         }
      }
      *dst++ = str[i];
      escaping = false;
   }
   *dst = '\0';
   return str+i;
}

static void event_handler(void *arg, esp_event_base_t event, int32_t event_id, void *event_data) {
   if(event_id == WIFI_EVENT_STA_CONNECTED) {
      memcpy(bssid, ((wifi_event_sta_connected_t *) event)->bssid, sizeof(bssid));
   }
}

void wifi_init(void) {
   save_count = 0;
   conn_count = 0;

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

void wifi_task(void) {
   if(save_count == 1) ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_set_blob(nvs, "config", &wifi_config, sizeof(wifi_config)));
   if(save_count > 0)  save_count--;
   if(conn_count == 1) wifi_reconnect();
   if(conn_count > 0)  conn_count--;
}

// reconnect with config
void wifi_reconnect(void) {
   esp_wifi_disconnect();
   ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_mode(wifi_config.mode));

   if(wifi_config.mode == WIFI_MODE_AP || wifi_config.mode == WIFI_MODE_APSTA) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_AP, (wifi_config_t*) &wifi_config.ap));
   }

   if(wifi_config.mode == WIFI_MODE_STA || wifi_config.mode == WIFI_MODE_APSTA) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_set_config(WIFI_IF_STA, (wifi_config_t*) &wifi_config.sta));
      if(wifi_config.sta_dhcp) {
         ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_dhcpc_start(sta_netif));
      } else {
         esp_netif_dhcpc_stop(sta_netif);
         ESP_ERROR_CHECK_WITHOUT_ABORT(esp_netif_set_ip_info(sta_netif, &wifi_config.static_ip));
      }
   }

   //ESP_ERROR_CHECK_WITHOUT_ABORT(esp_wifi_connect());
   esp_wifi_connect();
}

// https://ieee-sensors.org/wp-content/uploads/2018/05/4a-esp8266_at_instruction_set_en.pdf
size_t wifi_command(uint8_t *data, size_t len, size_t max_len) {
   size_t resp_len = 0;

   uint8_t *under = memchr(data, '_', len);
   uint8_t *query = memchr(data, '?', len);
   uint8_t *equal = memchr(data, '=', len);

   // ignore special chars after = sign
   if(equal && equal < query) query = NULL;
   if(equal && equal < under) under = NULL;

   bool persist = !query && under && memcmp(under, "_DEF", 4) == 0;

   // make a null terminated copy of AT command, excluding trailing ?
   uint8_t cmd[16] = {0};
   if(query)
      memcpy(cmd, data+1, query-data-1);
   else
      memcpy(cmd, data+1, len-1);

#define WIFI_CMD(target) (memcmp(cmd, target, sizeof(target)-1) == 0)

   if(WIFI_CMD("CWMODE")) {
      if(query) {
         resp_len = snprintf((char*) data, max_len,
                             "+%s:%d\r\nOK\r\n", cmd, wifi_config.mode);
         goto wifi_command_end;
      }

      if(equal) {
         if(!isdigit(*(equal+1))) goto wifi_command_fail;
         uint8_t mode = atoi((char*) equal+1);
         if(mode > WIFI_MODE_MAX) goto wifi_command_fail;
         wifi_config.mode = mode;
         goto wifi_command_ok;
      }
   }

   if(WIFI_CMD("CWJAP")) {
      if(query) {
         int rssi = 0;
         uint8_t channel = 0;
         uint8_t ssid_escape[sizeof(wifi_config.sta.ssid)+3];
         escape_string(ssid_escape, wifi_config.sta.ssid, sizeof(wifi_config.sta.ssid));
         esp_wifi_sta_get_rssi(&rssi);
         esp_wifi_get_channel(&channel, NULL);
         resp_len = snprintf((char*) data, max_len,
                             "+%s:%s,\"" MACSTR "\",%d,%d\r\nOK\r\n",
                             cmd, ssid_escape, MAC2STR(bssid), channel, rssi);
         goto wifi_command_end;
      }

      if(equal) {
         uint8_t *ssid = equal+1;
         uint8_t *pass = unescape_string(ssid, len-(ssid-data)) + 1;

         if(*pass != '"') goto wifi_command_fail;
         unescape_string(pass, len-(pass-data));

         strncpy((char*) wifi_config.sta.ssid, (char*) ssid, sizeof(wifi_config.sta.ssid));
         strncpy((char*) wifi_config.sta.password, (char*) pass, sizeof(wifi_config.sta.password));

         goto wifi_command_ok;
      }
   }

   if(WIFI_CMD("CWDHCP")) {
      if(query) {
         resp_len = snprintf((char*) data, max_len,
                             "+%s:%d\r\nOK\r\n", cmd, wifi_config.sta_dhcp << 1 | 1);
         goto wifi_command_end;
      }

      if (equal) {
         char *start = (char*) equal+1;
         char *end = NULL;

         uint8_t mode = strtol(start, &end, 10);
         if(start == end) goto wifi_command_fail;
         start = end+1;

         uint8_t enable = strtol(start, &end, 10);
         if(start == end) goto wifi_command_fail;

         if(mode == 1) {
            wifi_config.sta_dhcp = !!enable;
         }
         goto wifi_command_ok;
      }
   }

   if(WIFI_CMD("CIPSTA")) {
      if(query) {
         esp_netif_ip_info_t ip_info = {0};
         esp_netif_get_ip_info(sta_netif, &ip_info);
         resp_len = snprintf((char*) data, max_len,
                             "+%s:ip:\"" IPSTR "\"\r\n"
                             "+%s:gateway:\"" IPSTR "\"\r\n"
                             "+%s:netmask:\"" IPSTR "\"\r\nOK\r\n",
                             cmd, IP2STR(&ip_info.ip),
                             cmd, IP2STR(&ip_info.gw),
                             cmd, IP2STR(&ip_info.netmask));
         goto wifi_command_end;
      }

      if(equal) {
         wifi_config.sta_dhcp = false;

         uint8_t *ipstr = equal+1;
         uint8_t *gwstr = unescape_string(ipstr, len-(ipstr-data)) + 1;
         wifi_config.static_ip.ip.addr = ipaddr_addr((char*) equal+1);

         if(*gwstr != '"') {
            wifi_config.static_ip.gw.addr = wifi_config.static_ip.ip.addr;
            goto wifi_command_ok;
         }
         uint8_t *nmstr = unescape_string(gwstr, len-(gwstr-data)) + 1;
         wifi_config.static_ip.gw.addr = ipaddr_addr((char*) gwstr);

         if(*nmstr != '"') {
            wifi_config.static_ip.netmask.addr = ipaddr_addr("255.255.255.0");
            goto wifi_command_ok;
         }
         unescape_string(nmstr, len-(nmstr-data));
         wifi_config.static_ip.netmask.addr = ipaddr_addr((char*) nmstr);

         goto wifi_command_ok;
      }
   }

   if(WIFI_CMD("CWSAP")) {
      if(query) {
         uint8_t ssid_escape[sizeof(wifi_config.ap.ssid)+3];
         uint8_t pass_escape[sizeof(wifi_config.ap.password)+3];
         escape_string(ssid_escape, wifi_config.ap.ssid, sizeof(wifi_config.ap.ssid));
         escape_string(pass_escape, wifi_config.ap.password, sizeof(wifi_config.ap.password));

         resp_len = snprintf((char*) data, max_len,
                             "+%s:%s,%s,%d,%d,%d,%d\r\nOK\r\n",
                             cmd, ssid_escape, pass_escape,
                             wifi_config.ap.channel, wifi_config.ap.authmode,
                             wifi_config.ap.max_connection, wifi_config.ap.ssid_hidden);
         goto wifi_command_end;
      }

      if(equal) {
         uint8_t *ssid = equal+1;
         uint8_t *pass = unescape_string(ssid, len-(ssid-data)) + 1;

         char *end = (char*) unescape_string(pass, len-(pass-data));
         char *start = end+1;

         uint8_t channel = strtol(start, &end, 10);
         if(start == end) goto wifi_command_fail;
         start = end+1;

         uint8_t authmode = strtol(start, &end, 10);
         if(start == end) goto wifi_command_fail;
         start = end+1;

         uint8_t max_conn = strtol(start, &end, 10);
         if(start == end) max_conn = 4;
         start = end+1;

         uint8_t hidden = strtol(start, &end, 10);
         if(start == end) hidden = 0;

         strncpy((char*) wifi_config.ap.ssid, (char*) ssid, sizeof(wifi_config.ap.ssid));
         strncpy((char*) wifi_config.ap.password, (char*) pass, sizeof(wifi_config.ap.password));
         wifi_config.ap.channel = channel;
         wifi_config.ap.authmode = authmode;
         wifi_config.ap.max_connection = max_conn;
         wifi_config.ap.ssid_hidden = hidden;

         goto wifi_command_ok;
      }
   }

   if(WIFI_CMD("RST")) {
      esp_restart();
   }

   if(WIFI_CMD("RESTORE")) {
      esp_err_t err = nvs_erase_all(nvs);
      if(err == ESP_OK) esp_restart();
      resp_len = snprintf((char*) data, max_len,
                          "%s\r\n",
                          esp_err_to_name(err));
      goto wifi_command_end;
   }

#undef WIFI_CMD

wifi_command_fail:
   memcpy(data, "FAIL\r\n", 6);
   resp_len = 6;
   goto wifi_command_end;

wifi_command_ok:
   memcpy(data, "OK\r\n", 4);
   resp_len = 4;
   goto wifi_command_end;

wifi_command_end:
   if(persist) save_count = DELAY_COUNT;
   if(equal)   conn_count = DELAY_COUNT;
   return resp_len < max_len ? resp_len : max_len;
}
