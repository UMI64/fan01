#include "board.hpp"
#include <esp_private/esp_gpio_reserve.h>
board::board()
{
    nvs_init();
    pwm_init();
    i2c_init();
    adc_init();
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
    fan_obj = new fan(adc_oneshot_unit_handle);
    lcd_obj = new lcd(i2c_master_bus_handle);
    keyboard_obj = new keyboard();
    husb238_obj = new husb238(i2c_master_bus_handle);
}
board::~board()
{
    delete fan_obj;
    delete husb238_obj;
    delete keyboard_obj;
    delete lcd_obj;
}