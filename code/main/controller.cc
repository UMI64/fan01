#include "controller.hpp"
#include "board.hpp"
controller::controller()
{
    board_obj = new board();
    main_thread = new thread_helper(std::bind(&controller::main_task, this, std::placeholders::_1));
}
controller::~controller()
{
    delete main_thread;
    delete board_obj;
}
void controller::keyboard_callback(keyboard::keys key, keyboard::actions action)
{
    switch (key)
    {
    case keyboard::keys::press_key:
    {
        if (action == keyboard::actions::long_press)
            board_obj->fan_obj->set_target_power(0);
    }
    break;
    case keyboard::keys::left_key:
    {
        if (action == keyboard::actions::long_press)
            board_obj->fan_obj->set_target_power(math_helper::limit_range(board_obj->fan_obj->get_target_power() - 1, 6.5, 0));
        else
            board_obj->fan_obj->set_target_power(math_helper::limit_range(board_obj->fan_obj->get_target_power() - 0.1, 6.5, 0));
    }
    break;
    case keyboard::keys::right_key:
    {
        if (action == keyboard::actions::long_press)
            board_obj->fan_obj->set_target_power(math_helper::limit_range(board_obj->fan_obj->get_target_power() + 1, 6.5, 0));
        else
            board_obj->fan_obj->set_target_power(math_helper::limit_range(board_obj->fan_obj->get_target_power() + 0.1, 6.5, 0));
    }
    break;
    default:
        break;
    }
    // ESP_LOGI("controller", "keyboard key %s action %s", keyboard::key_to_name(key), keyboard::action_to_name(action));
}

void controller::main_task(void *param)
{
    while (!thread_helper::thread_is_exit())
        thread_helper::sleep(50);
}
void controller::run()
{
    for (auto src_pdo_voltage : husb238::support_voltages)
    {
        float current = board_obj->husb238_obj->read_pdo_cap(src_pdo_voltage);
        if (current != 0)
            ESP_LOGI("controller", "available PD %uV %0.2fA", husb238::src_pdo_voltage_to_float(src_pdo_voltage), current);
    }
    board_obj->keyboard_obj->register_callback(std::bind(&controller::keyboard_callback, this, std::placeholders::_1, std::placeholders::_2));
    board_obj->husb238_obj->set_pdo(husb238::src_pdo_voltage::src_pdo_20v);
    board_obj->husb238_obj->req_pdo();
    board_obj->fan_obj->set_turn();
    while (1)
        vTaskDelay(pdMS_TO_TICKS(30000));
}