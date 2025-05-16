#include "fan.hpp"
fan::fan(board * board_obj) : board_obj(board_obj)
{
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
        {
            ESP_LOGI(tag(), "set power 0");
            gpio_set_level(fan_power_pin, 0);
        }
        else
        {
            ESP_LOGI(tag(), "set power 1");
            gpio_set_level(fan_power_pin, 1);
        }
    }
}
bool fan::get_switch()
{
    return fan_power_switch;
}
void fan::set_turn()
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

float fan::read_voltage()
{
    int raw=board_obj->adc_channel0_value;
    return (float)raw / 2500 * 27.5f;
}
float fan::read_current()
{
    int raw=board_obj->adc_channel1_value;
    return (float)raw / 2500 * 2.02f;
}
float fan::read_power()
{
    uint8_t filter_count = 5;
    float _current = 0, _voltage = 0;
    for (int i = 0; i < filter_count; i++)
    {
        _current += read_current();
        _voltage += read_voltage();
    }
    current = _current / filter_count;
    voltage = _voltage / filter_count;
    power = voltage * current;
    filter_current = filter_current * 0.95f + current * 0.05f;
    filter_voltage = filter_voltage * 0.95f + voltage * 0.05f;
    filter_power = filter_power * 0.95f + power * 0.05f;
    return power;
}

uint32_t fan::get_speed()
{
    return speed_count.speed_rpm;
}
void fan::set_target_speed(uint32_t rpm)
{
    pid_obj.target_val = rpm;
}
uint32_t fan::get_target_speed()
{
    return pid_obj.target_val;
}

float fan::get_voltage(void) { return this->filter_voltage; }
float fan::get_current(void) { return this->filter_current; }
float fan::get_power(void) { return this->filter_power; }
void fan::main_task(void *param)
{
    auto do_power_pid = [&]() -> void
    {
        if (get_switch())
        {
            float change_duty_cycle = pid_obj.do_cal(get_speed());
            set_duty_cycle(current_duty_cycle + change_duty_cycle);
            // ESP_LOGI(tag(), "%urpm | %0.4fV %0.4fA %0.4fW | p:%0.4f i:%0.4f d:%0.4f | d %0.4f cd %0.4f",
            //          speed_count.speed_rpm,
            //          voltage, current, filter_power,
            //          pid_obj.p_vote, pid_obj.i_vote, pid_obj.d_vote,
            //          current_duty_cycle, change_duty_cycle);
        }
        else
        {
            pid_obj.reset();
            set_duty_cycle(0);
        }
    };
    auto do_count_speed = [&]() -> void
    {
        uint16_t pin_count = speed_count.pin_count;
        speed_count.pin_count = 0;
        speed_count.speed_rpm = (float)speed_count.speed_rpm * 0.75f + (float)pin_count * 1000 / main_task_cycle_ms / 2 * 60 * 0.25f;
        // ESP_LOGI(tag(), "pin %u\n", pin_count);
    };
    TickType_t last_wake_tick = thread_helper::get_time_tick();
    while (!thread_helper::thread_is_exit())
    {
        thread_helper::sleep_until(last_wake_tick, main_task_cycle_ms);
        read_power();
        do_count_speed();
        do_power_pid();
    }
}
void fan::speed_pin_isr_handler(void *arg)
{
    fan *fan_obj = (fan *)arg;
    fan_obj->speed_count.pin_count += 1;
}