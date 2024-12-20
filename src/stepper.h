#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>
#include <stdbool.h>

typedef enum {
   STEPPER_0 = 0,
   STEPPER_RA = 0,
   STEPPER_DE,
   STEPPER_COUNT,
} stepper_E;

typedef enum {
   STEPPER_USTEP_1T = 0,
   STEPPER_USTEP_2T,
   STEPPER_USTEP_16,
   STEPPER_USTEP_32,
   STEPPER_USTEP_1,
   STEPPER_USTEP_2,
   STEPPER_USTEP_4,
   STEPPER_USTEP_8,
} stepper_ustep_E;

void stepper_init(void);
void stepper_task(void);
bool stepper_busy(stepper_E stepper);
void stepper_step(stepper_E stepper, int32_t steps);

#endif
