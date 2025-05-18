#include "ui.hpp"
ui::ui(controller *controller_obj) : controller_obj(controller_obj)
{
    u8g2_SetUserPtr(&u8g2, this);
    u8g2_Setup_ssd1306_i2c_128x32_univision_f(&u8g2, U8G2_R0, byte_cb, gpio_and_delay_cb);
    // 清空屏幕内存后开启显示
    u8g2_InitDisplay(&u8g2); // 根据所选的芯片进行初始化工作，初始化完成后，显示器处于关闭状态
    u8g2_ClearBuffer(&u8g2);
    u8g2_SendBuffer(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0); // 打开显示器
    ui_window_obj = new ui_base(&u8g2, 0, 0, 128, 32);
    main_page_obj = new main_page(this);
    menu_page_obj = new menu_page(this);
    ui_window_obj->append_component(main_page_obj->base_obj);
    ui_window_obj->append_component(menu_page_obj->base_obj);
    ui_window_obj->set_focus(main_page_obj->base_obj);

    controller_obj->keyboard_obj->register_callback(std::bind(&ui::keyboard_callback, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    render_thread = new thread_helper(std::bind(&ui::render_task, this, std::placeholders::_1), 4096, "ui:render_task");
    manage_thread = new thread_helper(std::bind(&ui::manage_task, this, std::placeholders::_1), 4096, "ui:manage_task");
}
void ui::render_task(void *param)
{
    while (!thread_helper::thread_is_exit())
    {
        {
            thread_mutex_lock_guard lock(render_lock);
            ui_base::forward_render(nullptr, ui_window_obj);
            u8g2_SendBuffer(&u8g2);
            u8g2_ClearBuffer(&u8g2);
        }
        thread_helper::sleep(50); // 每隔50ms执行一次渲染和刷新
    }
}
void ui::manage_task(void *param)
{
    while (!thread_helper::thread_is_exit())
    {
        {
            thread_mutex_lock_guard lock(render_lock);
            main_page_obj->ui_voltage_obj->set_ascii_str("U: %.2fV", controller_obj->fan_obj->get_voltage());
            main_page_obj->ui_current_obj->set_ascii_str("I: %.2fA", controller_obj->fan_obj->get_current());
            main_page_obj->ui_power_obj->set_ascii_str("P: %.2fW", controller_obj->fan_obj->get_power());
            main_page_obj->ui_target_speed_obj->set_ascii_str("TS: %uRPM", controller_obj->fan_obj->get_target_speed());
            main_page_obj->ui_speed_obj->set_ascii_str("RS: %uRPM", controller_obj->fan_obj->get_speed());
        }
        thread_helper::sleep(200);
    }
}
void ui::keyboard_callback(keyboard::keys key, uint32_t continue_ms, bool press, bool change)
{
    thread_mutex_lock_guard lock(render_lock);
    ui_base::forward_keyevent(ui_window_obj, (uint32_t)key, continue_ms, press, change);
}

main_page::main_page(ui *ui_obj) : ui_obj(ui_obj)
{
    base_obj = new ui_base(&ui_obj->u8g2, 0, 0, 128, 32);
    base_obj->set_keyevent_cb(std::bind(&main_page::key_event_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    ui_voltage_obj = (ui_text *)base_obj->append_component(new ui_text(&ui_obj->u8g2, 5, 0 + 1, "U: -V"));
    ui_current_obj = (ui_text *)base_obj->append_component(new ui_text(&ui_obj->u8g2, 5, 0 + 1 + 10, "I: -A"));
    ui_power_obj = (ui_text *)base_obj->append_component(new ui_text(&ui_obj->u8g2, 5, 0 + 1 + 10 + 10, "P: -/-W"));
    ui_target_speed_obj = (ui_text *)base_obj->append_component(new ui_text(&ui_obj->u8g2, 63, 0 + 1, "TS: -RPM"));
    ui_speed_obj = (ui_text *)base_obj->append_component(new ui_text(&ui_obj->u8g2, 63, 0 + 1 + 10, "RS: -RPM"));
}
bool main_page::key_event_cb(uint32_t key, uint32_t key_continue_ms, bool press, bool change)
{
    fan *fan_obj = ui_obj->controller_obj->fan_obj;
    auto change_fan_speed = [&](int change_speed)
    {
        fan_obj->set_target_speed(math_helper::limit_range((float)fan_obj->get_target_speed() + change_speed, 8000, 0));
    };
    switch (key)
    {
    case keyboard::press_key:
    {
        keyboard::action_param_t action_param = {300, 800, 100};
        if (change && !press)
        {
            keyboard::action res = keyboard::event_action(action_param, key_continue_ms);
            if (res == keyboard::short_press) // 短按 切换页面
            {
                ui_obj->main_page_obj->base_obj->set_postion(0, -32);
                ui_obj->menu_page_obj->base_obj->set_postion(0, 0);
                ui_obj->ui_window_obj->set_focus(ui_obj->menu_page_obj->base_obj);
            }
            else // 长按 开关风扇
                fan_obj->set_turn();
        }
        break;
    }
    case keyboard::left_key:
    {
        keyboard::action_param_t action_param = {350, 700, 100};
        if (change && !press)
        {
            keyboard::action res = keyboard::event_action(action_param, key_continue_ms);
            if (res == keyboard::short_press) // 短按
                change_fan_speed(-100);
            else if (res == keyboard::long_press) // 长按
                change_fan_speed(-1000);
        }
        else if (press)
        {
            keyboard::action res = keyboard::event_action(action_param, key_continue_ms);
            if (res == keyboard::continue_press) // 连续
                change_fan_speed(-200);
        }
        break;
    }
    case keyboard::right_key:
    {
        keyboard::action_param_t action_param = {350, 700, 100};
        if (change && !press)
        {
            keyboard::action res = keyboard::event_action(action_param, key_continue_ms);
            if (res == keyboard::short_press) // 短按
                change_fan_speed(100);
            else if (res == keyboard::long_press) // 长按
                change_fan_speed(1000);
        }
        else if (press)
        {
            keyboard::action res = keyboard::event_action(action_param, key_continue_ms);
            if (res == keyboard::continue_press) // 连续
                change_fan_speed(200);
        }
        break;
    }
    }
    return false;
}

menu_page::menu_page(ui *ui_obj) : ui_obj(ui_obj)
{
    base_obj = new ui_base(&ui_obj->u8g2, 0, -32, 128, 32);
    base_obj->set_keyevent_cb(std::bind(&menu_page::key_event_cb, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3, std::placeholders::_4));
    test_button_obj = (ui_button *)base_obj->append_component(new ui_button(&ui_obj->u8g2, 5, 14, "config"));
}
bool menu_page::key_event_cb(uint32_t key, uint32_t key_continue_ms, bool press, bool change)
{
    switch (key)
    {
    case keyboard::press_key:
    {
        keyboard::action_param_t action_param = {300, 800, 100};
        if (change && !press)
        {
            keyboard::action res = keyboard::event_action(action_param, key_continue_ms);
            if (res == keyboard::short_press) // 短按 切换页面
            {
                ui_obj->main_page_obj->base_obj->set_postion(0, 0);
                ui_obj->menu_page_obj->base_obj->set_postion(0, -32);
                ui_obj->ui_window_obj->set_focus(ui_obj->main_page_obj->base_obj);
            }
        }
        break;
    }
    }
    return false;
}
