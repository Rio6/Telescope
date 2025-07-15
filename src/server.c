#include "server.h"
#include "synscan.h"

#include <esp_log.h>
#include <nvs_flash.h>
#include <lwip/sockets.h>

static nvs_handle_t nvs;
static ss_parser_S server_parser = {0};

typedef struct {
   in_port_t port;
} server_config_S;

static server_config_S server_config = {0};
static int sock;

void server_init(void) {
   ESP_ERROR_CHECK(nvs_open("server", NVS_READWRITE, &nvs));
   // TODO nvs load
   server_config.port = htons(8888);

   struct sockaddr_in addr;
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   addr.sin_family = AF_INET;
   addr.sin_port = server_config.port;

   sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
   assert(sock >= 0);

   int err = fcntl(sock, F_SETFL, O_NONBLOCK);
   assert(err >= 0);

   err = bind(sock, (struct sockaddr*) &addr, sizeof(addr));
   assert(err >= 0);
}

void server_task(void) {
   uint8_t buff[64] = {0};
   struct sockaddr_in addr;
   socklen_t socklen = sizeof(addr);
   ssize_t len = recvfrom(sock, buff, sizeof(buff), 0, (struct sockaddr*) &addr, &socklen);

   ESP_LOGD("server", "%s", buff);

   for(int i = 0; i < len; i++) {
      size_t resp_len = ss_handle_byte(&server_parser, buff[i]);

      if(resp_len) {
         int err = sendto(sock, server_parser.data, resp_len, 0, (struct sockaddr*) &addr, sizeof(addr));
         assert(err >= 0);
      }
   }
}

void server_command(uint8_t *data, size_t len) {
   // server_config.port = htons(port);
}
