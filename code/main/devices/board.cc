#include "board.hpp"
#include <esp_private/esp_gpio_reserve.h>
board::board()
{
    nvs_init();
    pwm_init();
    i2c_init();
    adc_init();
    ESP_ERROR_CHECK(gpio_install_isr_service(0));
}
board::~board()
{
}