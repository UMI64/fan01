#include "controller.hpp"
extern "C"
{
    void app_main(void)
    {
        esp_log_level_set("gpio", ESP_LOG_WARN);
        controller controller_obj;
        controller_obj.run();
    }
}