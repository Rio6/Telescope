#include "stepper.h"
#include <esp_timer.h>

static esp_timer_handle_t task_timer;

void app_task(void *args) {
    stepper_task();
}

void app_main(void) {

    esp_timer_init();
    stepper_init();

    esp_timer_create_args_t args = {
        .name = "app_task",
        .callback = app_task,
    };

    esp_timer_create(&args, &task_timer);
    esp_timer_start_periodic(task_timer, 1000);

    while(1);
}
