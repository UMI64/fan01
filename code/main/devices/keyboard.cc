#include "keyboard.hpp"
keyboard::keyboard()
{
    press_key_obj.key = press_key;
    press_key_obj.pin = a_key_pin;
    press_key_obj.short_tick = 100;
    press_key_obj.long_tick = 250;
    right_key_obj.key = right_key;
    right_key_obj.pin = d_key_pin;
    right_key_obj.short_tick = 100;
    right_key_obj.long_tick = 250;
    left_key_obj.key = left_key;
    left_key_obj.pin = b_key_pin;
    left_key_obj.short_tick = 100;
    left_key_obj.long_tick = 250;

    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_ANYEDGE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << a_key_pin) | (1ULL << d_key_pin) | (1ULL << b_key_pin);
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    ESP_ERROR_CHECK(gpio_isr_handler_add(press_key_obj.pin, keyboard::press_key_pin_isr_handler, this));
    ESP_ERROR_CHECK(gpio_isr_handler_add(right_key_obj.pin, keyboard::right_key_pin_isr_handler, this));
    ESP_ERROR_CHECK(gpio_isr_handler_add(left_key_obj.pin, keyboard::left_key_pin_isr_handler, this));

    message_queue = xQueueCreate(32, sizeof(key_changes));
    message_task_handle = new thread_helper(std::bind(&keyboard::message_task, this, std::placeholders::_1), 4096);
}
keyboard::~keyboard()
{
    delete message_task_handle;
    vQueueDelete(message_queue);
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
const char *keyboard::action_to_name(keyboard::actions action)
{
    switch (action)
    {
    case actions::short_press:
        return (const char *)"short";
    case actions::long_press:
        return (const char *)"long";
    case actions::continue_press:
        return (const char *)"continue";
    default:
        return (const char *)"null";
    }
}
void keyboard::message_task(void *param)
{
    TickType_t now_tick;
    while (true)
    {
        key_changes changed_key_changes;
        if (xQueueReceive(message_queue, &changed_key_changes, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            // ESP_LOGI("keyboard", "keyboard recv key message %d %d", changed_key_changes.pin, changed_key_changes.level);
            auto handle_key_changes = [&](struct key_obj &key_obj) -> void
            {
                if (key_obj.wait == key_wait::null)
                {
                    if (changed_key_changes.level == 0)
                    {
                        key_obj.wait = key_wait::down;
                        key_obj.changed_tick = now_tick;
                        // ESP_LOGI("keyboard", "pin %d wait down", changed_key_changes.pin);
                    }
                }
                else if (key_obj.wait == key_wait::down)
                {
                    if (changed_key_changes.level == 0)
                    {
                        key_obj.changed_tick = now_tick;
                        // ESP_LOGI("keyboard", "pin %d update down tick", changed_key_changes.pin);
                    }
                }
                else if (key_obj.wait == key_wait::up && changed_key_changes.level)
                {
                    auto ms = thread_helper::tick_to_ms(now_tick - key_obj.changed_tick);
                    if (ms > key_obj.short_tick)
                    {
                        if (ms < key_obj.continue_tick)
                        { // ESP_LOGI("keyboard", "pin %d up %lums", changed_key_changes.pin, ms);
                            key_obj.report_tick = now_tick;
                            actions action = ms > key_obj.long_tick ? actions::long_press : actions::short_press;
                            for (auto caller : caller_list)
                                caller(key_obj.key, action);
                        }
                        key_obj.wait = key_wait::null;
                    }
                }
            };
            now_tick = thread_helper::get_time_tick();
            switch (changed_key_changes.pin)
            {
            case a_key_pin:
                handle_key_changes(press_key_obj);
                break;
            case b_key_pin:
                handle_key_changes(left_key_obj);
                break;
            case d_key_pin:
                handle_key_changes(right_key_obj);
                break;
            default:
                break;
            }
        }
        else
        {
            auto handle_key_periodicity = [&](struct key_obj &key_obj) -> void
            {
                switch (key_obj.wait)
                {
                case key_wait::up:
                {
                    if (gpio_get_level(key_obj.pin))
                    {
                        key_obj.wait = key_wait::null;
                        // ESP_LOGI("keyboard", "pin %d wait up to null", key_obj.pin);
                    }
                    else if (thread_helper::tick_to_ms(now_tick - key_obj.changed_tick) > key_obj.continue_tick)
                    {
                        if (thread_helper::tick_to_ms(now_tick - key_obj.report_tick) > 100)
                        {
                            key_obj.report_tick = now_tick;
                            for (auto caller : caller_list)
                                caller(key_obj.key, actions::continue_press);
                        }
                    }
                }
                break;
                case key_wait::down:
                {
                    if (thread_helper::tick_to_ms(now_tick - key_obj.changed_tick) > 20)
                    {
                        if (gpio_get_level(key_obj.pin) == 0)
                        {
                            key_obj.wait = key_wait::up;
                            // ESP_LOGI("keyboard", "pin %d wait down to up", key_obj.pin);
                        }
                        else
                        {
                            key_obj.wait = key_wait::null;
                            // ESP_LOGI("keyboard", "pin %d wait down to null", key_obj.pin);
                        }
                    }
                }
                break;
                default:
                    break;
                }
            };
            now_tick = thread_helper::get_time_tick();
            handle_key_periodicity(press_key_obj);
            handle_key_periodicity(right_key_obj);
            handle_key_periodicity(left_key_obj);
        }
    }
}
void keyboard::register_callback(std::function<void(keyboard::keys key, keyboard::actions action)> caller)
{
    caller_list.push_back(caller);
}
void keyboard::unregister_callback(std::function<void(keyboard::keys key, keyboard::actions action)> caller)
{
    for (auto it = caller_list.begin(); it != caller_list.end(); it++)
    {
        if ((*it).target<void(void)>() == caller.target<void(void)>())
            it = caller_list.erase(it);
    }
}
void keyboard::press_key_pin_isr_handler(void *arg)
{
    keyboard *keyboard_obj = (keyboard *)arg;
    if (keyboard_obj->press_key_obj.level != gpio_get_level(keyboard_obj->press_key_obj.pin))
    {
        key_changes key_change = {
            !keyboard_obj->press_key_obj.level,
            keyboard_obj->press_key_obj.pin,
        };
        keyboard_obj->press_key_obj.level = !keyboard_obj->press_key_obj.level;
        xQueueSendFromISR(keyboard_obj->message_queue, &key_change, NULL);
    }
}
void keyboard::right_key_pin_isr_handler(void *arg)
{
    keyboard *keyboard_obj = (keyboard *)arg;
    if (keyboard_obj->right_key_obj.level != gpio_get_level(keyboard_obj->right_key_obj.pin))
    {
        key_changes key_change = {
            !keyboard_obj->right_key_obj.level,
            keyboard_obj->right_key_obj.pin,
        };
        keyboard_obj->right_key_obj.level = !keyboard_obj->right_key_obj.level;
        xQueueSendFromISR(keyboard_obj->message_queue, &key_change, NULL);
    }
}
void keyboard::left_key_pin_isr_handler(void *arg)
{
    keyboard *keyboard_obj = (keyboard *)arg;
    if (keyboard_obj->left_key_obj.level != gpio_get_level(keyboard_obj->left_key_obj.pin))
    {
        key_changes key_change = {
            !keyboard_obj->left_key_obj.level,
            keyboard_obj->left_key_obj.pin,
        };
        keyboard_obj->left_key_obj.level = !keyboard_obj->left_key_obj.level;
        xQueueSendFromISR(keyboard_obj->message_queue, &key_change, NULL);
    }
}
