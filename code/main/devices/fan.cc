#include "fan.hpp"
fan::fan(ledc_channel_t ledc_channel, adc_oneshot_unit_handle_t &adc_oneshot_unit_handle)
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT_OD;
    io_conf.pin_bit_mask = 1ULL << fan_power_pin;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_set_level(fan_power_pin, 1);
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << fan_speed_pin;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_set_level(fan_speed_pin, 0);
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    this->ledc_channel = ledc_channel;
    ledc_channel_config_t ledc_channel_fan = {};
    ledc_channel_fan.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel_fan.channel = this->ledc_channel;
    ledc_channel_fan.timer_sel = LEDC_TIMER_0;
    ledc_channel_fan.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_fan.gpio_num = fan_pwm_pin;
    ledc_channel_fan.duty = 0; // Set duty to 0%
    ledc_channel_fan.hpoint = 0;
    ledc_channel_fan.flags.output_invert = 1;
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_fan)); // FAN

    adc_channel_current.adc_channel = ADC_CHANNEL_0;
    adc_channel_current.adc_atten = ADC_ATTEN_DB_0;
    adc_channel_voltage.adc_channel = ADC_CHANNEL_1;
    adc_channel_voltage.adc_atten = ADC_ATTEN_DB_12;
    this->adc_oneshot_unit_handle = adc_oneshot_unit_handle;
    adc_oneshot_chan_cfg_t adc_chan_cfg = {};
    adc_chan_cfg.atten = ADC_ATTEN_DB_0;
    adc_chan_cfg.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_oneshot_unit_handle, adc_channel_current.adc_channel, &adc_chan_cfg));
    adc_chan_cfg.atten = ADC_ATTEN_DB_12;
    ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_oneshot_unit_handle, adc_channel_voltage.adc_channel, &adc_chan_cfg));
    adc_cali_curve_fitting_config_t cali_config = {};
    cali_config.unit_id = ADC_UNIT_1;
    cali_config.chan = adc_channel_current.adc_channel;
    cali_config.atten = ADC_ATTEN_DB_0;
    cali_config.bitwidth = ADC_BITWIDTH_12;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_channel_current.adc_cali_handle));
    cali_config.atten = ADC_ATTEN_DB_12;
    cali_config.chan = adc_channel_voltage.adc_channel;
    ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_channel_voltage.adc_cali_handle));
}
void fan::set_switch(bool sw)
{
    gpio_set_level(fan_power_pin, sw ? 0 : 1);
}
void fan::set_speed(float p)
{
    p = p > 100 ? (100) : (p > 0 ? p : 0);
    uint32_t uint_duty = p / 100 * 0b1111111111111;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, ledc_channel, uint_duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, ledc_channel));
}

float fan::read_fan_voltage()
{
    int raw;
    ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(adc_oneshot_unit_handle, adc_channel_voltage.adc_cali_handle, adc_channel_voltage.adc_channel, &raw));
    voltage = (float)raw / 2500 * 27.5f;
    return voltage;
}
float fan::read_fan_current()
{
    int raw;
    set_adc_range(adc_channel_current, ADC_ATTEN_DB_0);
    ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(adc_oneshot_unit_handle, adc_channel_current.adc_cali_handle, adc_channel_current.adc_channel, &raw));
    if (raw >= 730)
    {
        set_adc_range(adc_channel_current, ADC_ATTEN_DB_6);
        ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(adc_oneshot_unit_handle, adc_channel_current.adc_cali_handle, adc_channel_current.adc_channel, &raw));
        if (raw >= 1200)
        {
            set_adc_range(adc_channel_current, ADC_ATTEN_DB_12);
            ESP_ERROR_CHECK(adc_oneshot_get_calibrated_result(adc_oneshot_unit_handle, adc_channel_current.adc_cali_handle, adc_channel_current.adc_channel, &raw));
        }
    }
    current = (float)raw / 2500 * 2.02f;
    return current;
}
float fan::read_fan_power()
{
    power = read_fan_current() * read_fan_voltage();
    return power;
}