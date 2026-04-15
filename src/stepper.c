// driver for A5984 https://www.allegromicro.com/~/media/Files/Datasheets/A5984-Datasheet.ashx
#include "stepper.h"
#include <driver/gpio.h>
#include <driver/mcpwm_prelude.h>
#include <esp_attr.h>

// declarations
typedef struct {
   gpio_num_t step;
   gpio_num_t ms1;
   gpio_num_t ms2;
   gpio_num_t ms3;
   gpio_num_t dir;
   gpio_num_t nfault;
   gpio_num_t nena;
} stepper_pins_S;

typedef enum {
   STEPPER_STOP,
   STEPPER_ACCEL,
   STEPPER_CRUISE,
   STEPPER_DECCEL,
} stepper_state_E;

typedef struct {
   const stepper_E id;
   const stepper_pins_S pins;

   mcpwm_timer_handle_t timer;
   mcpwm_oper_handle_t operator;
   mcpwm_cmpr_handle_t comparator;
   mcpwm_gen_handle_t step_generator;
   mcpwm_gen_handle_t ena_generator;

   stepper_ustep_E ustep;
   stepper_mode_E mode;
   stepper_speed_E speed;
   stepper_dir_E dir;
   uint32_t period;
   uint32_t cpr;

   uint32_t count;
   uint32_t target;
   uint32_t target_period;
   uint32_t accel_speed;
   stepper_state_E state;
} stepper_state_S;

// definitions
static stepper_state_S stepper_states[STEPPER_COUNT] = {
   [STEPPER_RA] = {
      .id     = STEPPER_RA,
      .pins   = {.step = 14, .ms1 = 21, .ms2 = 22, .ms3 = 23, .dir = 12, .nfault = 34, .nena = 19},
      .mode   = STEPPER_TRACKING,
      .speed  = STEPPER_SLOW,
      .ustep  = STEPPER_USTEP_32,
      .dir    = STEPPER_CW,
      .period = 10,
      .cpr    = 32 * STEPPER_STEPS_PER_REV * 3 * 256,
      .state  = STEPPER_STOP,
   },
   [STEPPER_DE] = {
      .id     = STEPPER_DE,
      .pins   = {.step = 15, .ms1 = 25, .ms2 = 26, .ms3 = 27, .dir = 13, .nfault = 35, .nena = 5},
      .mode   = STEPPER_TRACKING,
      .speed  = STEPPER_SLOW,
      .ustep  = STEPPER_USTEP_32,
      .dir    = STEPPER_CW,
      .period = 10,
      .cpr    = 32 * STEPPER_STEPS_PER_REV * 3 * 257,
      .state  = STEPPER_STOP,
   },
};

static const gpio_num_t nRST = 32;
static const uint32_t PULSE_WIDTH_FACTOR = 10;

static const uint32_t ACCEL_FACTOR = 512;
static const uint32_t ACCEL_STOP   = 1;
static const uint32_t ACCEL_STEP   = 1;

static bool stepper_timer_stop_callback(mcpwm_timer_handle_t, const mcpwm_timer_event_data_t*, void*);
static bool stepper_pulse_callback(mcpwm_cmpr_handle_t, const mcpwm_compare_event_data_t*, void*);

void stepper_init(void) {
   // global GPIO config
   gpio_set_direction(nRST, GPIO_MODE_OUTPUT);
   gpio_set_level(nRST, 1);

   // config for each stepper
   for(stepper_E stepper = STEPPER_0; stepper != STEPPER_COUNT; stepper++) {
      const stepper_pins_S *pins = &stepper_states[stepper].pins;
      stepper_state_S *state = &stepper_states[stepper];

      // GPIO config
      gpio_config_t config = {
         .pin_bit_mask =
            (1 << pins->step) |
            (1 << pins->ms1)  |
            (1 << pins->ms2)  |
            (1 << pins->ms3)  |
            (1 << pins->dir)  |
            (1 << pins->nena),
         .mode = GPIO_MODE_OUTPUT,
      };
      ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_config(&config));
      ESP_ERROR_CHECK_WITHOUT_ABORT(gpio_set_direction(pins->nfault, GPIO_MODE_INPUT));

      // start with motors /*disabled*/ enabled
      //gpio_set_level(state->pins.nena, 1);
      gpio_set_level(state->pins.nena, 0);

      // configure microstep
      stepper_ustep_E ustep = stepper_states[stepper].ustep;
      gpio_set_level(pins->ms1, (ustep >> 0) & 1);
      gpio_set_level(pins->ms2, (ustep >> 1) & 1);
      gpio_set_level(pins->ms3, (ustep >> 2) & 1);

      // MCPWM config
      mcpwm_timer_config_t timer_config = {
         .group_id      = stepper,
         .resolution_hz = STEPPER_FREQ * PULSE_WIDTH_FACTOR,
         .count_mode    = MCPWM_TIMER_COUNT_MODE_UP,
         .period_ticks  = state->period,
         .flags.update_period_on_empty = 1,
      };
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_timer(&timer_config, &state->timer));

      mcpwm_timer_event_callbacks_t timer_callback = {
         .on_stop = stepper_timer_stop_callback,
      };
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_register_event_callbacks(state->timer, &timer_callback, (void*) state));

      mcpwm_operator_config_t oper_config = {
         .group_id = stepper,
      };
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_operator(&oper_config, &state->operator));
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_operator_connect_timer(state->operator, state->timer));

      mcpwm_comparator_config_t cmpr_config = {0};
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_comparator(state->operator, &cmpr_config, &state->comparator));
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_comparator_set_compare_value(state->comparator, 1));

      mcpwm_comparator_event_callbacks_t cmpr_callback = {
         .on_reach = stepper_pulse_callback,
      };
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_comparator_register_event_callbacks(state->comparator, &cmpr_callback, (void*) state));

      // generator for step signal
      mcpwm_generator_config_t step_gen_config = {
         .gen_gpio_num = pins->step,
      };
      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_new_generator(state->operator, &step_gen_config, &state->step_generator));

      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_generator_set_action_on_timer_event(state->step_generator, (mcpwm_gen_timer_event_action_t) {
         .event  = MCPWM_TIMER_EVENT_EMPTY,
         .action = MCPWM_GEN_ACTION_HIGH,
      }));

      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_generator_set_action_on_compare_event(state->step_generator, (mcpwm_gen_compare_event_action_t) {
         .comparator = state->comparator,
         .action     = MCPWM_GEN_ACTION_LOW,
      }));

      ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_enable(state->timer));
   }
}

void stepper_task(void) {
   for(stepper_E stepper = STEPPER_0; stepper != STEPPER_COUNT; stepper++) {
      stepper_state_S *state = &stepper_states[stepper];

      if(state->state == STEPPER_ACCEL) {
         state->accel_speed += ACCEL_STEP;
         if(ACCEL_FACTOR / state->accel_speed <= state->target_period) {
            ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_set_period(state->timer, state->target_period));
            state->state = STEPPER_CRUISE;
         } else {
            ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_set_period(state->timer, ACCEL_FACTOR / state->accel_speed));
         }
      }

      if(state->state == STEPPER_DECCEL) {
         if(state->accel_speed > ACCEL_STEP) {
            state->accel_speed -= ACCEL_STEP;
            ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_set_period(state->timer, ACCEL_FACTOR / state->accel_speed));
         } else {
            stepper_stop_instant(stepper);
         }
      }
   }
}

void stepper_start(stepper_E stepper) {
   stepper_state_S *state = &stepper_states[stepper];

   if(state->mode == STEPPER_GOTO && state->target == state->count)
      return;

   state->target_period = state->period * PULSE_WIDTH_FACTOR;
   if(state->speed == STEPPER_FAST && state->target_period >= STEPPER_FAST_RATIO)
      state->target_period /= STEPPER_FAST_RATIO;

   state->accel_speed = ACCEL_STOP;
   state->state = STEPPER_ACCEL;

   gpio_set_level(state->pins.nena, 0);
   gpio_set_level(state->pins.dir, state->dir == STEPPER_CCW);
   ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_set_period(state->timer, ACCEL_FACTOR / state->accel_speed));
   ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_start_stop(state->timer, MCPWM_TIMER_START_NO_STOP));
}

void stepper_stop(stepper_E stepper) {
   stepper_states[stepper].state = STEPPER_DECCEL;
}

void stepper_stop_instant(stepper_E stepper) {
   stepper_state_S *state = &stepper_states[stepper];
   ESP_ERROR_CHECK_WITHOUT_ABORT(mcpwm_timer_start_stop(state->timer, MCPWM_TIMER_STOP_FULL));
}

bool stepper_busy(stepper_E stepper) {
   return stepper_states[stepper].state != STEPPER_STOP;
}

uint32_t stepper_cpr(stepper_E stepper) {
   return stepper_states[stepper].cpr;
}

void stepper_set_count(stepper_E stepper, uint32_t count) {
   stepper_states[stepper].count = count;
}

uint32_t stepper_get_count(stepper_E stepper) {
   return stepper_states[stepper].count;
}

void stepper_set_period(stepper_E stepper, uint32_t period) {
   stepper_states[stepper].period = period;
}

void stepper_set_target(stepper_E stepper, uint32_t target) {
   stepper_states[stepper].target = target;
}

void stepper_set_mode(stepper_E stepper, stepper_mode_E mode, stepper_speed_E speed, stepper_dir_E dir) {
   stepper_state_S *state = &stepper_states[stepper];
   state->mode = mode;
   state->speed = speed;
   state->dir = dir;
}

uint32_t stepper_get_period(stepper_E stepper) {
   return stepper_states[stepper].period;
}

uint32_t stepper_get_target(stepper_E stepper) {
   return stepper_states[stepper].target;
}

stepper_mode_E stepper_get_mode(stepper_E stepper) {
   return stepper_states[stepper].mode;
}

stepper_speed_E stepper_get_speed(stepper_E stepper) {
   return stepper_states[stepper].speed;
}

stepper_dir_E stepper_get_dir(stepper_E stepper) {
   return stepper_states[stepper].dir;
}

bool stepper_get_fault(stepper_E stepper) {
   return !gpio_get_level(stepper_states[stepper].pins.nfault);
}

static bool IRAM_ATTR stepper_timer_stop_callback(mcpwm_timer_handle_t timer, const mcpwm_timer_event_data_t *edata, void *user_ctx) {
   stepper_state_S *state = user_ctx;
   //gpio_set_level(state->pins.nena, 1);
   state->state = STEPPER_STOP;
   return false;
}

static bool IRAM_ATTR stepper_pulse_callback(mcpwm_cmpr_handle_t comparator, const mcpwm_compare_event_data_t *edata, void *user_ctx) {
   stepper_state_S *state = user_ctx;

   if(state->dir == STEPPER_CW) {
      state->count++;
   } else {
      state->count--;
   }

   if(state->mode == STEPPER_GOTO && state->count == state->target) {
      stepper_stop_instant(state->id);
   }

   return false;
}
