#include "sense.h"

#include <esp_adc/adc_oneshot.h>

#define VSENSE_CHAN ADC_CHANNEL_3
#define ISENSE_CHAN ADC_CHANNEL_0

static adc_oneshot_unit_handle_t adc_handle;

void sense_init(void) {
   adc_oneshot_unit_init_cfg_t init_config = {
      .unit_id = ADC_UNIT_1,
      .ulp_mode = ADC_ULP_MODE_DISABLE,
   };
   ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &adc_handle));

   adc_oneshot_chan_cfg_t chan_config = {
      .bitwidth = ADC_BITWIDTH_12,
      .atten = ADC_ATTEN_DB_6,
   };
   ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, ISENSE_CHAN, &chan_config));
   ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_handle, VSENSE_CHAN, &chan_config));
}

float sense_isense(void) {
   int raw = 0;
   ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, ISENSE_CHAN, &raw));
   return raw * 2 * 1.1 / (1<<12) / 20 / 0.4; // raw * atten * vref / 2^BIT_WIDTH / INA gain / Rsense
}

float sense_vsense(void) {
   int raw = 0;
   ESP_ERROR_CHECK(adc_oneshot_read(adc_handle, VSENSE_CHAN, &raw));
   return raw * 2 * 1.1 / (1<<12); // raw * atten * vref / 2^BIT_WIDTH
}
