#pragma once
#include "helper.hpp"
class fan
{
private:
    gpio_num_t fan_power_pin = GPIO_NUM_4;
    gpio_num_t fan_pwm_pin = GPIO_NUM_13;   // GPIO_NUM_21;
    gpio_num_t fan_speed_pin = GPIO_NUM_12; // GPIO_NUM_20;

    const uint8_t main_task_cycle_ms = 50;
    bool fan_power_switch = false;
    float current_duty_cycle = 0;
    thread_helper *main_thread;
    math_helper::pid pid_obj = math_helper::pid(0.0008f, 0.0f, 0.0005f, -0.5, 0.5);
    struct
    {
        std::atomic_int16_t pin_count = 0;
        uint16_t speed_rpm = 0;
    } speed_count;
    struct adc_channel_unit_t
    {
        adc_channel_t adc_channel;
        adc_cali_handle_t adc_cali_handle;
        adc_oneshot_chan_cfg_t adc_oneshot_chan_cfg;
    } adc_channel_current, adc_channel_voltage;
    adc_oneshot_unit_handle_t adc_oneshot_unit_handle;
    void set_duty_cycle(float p);
    void main_task(void *param);
    float get_duty_cycle();
    float read_voltage();
    float read_current();
    float read_power();
    static void IRAM_ATTR speed_pin_isr_handler(void *arg);

public:
    float voltage = 0, filter_voltage = 0;
    float current = 0, filter_current = 0;
    float power = 0, filter_power = 0;

    fan(adc_oneshot_unit_handle_t &adc_oneshot_unit_handle);
    ~fan();
    static const char *tag();
    void set_turn();
    void set_switch(bool sw);
    bool get_switch();

    void set_target_speed(uint32_t rpm);
    uint32_t get_target_speed();
    uint32_t get_speed();

    float get_voltage();
    float get_current();
    float get_power();
};