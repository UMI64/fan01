#include "keyboard.hpp"
keyboard::keyboard()
{
    // 70ms, 350ms, 700ms, 200ms
    auto set_key_obj = [&](keyboard::key_obj &key_obj, keyboard::keys key, gpio_num_t pin) -> void
    {
        key_obj.key = key;
        key_obj.pin = pin;
    };
    set_key_obj(press_key_obj, press_key, a_key_pin);
    set_key_obj(right_key_obj, right_key, d_key_pin);
    set_key_obj(left_key_obj, left_key, b_key_pin);
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(a_key_pin) | BIT64(d_key_pin) | BIT64(b_key_pin);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    loop_task_handle = new thread_helper(std::bind(&keyboard::loop_task, this, std::placeholders::_1), 4096, "keyboard:loop_task");
}
keyboard::~keyboard()
{
    delete loop_task_handle;
}
const char *keyboard::tag()
{
    return (const char *)"keyboard";
}
const char *keyboard::key_to_name(keyboard::keys key)
{
    switch (key)
    {
    case keys::press_key:
        return (const char *)"press";
    case keys::right_key:
        return (const char *)"right";
    case keys::left_key:
        return (const char *)"left";
    default:
        return (const char *)"null";
    }
}
void keyboard::loop_task(void *param)
{
    auto init_key = [&](struct key_obj &key_obj) -> void
    {
        TickType_t now_tick = thread_helper::get_time_tick();
        int new_level = gpio_get_level(key_obj.pin);
        key_obj.statue = new_level == 0 ? key_statue::down : key_statue::up;
        key_obj.changed_tick = now_tick;
        key_obj.unstable = 0;
    };
    auto fetch_key = [&](struct key_obj &key_obj) -> void
    {
        bool change = false;
        TickType_t now_tick = thread_helper::get_time_tick();
        uint32_t continue_ms = thread_helper::tick_to_ms(now_tick - key_obj.changed_tick);
        int new_level = gpio_get_level(key_obj.pin);
        key_obj.unstable += (new_level != key_obj.statue) ? 3 : (key_obj.unstable >= 1 ? -1 : 0); // 电平不对不稳定度加3，电平一致则减1
        if (key_obj.unstable >= 8)
        {
            key_obj.statue = new_level == 0 ? key_statue::down : key_statue::up;
            key_obj.changed_tick = now_tick;
            key_obj.unstable = 0;
            change = true;
        }
        for (auto caller : caller_list)
            caller(key_obj.key, continue_ms, key_obj.statue == key_statue::down, change);
    };
    init_key(press_key_obj);
    init_key(right_key_obj);
    init_key(left_key_obj);
    while (!thread_helper::thread_is_exit())
    {
        fetch_key(press_key_obj);
        fetch_key(right_key_obj);
        fetch_key(left_key_obj);
        thread_helper::sleep(10);
    }
}
void keyboard::register_callback(caller_func caller)
{
    thread_mutex_lock_guard lock(caller_lock);
    caller_list.push_back(caller);
}
void keyboard::unregister_callback(caller_func caller)
{
    thread_mutex_lock_guard lock(caller_lock);
    for (auto it = caller_list.begin(); it != caller_list.end(); it++)
    {
        if ((*it).target<void(void)>() == caller.target<void(void)>())
            it = caller_list.erase(it);
    }
}
keyboard::action keyboard::event_action(action_param_t &action_param, uint32_t continue_ms)
{
    if (continue_ms < action_param.short_time) // 短按
        return action::short_press;
    else if (continue_ms < action_param.long_time) // 长按
        return action::long_press;
    else if (((continue_ms - action_param.long_time) % action_param.continue_time) == 0)
        return action::continue_press;
    return action::none_press;
}