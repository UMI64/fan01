#pragma once
#include "helper.hpp"
class fan
{
private:
    bool fan_power_switch = false;
    float current_power = 0;
    float target_power = 0;
    float current_duty_cycle = 0;

    gpio_num_t fan_power_pin = GPIO_NUM_4;
    gpio_num_t fan_pwm_pin = GPIO_NUM_13;
    gpio_num_t fan_speed_pin = GPIO_NUM_12;
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
            ESP_ERROR_CHECK(adc_cali_delete_scheme_curve_fitting(adc_channel.adc_cali_handle));
            ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_channel.adc_cali_handle));
        }
    }
    thread_helper *power_controller_thread;
    void set_duty_cycle(float p);
    void power_controller_task(void *param);

public:
    float voltage;
    float current;
    float power;

    fan(adc_oneshot_unit_handle_t &adc_oneshot_unit_handle);
    ~fan();
    void set_switch(bool sw);
    void set_target_power(float w);
    float get_duty_cycle(void);
    float read_fan_voltage();
    float read_fan_current();
    float read_fan_power();
};