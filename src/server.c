#include "server.h"
#include "synscan.h"

#include <esp_log.h>
#include <lwip/sockets.h>

static ss_parser_S server_parser = {0};

static int sock;

void server_init(void) {
   struct sockaddr_in addr;
   addr.sin_addr.s_addr = htonl(INADDR_ANY);
   addr.sin_family = AF_INET;
   addr.sin_port = htons(11880);

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

   if(len > 0) {
      ESP_LOGD("server rx", "%s", buff);
   }

   for(int i = 0; i < len; i++) {
      size_t resp_len = ss_handle_byte(&server_parser, buff[i]);

      if(resp_len) {
         ESP_LOGD("server tx", "%.*s", resp_len, server_parser.data);
         int err = sendto(sock, server_parser.data, resp_len, 0, (struct sockaddr*) &addr, sizeof(addr));
         assert(err >= 0);
      }
   }
}

void server_command(uint8_t *data, size_t len) {
   // server_config.port = htons(port);
}
