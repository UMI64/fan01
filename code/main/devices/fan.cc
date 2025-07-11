#include "fan.hpp"
fan::fan(board *board_obj) : board_obj(board_obj)
{
    speed_count.t_timestamp = esp_timer_get_time();
    speed_count.m_timestamp = speed_count.t_timestamp;
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT_OD;
    io_conf.pin_bit_mask = BIT64(fan_power_pin);
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_set_level(fan_power_pin, 1);
    gpio_reset_pin(fan_power_pin);
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    io_conf.intr_type = GPIO_INTR_POSEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(fan_speed_pin);
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_set_level(fan_speed_pin, 0);
    ESP_ERROR_CHECK(gpio_config(&io_conf));
    ESP_ERROR_CHECK(gpio_isr_handler_add(fan_speed_pin, fan::speed_pin_isr_handler, this));
    {
        ledc_channel_config_t ledc_channel_fan = {};
        gpio_reset_pin(fan_pwm_pin);
        ledc_channel_fan.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_channel_fan.channel = LEDC_CHANNEL_0;
        ledc_channel_fan.timer_sel = LEDC_TIMER_0;
        ledc_channel_fan.intr_type = LEDC_INTR_DISABLE;
        ledc_channel_fan.gpio_num = fan_pwm_pin;
        ledc_channel_fan.duty = 0; // Set duty to 0%
        ledc_channel_fan.hpoint = 0;
        ledc_channel_fan.flags.output_invert = 1;
        ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel_fan)); // FAN
    }
    gpio_set_level(fan_power_pin, 0);
    main_thread = new thread_helper(std::bind(&fan::main_task, this, std::placeholders::_1), "fan:main_task");
}
fan::~fan()
{
    gpio_set_level(fan_power_pin, 1);
    delete main_thread;
}
const char *fan::tag()
{
    return (const char *)"fan";
}
void fan::set_switch(bool sw)
{
    if (fan_power_switch != sw)
    {
        fan_power_switch = sw;
        if (fan_power_switch)
            ESP_LOGI(tag(), "set power on");
        else
            ESP_LOGI(tag(), "set power off");
    }
}
bool fan::get_switch()
{
    return fan_power_switch;
}
void fan::turn_switch()
{
    set_switch(!fan_power_switch);
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

int fan::read_voltage()
{
    int raw = board_obj->read_voltage(ADC_CHANNEL_3);
    return raw * 27500 / 2500;
}
int fan::read_current()
{
    int raw = board_obj->read_voltage(ADC_CHANNEL_0);
    return raw * 2020 / 2500;
}
int fan::read_power()
{
    current = read_current();
    voltage = read_voltage();
    power = voltage * current / 1000;
    filter_current = (filter_current * 3 + current) / 4;
    filter_voltage = (filter_voltage * 3 + voltage) / 4;
    filter_power = filter_current * filter_voltage / 1000;
    return power;
}

uint16_t fan::get_speed()
{
    return speed_count.t_speed_rpm;
}
void fan::set_target_speed(uint16_t rpm)
{
    pid_obj.target_val = rpm;
}
uint16_t fan::get_target_speed()
{
    return pid_obj.target_val;
}

float fan::get_voltage(void) { return (float)this->filter_voltage / 1000; }
float fan::get_current(void) { return (float)this->filter_current / 1000; }
float fan::get_power(void) { return (float)this->filter_power / 1000; }
void fan::main_task(void *param)
{
    auto do_power_pid = [&]() -> void
    {
        if (get_switch())
        {
            set_duty_cycle(current_duty_cycle + pid_obj.do_cal(get_speed()));
        }
        else
        {
            pid_obj.reset();
            set_duty_cycle(0);
        }
    };
    TickType_t last_wake_tick = thread_helper::get_time_tick();
    while (!thread_helper::thread_is_exit())
    {
        thread_helper::sleep_until(last_wake_tick, main_task_cycle_ms);
        read_power();
        do_power_pid();
    }
}
void fan::speed_pin_isr_handler(void *arg)
{
    fan *fan_obj = (fan *)arg;
    int64_t time_now = esp_timer_get_time();
    int64_t time_diff = time_now - fan_obj->speed_count.t_timestamp;
    fan_obj->speed_count.t_speed_rpm = 0.5f * ((float)(60 * 1000 * 1000) / time_diff);
    fan_obj->speed_count.pin_count += 1;
    fan_obj->speed_count.t_timestamp = time_now;
    if (time_now >= fan_obj->speed_count.m_timestamp)
    {
        fan_obj->speed_count.m_speed_rpm = (float)fan_obj->speed_count.m_speed_rpm * 0.75f + (float)fan_obj->speed_count.pin_count * 1000 / fan_obj->main_task_cycle_ms / 2 * 60 * 0.25f;
        fan_obj->speed_count.m_timestamp = time_now + 1000 * fan_obj->main_task_cycle_ms;
        fan_obj->speed_count.pin_count = 0;
    }
}