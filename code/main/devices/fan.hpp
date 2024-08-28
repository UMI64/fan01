#pragma once
#include "helper.hpp"
class fan
{
private:
    gpio_num_t fan_power_pin = GPIO_NUM_4;
    gpio_num_t fan_pwm_pin = GPIO_NUM_13;
    gpio_num_t fan_speed_pin = GPIO_NUM_12;
    ledc_channel_t ledc_channel;
    struct adc_channel_unit_t
    {
        adc_channel_t adc_channel;
        adc_cali_handle_t adc_cali_handle;
        adc_atten_t adc_atten;
    } adc_channel_current, adc_channel_voltage;
    adc_oneshot_unit_handle_t adc_oneshot_unit_handle;
    void set_adc_range(struct adc_channel_unit_t &adc_channel, adc_atten_t adc_atten)
    {
        if (adc_channel.adc_atten != adc_atten)
        {
            adc_channel.adc_atten = adc_atten;
            adc_oneshot_chan_cfg_t adc_chan_cfg = {};
            adc_chan_cfg.atten = adc_atten;
            adc_chan_cfg.bitwidth = ADC_BITWIDTH_12;
            ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_oneshot_unit_handle, adc_channel.adc_channel, &adc_chan_cfg));
            adc_cali_curve_fitting_config_t cali_config = {};
            cali_config.unit_id = ADC_UNIT_1;
            cali_config.chan = adc_channel.adc_channel;
            cali_config.atten = adc_atten;
            cali_config.bitwidth = ADC_BITWIDTH_12;
            ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_channel.adc_cali_handle));
        }
    }

public:
    fan(ledc_channel_t ledc_channel, adc_oneshot_unit_handle_t &adc_oneshot_unit_handle);
    void set_switch(bool sw);
    void set_speed(float p);
    float get_speed(void);
    float read_fan_voltage();
    float read_fan_current();
    float read_fan_power();
};