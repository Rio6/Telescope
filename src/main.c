#include "stepper.h"
#include "wifi.h"
#include "server.h"
#include "uart.h"
#include "sense.h"

#include <esp_log.h>
#include <esp_timer.h>
#include <esp_event.h>
#include <nvs_flash.h>
#include <driver/gpio.h>

static esp_timer_handle_t task_timer;

void app_task(void *args) {
   uart_task();
   server_task();

   // LED when motor fault
   gpio_set_level(GPIO_NUM_2, stepper_get_fault(STEPPER_RA) || stepper_get_fault(STEPPER_DE));
}

void app_main(void) {

   esp_err_t err = nvs_flash_init();
   if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK_WITHOUT_ABORT(nvs_flash_erase());
      err = nvs_flash_init();
   }
   ESP_ERROR_CHECK_WITHOUT_ABORT(err);

   ESP_ERROR_CHECK(esp_event_loop_create_default());

   gpio_set_direction(GPIO_NUM_2, GPIO_MODE_OUTPUT);

   sense_init();
   uart_init();
   wifi_init();
   server_init();
   stepper_init();

   esp_timer_create_args_t args = {
      .name = "app_task",
      .callback = app_task,
   };

   esp_timer_create(&args, &task_timer);
   esp_timer_start_periodic(task_timer, 10000); // us
}
