#pragma once
#include <functional>
#include <string>
#include <map>
#include <vector>
#include <string.h>
#include <stdio.h>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "driver/i2c_master.h"
#include "driver/uart.h"

class helper_err
{
public:
    typedef enum
    {
        success = ESP_OK,
        fail = ESP_FAIL,
        timeout_fail = ESP_ERR_TIMEOUT
    } code;
    static std::string to_string(code err)
    {
        switch (err)
        {
        case code::success:
            return "success";
        case code::fail:
            return "fail";
        case code::timeout_fail:
            return "timeout_fail";
        default:
            return "undefine code";
        }
    }
};

class thread_helper
{
private:
    TaskHandle_t pxCreatedTask = NULL;
    std::function<void(void *)> task_caller;
    void *task_param;
    static void caller(void *param)
    {
        ((thread_helper *)param)->task_caller(((thread_helper *)param)->task_param);
        vTaskDelete(NULL);
    }

public:
    thread_helper(std::function<void(void *)> caller, void *param = nullptr, std::string name = "", uint32_t stack_depth = 4096, uint8_t prio = 1)
    {
        task_caller = caller;
        task_param = param;
        xTaskCreate(this->caller, name.c_str(), stack_depth, this, prio, &pxCreatedTask);
        if (pxCreatedTask != NULL)
            vTaskSetThreadLocalStoragePointer(pxCreatedTask, 0, (void *)0);
    }
    thread_helper(std::function<void(void *)> caller, uint32_t stack_depth, void *param = nullptr, std::string name = "", uint8_t prio = 1)
    {
        task_caller = caller;
        task_param = param;
        xTaskCreate(this->caller, name.c_str(), stack_depth, this, prio, &pxCreatedTask);
        if (pxCreatedTask != NULL)
            vTaskSetThreadLocalStoragePointer(pxCreatedTask, 0, (void *)0);
    }
    ~thread_helper()
    {
        if (pxCreatedTask != NULL)
            vTaskSetThreadLocalStoragePointer(pxCreatedTask, 0, (void *)1);
    }
    static bool thread_is_exit()
    {
        void *value = pvTaskGetThreadLocalStoragePointer(NULL, 0);
        return (int)value == 1;
    }
    static void *malloc(size_t xSize)
    {
        return pvPortMalloc(xSize);
    }
    static void free(void *pointer)
    {
        vPortFree(pointer);
    }
    static TickType_t get_time_tick()
    {
        return xTaskGetTickCount();
    }
    static TickType_t get_time_ms()
    {
        return pdTICKS_TO_MS(xTaskGetTickCount());
    }
    static void sleep(uint32_t sleep_time_ms)
    {
        vTaskDelay(pdMS_TO_TICKS(sleep_time_ms));
    }
    static bool sleep_until(TickType_t &last_wake_tick, uint32_t sleep_time_ms)
    {
        return xTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(sleep_time_ms)) == pdTRUE;
    }
};

class thread_mutex_lock
{
private:
    SemaphoreHandle_t semaphore;

public:
    thread_mutex_lock(void)
    {
        semaphore = xSemaphoreCreateMutex();
    }
    ~thread_mutex_lock(void)
    {
        vSemaphoreDelete(semaphore);
    }
    bool lock(void)
    {
        return xSemaphoreTake(semaphore, portMAX_DELAY);
    }
    bool lock(uint32_t delay_ms)
    {
        return xSemaphoreTake(semaphore, pdMS_TO_TICKS(delay_ms));
    }
    void unlock(void)
    {
        xSemaphoreGive(semaphore);
    }
};

class timer_helper
{
private:
    std::string name;
    esp_timer_handle_t lvgl_tick_timer;

public:
    timer_helper(std::string name)
    {
        this->name = name;
    }
    static timer_helper *create_timer(std::string name, void (*callback)(void *arg))
    {
        timer_helper *helper = new timer_helper(name);
        esp_timer_create_args_t lvgl_tick_timer_args = {};
        lvgl_tick_timer_args.callback = callback;
        lvgl_tick_timer_args.name = helper->name.c_str();
        ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &helper->lvgl_tick_timer));
        return helper;
    }
    static void destroy_timer(timer_helper *helper)
    {
        ESP_ERROR_CHECK(esp_timer_delete(helper->lvgl_tick_timer));
        delete helper;
    }
    static void start_timer(timer_helper *helper, uint64_t period)
    {
        ESP_ERROR_CHECK(esp_timer_start_periodic(helper->lvgl_tick_timer, period));
    }
    static void start_timer_once(timer_helper *helper, uint64_t period)
    {
        ESP_ERROR_CHECK(esp_timer_start_once(helper->lvgl_tick_timer, period));
    }
    static void stop_timer(timer_helper *helper)
    {
        ESP_ERROR_CHECK(esp_timer_stop(helper->lvgl_tick_timer));
    }
};

class math_helper
{
public:
    static float limit_range(float num, float max, float min)
    {
        return num < min ? (min) : (num > max ? max : num);
    }
};