#include "synscan.h"
#include <driver/uart.h>

static QueueHandle_t uart_queue;

static ss_parser_S uart_parser = {0};

void uart_init(void) {
   uart_config_t uart_config = {
      .baud_rate = 115200,
      .data_bits = UART_DATA_8_BITS,
      .parity    = UART_PARITY_DISABLE,
      .stop_bits = UART_STOP_BITS_1,
      .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
   };
   ESP_ERROR_CHECK(uart_param_config(UART_NUM_0, &uart_config));
   ESP_ERROR_CHECK(uart_driver_install(UART_NUM_0, 1024, 1024, 10, &uart_queue, 0));
}

void uart_task(void) {
   size_t uart_len = 0;
   uart_get_buffered_data_len(UART_NUM_0, &uart_len);
   for(size_t i = 0; i < uart_len; i++) {
      uint8_t byte = 0;
      uart_read_bytes(UART_NUM_0, &byte, 1, portMAX_DELAY);
      size_t resp_len = ss_handle_byte(&uart_parser, byte);

      if(resp_len) {
         uart_write_bytes(UART_NUM_0, uart_parser.data, resp_len);
      }
   }
}
