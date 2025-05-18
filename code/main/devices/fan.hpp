#pragma once
#include "helper.hpp"
#include "board.hpp"
class fan
{
private:
    board * board_obj;
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
    void set_duty_cycle(float p);
    void main_task(void *param);
    float get_duty_cycle();
    int read_voltage();
    int read_current();
    int read_power();
    static void IRAM_ATTR speed_pin_isr_handler(void *arg);

public:
    int voltage = 0, filter_voltage = 0;
    int current = 0, filter_current = 0;
    int power = 0, filter_power = 0;

    fan(board * board_obj);
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