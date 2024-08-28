#pragma once
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_mac.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"

#include "lwip/err.h"
#include "lwip/sys.h"
class leida_wifi
{
private:
    void init_softAP();
    void init_station();
    void scan_AP();
public:
    leida_wifi(/* args */);
    ~leida_wifi();
};


