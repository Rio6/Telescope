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
   stepper_mode_E mode;
   stepper_dir_E dir;
   uint32_t pulse_counter;
   uint32_t steps;
   uint32_t target;
} stepper_state_S;

// definitions
static stepper_state_S stepper_states[STEPPER_COUNT] = {
   [STEPPER_RA] = {
      .pins = {.step = 14, .ms1 = 21, .ms2 = 22, .ms3 = 23, .dir = 12},
      .mode = STEPPER_TRACKING,
      .dir = STEPPER_CW,
      .ustep = STEPPER_USTEP_32,
   },
   [STEPPER_DE] = {
      .pins = {.step = 15, .ms1 = 25, .ms2 = 26, .ms3 = 27, .dir = 13},
      .mode = STEPPER_TRACKING,
      .dir = STEPPER_CW,
      .ustep = STEPPER_USTEP_32,
   },
};

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

bool stepper_busy(stepper_E stepper) {
   return false;
}

uint32_t stepper_cpr(stepper_E stepper) {
   uint8_t ustep_factor;
   switch(stepper_states[stepper].ustep) {
      case STEPPER_USTEP_1:
      case STEPPER_USTEP_1T:
         ustep_factor = 1;
         break;
      case STEPPER_USTEP_2:
      case STEPPER_USTEP_2T:
         ustep_factor = 2;
         break;
      case STEPPER_USTEP_4:
         ustep_factor = 4;
         break;
      case STEPPER_USTEP_8:
         ustep_factor = 8;
         break;
      case STEPPER_USTEP_16:
         ustep_factor = 16;
         break;
      case STEPPER_USTEP_32:
         ustep_factor = 32;
         break;
      default:
         ustep_factor = 1;
         break;
   }
   return ustep_factor * STEPPER_STEPS_PER_REV * STEPPER_GEAR_RATIO;
}

stepper_mode_E stepper_get_mode(stepper_E stepper) {
   return stepper_states[stepper].mode;
}

stepper_dir_E stepper_get_dir(stepper_E stepper) {
   return stepper_states[stepper].dir;
}

void stepper_set_mode(stepper_E stepper, stepper_mode_E mode, stepper_dir_E dir) {
   stepper_state_S *state = &stepper_states[stepper];
   state->mode = mode;
   state->dir = dir;
}
