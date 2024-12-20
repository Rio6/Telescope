// driver for A5984 https://www.allegromicro.com/~/media/Files/Datasheets/A5984-Datasheet.ashx
#include "stepper.h"
#include <driver/gpio.h>

// declarations
typedef struct {
   gpio_num_t step;
   gpio_num_t ms1;
   gpio_num_t ms2;
   gpio_num_t ms3;
   gpio_num_t dir;
   gpio_num_t nfault;
} stepper_pins_S;

typedef struct {
   const stepper_pins_S pins;
   stepper_ustep_E ustep;
   uint32_t pulse_counter;
   uint32_t steps;
   uint32_t target;
} stepper_state_S;

// definitions
static stepper_state_S stepper_states[STEPPER_COUNT] = {
   [STEPPER_RA] = {
      .pins = {.step = 14, .ms1 = 21, .ms2 = 22, .ms3 = 23, .dir = 12},
      .ustep = STEPPER_USTEP_32,
   },
   [STEPPER_DE] = {
      .pins = {.step = 15, .ms1 = 25, .ms2 = 26, .ms3 = 27, .dir = 13},
      .ustep = STEPPER_USTEP_32,
   },
};

static const uint32_t STEPPER_PERIOD = 500;
static const gpio_num_t nENA = 19;
static const gpio_num_t nRST = 32;

void stepper_init(void) {
   gpio_set_direction(nENA, GPIO_MODE_OUTPUT);
   gpio_set_direction(nRST, GPIO_MODE_OUTPUT);
   gpio_set_level(nENA, 0);
   gpio_set_level(nRST, 1);

   for(stepper_E stepper = STEPPER_0; stepper != STEPPER_COUNT; stepper++) {
      const stepper_pins_S *pins = &stepper_states[stepper].pins;

      gpio_set_direction(pins->step,   GPIO_MODE_OUTPUT);
      gpio_set_direction(pins->ms1,    GPIO_MODE_OUTPUT);
      gpio_set_direction(pins->ms2,    GPIO_MODE_OUTPUT);
      gpio_set_direction(pins->ms3,    GPIO_MODE_OUTPUT);
      gpio_set_direction(pins->dir,    GPIO_MODE_OUTPUT);
      gpio_set_direction(pins->nfault, GPIO_MODE_INPUT);

      stepper_ustep_E ustep = stepper_states[stepper].ustep;
      gpio_set_level(pins->ms1, (ustep >> 0) & 1);
      gpio_set_level(pins->ms2, (ustep >> 1) & 1);
      gpio_set_level(pins->ms3, (ustep >> 2) & 1);
   }
}

void stepper_task() {
   for(stepper_E stepper = STEPPER_0; stepper != STEPPER_COUNT; stepper++) {
      stepper_state_S *state = &stepper_states[stepper];
      if(state->pulse_counter == 0) {
         gpio_set_level(state->pins.step, 1);
         state->pulse_counter = STEPPER_PERIOD;
      } else {
         gpio_set_level(state->pins.step, 0);
         state->pulse_counter--;
      }
   }
}

bool stepper_busy(stepper_E stepper) {
   stepper_state_S *state = &stepper_states[stepper];
   return state->target != state->steps;
}

void stepper_step(stepper_E stepper, int32_t steps) {
   stepper_state_S *state = &stepper_states[stepper];
   gpio_set_level(state->pins.dir, steps > 0);
   state->target = state->steps + steps;
   state->pulse_counter = 1;
}
