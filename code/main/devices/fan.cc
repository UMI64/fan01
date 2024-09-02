#include "fan.hpp"
fan::fan(adc_oneshot_unit_handle_t &adc_oneshot_unit_handle)
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

    ledc_channel_config_t ledc_channel_fan = {};
    ledc_channel_fan.speed_mode = LEDC_LOW_SPEED_MODE;
    ledc_channel_fan.channel = LEDC_CHANNEL_0;
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
fan::~fan()
{
    if (fan_power_switch)
    {
        gpio_set_level(fan_power_pin, 1);
        delete power_controller_thread;
    }
}
void fan::set_switch(bool sw)
{
    if (fan_power_switch != sw)
    {
        fan_power_switch = sw;
        if (fan_power_switch)
        {
            gpio_set_level(fan_power_pin, 0);
            set_duty_cycle(0);
            power_controller_thread = new thread_helper(std::bind(&fan::power_controller_task, this, std::placeholders::_1));
        }
        else
        {
            gpio_set_level(fan_power_pin, 1);
            delete power_controller_thread;
        }
    }
}
void fan::set_turn()
{
    set_switch(!fan_power_switch);
}
void fan::set_target_power(float w)
{
    target_power = w;
}
float fan::get_target_power()
{
    return target_power;
}
void fan::set_duty_cycle(float p)
{
    current_duty_cycle = math_helper::limit_range(p, 100, 0);
    uint32_t uint_duty = current_duty_cycle / 100 * 0b1111111111111;
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, uint_duty));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0));
}
float fan::get_duty_cycle(void)
{
    return current_duty_cycle;
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
    float current = 0;
    for (int i = 0; i < 20; i++)
        current += read_fan_current();
    power = current / 20 * read_fan_voltage();
    return power;
}

void fan::power_controller_task(void *param)
{
    int count = 0;
    float power_sum = 0;
    float p = 0.8, i = 0.04;
    float change_duty_cycle, diff_sum = 0;
    TickType_t last_wake_tick = thread_helper::get_time_tick();
    while (!thread_helper::thread_is_exit())
    {
        thread_helper::sleep_until(last_wake_tick, 10);
        power_sum += read_fan_power();
        if (count++ > 10)
        {
            current_power = power_sum / 10;
            float diff = diff * 0.6 + (target_power - current_power) * 0.4;
            diff_sum += diff * 0.8;
            diff_sum = math_helper::limit_range(diff_sum, 2, -2);
            change_duty_cycle = diff * p + diff_sum * i;
            set_duty_cycle(current_duty_cycle + change_duty_cycle);
            // ESP_LOGI("controller", "fan %0.4fV %0.4fA %0.4fW target:%0.1fW DC: %0.2f CDC: %0.2f diff: %0.2f diff_sum: %0.2f", voltage, current, current_power, target_power, current_duty_cycle, change_duty_cycle, diff, diff_sum);
            count = 0;
            power_sum = 0;
        }
    }
}