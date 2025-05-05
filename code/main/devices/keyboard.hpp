#pragma once
#include <helper.hpp>
class keyboard
{
public:
    typedef struct
    {
        uint32_t short_time = 200;
        uint32_t long_time = 700;
        uint32_t continue_time = 100;
    } action_param_t;

    typedef enum
    {
        none_press = -1,
        short_press = 0,
        long_press = 1,
        continue_press = 2,
    } action;
    typedef enum
    {
        press_key = 0,
        right_key = 1,
        left_key = 2,
    } keys;
    typedef std::function<void(keys key, uint32_t continue_ms, bool press, bool change)> caller_func;

private:
    const static gpio_num_t d_key_pin = GPIO_NUM_10;
    const static gpio_num_t b_key_pin = GPIO_NUM_7;
    const static gpio_num_t a_key_pin = GPIO_NUM_6;
    typedef enum
    {
        down = 0,
        up = 1,
    } key_statue;
    struct key_obj
    {
        keys key;
        gpio_num_t pin;
        uint8_t unstable = 0;
        key_statue statue = key_statue::up;
        TickType_t changed_tick = 0;
    } press_key_obj, right_key_obj, left_key_obj;
    thread_mutex_lock caller_lock;
    std::vector<caller_func> caller_list;
    thread_helper *loop_task_handle;
    void loop_task(void *param);

public:
    keyboard();
    ~keyboard();
    static const char *tag();
    static const char *key_to_name(keys key);
    void register_callback(caller_func caller);
    void unregister_callback(caller_func caller);

public:
    static keyboard::action event_action(action_param_t &action_param, uint32_t continue_ms);
};