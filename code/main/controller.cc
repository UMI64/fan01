#include "controller.hpp"
#include "board.hpp"
controller::controller()
{
    board_obj = new board();
    main_thread = new thread_helper(std::bind(&controller::main_task, this, std::placeholders::_1));
    fan_power_controller_thread = new thread_helper(std::bind(&controller::fan_power_controller_task, this, std::placeholders::_1));
}
controller::~controller()
{
    delete fan_power_controller_thread;
    delete main_thread;
    delete board_obj;
}
void controller::main_task(void *param)
{
    while (!thread_helper::thread_is_exit())
        thread_helper::sleep(50);
}
void controller::fan_power_controller_task(void *param)
{
    int count = 0;
    float power_sum = 0;
    TickType_t last_wake_tick = thread_helper::get_time_tick();
    while (!thread_helper::thread_is_exit())
    {
        thread_helper::sleep_until(last_wake_tick, 10);
        power_sum += board_obj->fan_obj->read_fan_power();
        if (count++ > 20)
        {
            ESP_LOGI("controller", "fan %0.4fV %0.4fA %0.4fW", board_obj->fan_obj->voltage, board_obj->fan_obj->current, power_sum / 20);
            count = 0;
            power_sum = 0;
        }
    }
}
void controller::run()
{
    for (auto src_pdo_voltage : husb238::support_voltages)
    {
        float current = board_obj->husb238_obj->read_pdo_cap(src_pdo_voltage);
        if (current != 0)
            ESP_LOGI("controller", "available PD %uV %0.2fA", husb238::src_pdo_voltage_to_float(src_pdo_voltage), current);
    }
    board_obj->husb238_obj->set_pdo(husb238::src_pdo_voltage::src_pdo_20v);
    board_obj->husb238_obj->req_pdo();
    board_obj->fan_obj->set_switch(true);
    while (1)
    {
        board_obj->fan_obj->set_speed(0);
        vTaskDelay(pdMS_TO_TICKS(5000));
        board_obj->fan_obj->set_speed(40);
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}