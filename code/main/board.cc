#include "board.hpp"
board::board()
{
    nvs_init();
    pwm_init();
    i2c_init();
    adc_init();
    fan_obj = new fan(adc_oneshot_unit_handle);
    husb238_obj = new husb238(i2c_master_bus_handle);
    keyboard_obj = new keyboard();
}
board::~board()
{
    delete fan_obj;
    delete husb238_obj;
}