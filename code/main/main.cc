#include "controller.hpp"
extern "C" void app_main(void)
{
    esp_log_level_set(keyboard::tag(), ESP_LOG_MAX);
    // esp_log_level_set("adc_hal", ESP_LOG_MAX);
    // esp_log_level_set("adc_oneshot:", ESP_LOG_MAX);
    // esp_log_level_set("adc_cali", ESP_LOG_MAX);
    // esp_log_level_set("*",ESP_LOG_WARN);
    controller controller_obj;
    controller_obj.run();
}