#pragma once
#include <functional>
#include <string>
#include <map>
#include <vector>
#include <atomic>
#include <string.h>
#include <stdio.h>
#include <sdkconfig.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_err.h>
#include <esp_log.h>
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_continuous.h"
#include "driver/i2c_master.h"
#include "driver/gptimer.h"
#include "driver/ledc.h"
#include "driver/gpio.h"
#include "nvs_flash.h"
#include "esp_timer.h"
#define ONEBIT(x) (1 << x)

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
    void init(std::function<void(void *)> caller, const char *name, void *param, uint32_t stack_depth, uint8_t prio)
    {
        task_caller = caller;
        task_param = param;
        xTaskCreate(this->caller, name, stack_depth, this, prio, &pxCreatedTask);
        if (pxCreatedTask != NULL)
            vTaskSetThreadLocalStoragePointer(pxCreatedTask, 0, (void *)0);
    }

public:
    thread_helper(std::function<void(void *)> caller, void *param = nullptr, std::string name = "", uint32_t stack_depth = 4096, uint8_t prio = 1)
    {
        init(caller, name.c_str(), param, stack_depth, prio);
    }
    thread_helper(std::function<void(void *)> caller, void *param = nullptr, const char *name = NULL, uint32_t stack_depth = 4096, uint8_t prio = 1)
    {
        init(caller, name ? name : "", param, stack_depth, prio);
    }
    thread_helper(std::function<void(void *)> caller, std::string name = "", void *param = nullptr, uint32_t stack_depth = 4096, uint8_t prio = 1)
    {
        init(caller, name.c_str(), param, stack_depth, prio);
    }
    thread_helper(std::function<void(void *)> caller, const char *name = NULL, void *param = nullptr, uint32_t stack_depth = 4096, uint8_t prio = 1)
    {
        init(caller, name ? name : "", param, stack_depth, prio);
    }
    thread_helper(std::function<void(void *)> caller, uint32_t stack_depth, void *param = nullptr, std::string name = "", uint8_t prio = 1)
    {
        init(caller, name.c_str(), param, stack_depth, prio);
    }
    thread_helper(std::function<void(void *)> caller, uint32_t stack_depth, void *param = nullptr, const char *name = "", uint8_t prio = 1)
    {
        init(caller, name ? name : "", param, stack_depth, prio);
    }
    thread_helper(std::function<void(void *)> caller, uint32_t stack_depth, std::string name = "", void *param = nullptr, uint8_t prio = 1)
    {
        init(caller, name.c_str(), param, stack_depth, prio);
    }
    thread_helper(std::function<void(void *)> caller, uint32_t stack_depth, const char *name = "", void *param = nullptr, uint8_t prio = 1)
    {
        init(caller, name ? name : "", param, stack_depth, prio);
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
    static TickType_t tick_to_ms(TickType_t tick)
    {
        return pdTICKS_TO_MS(tick);
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
    StaticSemaphore_t xMutexBuffer;

public:
    thread_mutex_lock(void)
    {
        semaphore = xSemaphoreCreateMutexStatic(&xMutexBuffer);
    }
    void lock(void)
    {
        xSemaphoreTake(semaphore, portMAX_DELAY);
    }
    bool try_lock(void)
    {
        return xSemaphoreTake(semaphore, 0) == pdTRUE;
    }
    void unlock(void)
    {
        xSemaphoreGive(semaphore);
    }
};

class thread_mutex_lock_guard
{
    friend class thread_mutex_lock;

private:
    thread_mutex_lock &mutex_;

public:
    explicit thread_mutex_lock_guard(thread_mutex_lock &mutex) : mutex_(mutex)
    {
        mutex_.lock();
    }
    ~thread_mutex_lock_guard()
    {
        mutex_.unlock();
    }
};

class thread_semaphore
{
private:
    SemaphoreHandle_t semaphore;
    StaticSemaphore_t xSemaphoreBuffer;

public:
    thread_semaphore(void)
    {
        semaphore = xSemaphoreCreateBinaryStatic(&xSemaphoreBuffer);
    }
    bool wait()
    {
        xSemaphoreTake(semaphore, portMAX_DELAY) == pdTRUE;
    }
    bool wait(uint32_t wait_time_ms) // pdTRUE: 成功等待 pdFALSE: 超时
    {
        xSemaphoreTake(semaphore, pdMS_TO_TICKS(wait_time_ms)) == pdTRUE;
    }
    bool try_wait(void) // pdTRUE: 成功等待 pdFALSE: 超时
    {
        return xSemaphoreTake(semaphore, 0) == pdTRUE;
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
    class pid
    {
    private:
        float diff = 0;
        float diff_sum = 0;
        float diff_change = 0;

    public:
        float p = 0, i = 0, d = 0;
        float i_min = 0, i_max = 0;
        float target_val = 0;

        float p_vote, i_vote, d_vote;
        pid(float p, float i, float d, float i_min = 0, float i_max = 0) : p(p), i(i), d(d), i_min(i_min), i_max(i_max)
        {
        }
        void set_target(float target_val)
        {
            this->target_val = target_val;
        }
        float do_cal(float now_val)
        {
            diff_change = target_val - now_val - diff;
            diff = diff + diff_change;
            diff_sum += diff;
            diff_sum = math_helper::limit_range(diff_sum, i_max, i_min);
            p_vote = diff * p;
            i_vote = diff_sum * i;
            d_vote = diff_change * d;
            return p_vote + i_vote + d_vote;
        }
        void reset()
        {
            diff = 0;
            diff_sum = 0;
        }
    };
};

class flag_helper
{
private:
    uint32_t __flags = 0;

public:
    /* 读取mask范围内的flags是否激活
        flags 0110  0110
        mask  0111  0100
        flag  0111  0100
              false true
    */
    bool read_flag(uint32_t flags, uint32_t mask)
    {
        return (__flags & mask) == flags;
    }
    // 读取flags是否激活
    bool read_flag(uint32_t flags)
    {
        return read_flag(flags, flags);
    }
    // 激活选中的flags
    bool set_flag(uint32_t flagss)
    {
        bool old = read_flag(flagss);
        __flags |= flagss;
        return old;
    }
    // 清除选中的flags
    bool unset_flag(uint32_t flagss)
    {
        bool old = read_flag(flagss);
        __flags &= ~flagss;
        return old;
    }
};