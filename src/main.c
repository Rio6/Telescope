#include "stepper.h"
#include "synscan.h"

#include <esp_timer.h>

static esp_timer_handle_t task_timer;

void app_task(void *args) {
    ss_task();
}

void app_main(void) {

    stepper_init();
    ss_init();

    esp_timer_create_args_t args = {
        .name = "app_task",
        .callback = app_task,
    };

    esp_timer_create(&args, &task_timer);
    esp_timer_start_periodic(task_timer, 10000); // us

    while(1);
}
