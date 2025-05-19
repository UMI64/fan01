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
    main_thread = new thread_helper(std::bind(&controller::main_task, this, std::placeholders::_1), TAG ":main_task");
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
    uint8_t pbo_voltage = husb238::src_pdo_voltage::src_pdo_12v;
    uint8_t fan_speed_use_storeg = false;
    uint16_t fan_speed = 0;
    board_obj->nvs_handle->get_item("pbo_voltage", pbo_voltage);
    board_obj->nvs_handle->get_item("fan_speed_use_storeg", fan_speed_use_storeg);
    if (fan_speed_use_storeg)
        board_obj->nvs_handle->get_item("fan_speed", fan_speed);
    for (auto src_pdo_voltage : husb238::support_voltages)
    {
        float current = husb238_obj->read_pdo_cap(src_pdo_voltage);
        if (current != 0)
            ESP_LOGI(TAG, "available PD %uV %0.2fA", husb238::src_pdo_voltage_to_float(src_pdo_voltage), current);
    }
    husb238_obj->set_pdo((husb238::src_pdo_voltage)pbo_voltage);
    husb238_obj->req_pdo();
    fan_obj->set_switch(true);
    fan_obj->set_target_speed(fan_speed);
    ui ui(this);
    while (1)
    {
        TickType_t last_wake_tick = thread_helper::get_time_tick();
        thread_helper::sleep_until(last_wake_tick, 100);
    }
}