#include "controller.hpp"
#include "ui.hpp"
#undef TAG
#define TAG "controller"
controller::controller()
{
    board_obj = new board();
    fan_obj = new fan(board_obj);
    lcd_obj = new lcd(board_obj->i2c_master_bus_handle);
    husb238_obj = new husb238(board_obj->i2c_master_bus_handle);
    keyboard_obj = new keyboard();
    ui_obj = new ui(board_obj);
    main_thread = new thread_helper(std::bind(&controller::main_task, this, std::placeholders::_1), "controller:main_task");
}
controller::~controller()
{
    delete main_thread;
    delete fan_obj;
    delete husb238_obj;
    delete keyboard_obj;
    delete lcd_obj;
    delete board_obj;
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
        float current = husb238_obj->read_pdo_cap(src_pdo_voltage);
        if (current != 0)
            ESP_LOGI(TAG, "available PD %uV %0.2fA", husb238::src_pdo_voltage_to_float(src_pdo_voltage), current);
    }
    husb238_obj->set_pdo(husb238::src_pdo_voltage::src_pdo_12v);
    husb238_obj->req_pdo();
    fan_obj->set_turn();
    while (1)
        vTaskDelay(pdMS_TO_TICKS(30000));
}