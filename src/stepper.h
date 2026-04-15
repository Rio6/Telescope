#ifndef STEPPER_H
#define STEPPER_H

#include <stdint.h>
#include <stdbool.h>

#define STEPPER_FREQ 16000
#define STEPPER_STEPS_PER_REV 200
#define STEPPER_FAST_RATIO 10

typedef enum {
   STEPPER_0  = 0,
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

typedef enum {
   STEPPER_GOTO,
   STEPPER_TRACKING,
} stepper_mode_E;

typedef enum {
   STEPPER_SLOW,
   STEPPER_FAST,
} stepper_speed_E;

typedef enum {
   STEPPER_CW,
   STEPPER_CCW,
} stepper_dir_E;

void stepper_init(void);
void stepper_task(void);

void stepper_start(stepper_E);
void stepper_stop(stepper_E);
void stepper_stop_instant(stepper_E);

bool stepper_busy(stepper_E);
uint32_t stepper_cpr(stepper_E);

void stepper_set_count(stepper_E, uint32_t);
uint32_t stepper_get_count(stepper_E);

void stepper_set_period(stepper_E, uint32_t);
void stepper_set_target(stepper_E, uint32_t);
void stepper_set_mode(stepper_E, stepper_mode_E, stepper_speed_E, stepper_dir_E);

uint32_t stepper_get_period(stepper_E);
uint32_t stepper_get_target(stepper_E);
stepper_mode_E stepper_get_mode(stepper_E);
stepper_speed_E stepper_get_speed(stepper_E);
stepper_dir_E stepper_get_dir(stepper_E);
bool stepper_get_fault(stepper_E);

#endif
