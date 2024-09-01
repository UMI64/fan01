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
#include "driver/gptimer.h"

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
/*
class timer_call
{
private:
    typedef struct timer_callback_item_t
    {
        uint64_t time_to_wait_us;
        uint64_t time_to_exec_us;
        bool recall;
        void *param;
        timer_call::timer_callback cb;
    } timer_callback_item_t;
    std::vector<timer_callback_item_t *> callback_list;
    gptimer_handle_t gptimer;
    SemaphoreHandle_t timer_semaphore;
    uint64_t all_count = 0;
    static bool callback_list_compare(timer_callback_item_t *v1, timer_callback_item_t *v2)
    {
        return v1->time_to_exec_us > v2->time_to_exec_us;
    }
    static bool gptimer_alarm_cb(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
    {
        timer_call *timer_call_obj = (timer_call *)user_ctx;
        xSemaphoreTakeFromISR(timer_call_obj->timer_semaphore, NULL);
        gptimer_stop(timer);
        uint64_t tick = timer_call_obj->tick_count();
        timer_call_obj->all_count += tick;
        for (auto it = timer_call_obj->callback_list.begin(); it != timer_call_obj->callback_list.end();)
        {
            timer_callback_item_t *p_item = *it;
            if (timer_call_obj->all_count >= p_item->time_to_exec_us)
            {
                p_item->cb(p_item->param);
                if (p_item->recall)
                {
                    p_item->time_to_exec_us = timer_call_obj->all_count + p_item->time_to_wait_us;
                    it++;
                }
                else
                    it = timer_call_obj->callback_list.erase(it);
            }
            else
            {
                it++;
            }
        }
        timer_call_obj->reconfig_timer(0);
        xSemaphoreGiveFromISR(timer_call_obj->timer_semaphore, NULL);
        gptimer_start(timer);
        return true;
    }
    void reconfig_timer(uint64_t start_count)
    {
        uint64_t alarm_count = 1000000; // 1s
        std::sort(callback_list.begin(), callback_list.end(), callback_list_compare);
        if (!callback_list.empty())
        {
            timer_callback_item_t *item = callback_list.front();
            alarm_count = item->time_to_exec_us - all_count;
        }
        gptimer_alarm_config_t alarm_config = {};
        alarm_config.alarm_count = alarm_count;
        gptimer_set_raw_count(gptimer, start_count);
        gptimer_set_alarm_action(gptimer, &alarm_config);
    }

public:
    typedef std::function<void(void *)> timer_callback;
    typedef void *timer_call_item;
    timer_call()
    {
        timer_semaphore = xSemaphoreCreateMutex();

        gptimer_config_t timer_config = {};
        timer_config.clk_src = GPTIMER_CLK_SRC_DEFAULT;
        timer_config.direction = GPTIMER_COUNT_UP;
        timer_config.resolution_hz = 1 * 1000 * 1000; // 1MHz, 1 tick = 1us
        ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));

        gptimer_alarm_config_t alarm_config = {};
        alarm_config.reload_count = 0;
        alarm_config.alarm_count = 1000000;
        alarm_config.flags.auto_reload_on_alarm = true;
        ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));

        gptimer_event_callbacks_t cbs = {};
        cbs.on_alarm = gptimer_alarm_cb;
        ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, this));
        ESP_ERROR_CHECK(gptimer_enable(gptimer));
        ESP_ERROR_CHECK(gptimer_start(gptimer));
    }
    ~timer_call()
    {
        vSemaphoreDelete(timer_semaphore);
    }
    uint64_t tick_count()
    {
        uint64_t tick_num;
        gptimer_get_raw_count(gptimer, &tick_num);
        return tick_num;
    }
    timer_call_item add_timer_callback(timer_callback cb, uint64_t time_to_wait_us, bool recall, void *param)
    {
        xSemaphoreTake(timer_semaphore, portMAX_DELAY);
        gptimer_stop(gptimer);
        uint64_t tick = tick_count();
        all_count += tick;
        timer_callback_item_t *item = (timer_callback_item_t *)pvPortMalloc(sizeof(timer_callback_item_t));
        item->cb = cb;
        item->param = param;
        item->recall = recall;
        item->time_to_wait_us = time_to_wait_us;
        item->time_to_exec_us = all_count + time_to_wait_us;
        callback_list.emplace_back(item);
        reconfig_timer(0);
        xSemaphoreGive(timer_semaphore);
        gptimer_start(gptimer);
        return item;
    }
    void del_timer_callback(timer_call_item p_item)
    {
        xSemaphoreTake(timer_semaphore, portMAX_DELAY);
        gptimer_stop(gptimer);
        uint64_t tick = tick_count();
        all_count += tick;
        for (auto it = callback_list.begin(); it != callback_list.end();)
        {
            timer_callback_item_t *_p_item = *it;
            if (p_item == _p_item)
            {
                it = callback_list.erase(it);
                reconfig_timer(0);
                break;
            }
        }
        xSemaphoreGive(timer_semaphore);
        gptimer_start(gptimer);
        vPortFree(p_item);
    }
};
*/
class math_helper
{
public:
    static float limit_range(float num, float max, float min)
    {
        return num < min ? (min) : (num > max ? max : num);
    }
};