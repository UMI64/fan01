#pragma once
#include "helper.hpp"
#include "controller.hpp"
#include "u8g2.h"
#include "ui/ui_base.hpp"
#include "ui/ui_text.hpp"
#include "ui/ui_button.hpp"

#undef TAG
#define TAG "ui"
#define I2C_BUFFER_SIZE 32
class ui;

class main_page
{
    friend class ui;
    friend class menu_page;

private:
    ui *ui_obj;

protected:
    ui_base *base_obj = nullptr;
    ui_text *ui_voltage_obj = nullptr;
    ui_text *ui_current_obj = nullptr;
    ui_text *ui_power_obj = nullptr;
    ui_text *ui_target_speed_obj = nullptr;
    ui_text *ui_speed_obj = nullptr;

public:
    main_page(ui *ui_obj);
    bool key_event_cb(uint32_t key, uint32_t key_continue_ms, bool press, bool change);
};

class menu_page
{
    friend class ui;
    friend class main_page;

private:
    ui *ui_obj;

protected:
    ui_base *base_obj = nullptr;
    ui_button *test_button_obj = nullptr;

public:
    menu_page(ui *ui_obj);
    bool key_event_cb(uint32_t key, uint32_t key_continue_ms, bool press, bool change);
};

class ui
{
    friend class main_page;
    friend class menu_page;

protected:
    thread_mutex_lock render_lock;
    u8g2_t u8g2;
    controller *controller_obj;
    ui_base *ui_window_obj;
    main_page *main_page_obj;
    menu_page *menu_page_obj;

private:
    uint8_t i2c_write_data[I2C_BUFFER_SIZE];
    size_t i2c_write_data_size;

    thread_helper *render_thread;
    thread_helper *manage_thread;
    void render_task(void *param);
    void manage_task(void *param);
    static uint8_t gpio_and_delay_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
    {
        switch (msg)
        {
        case U8X8_MSG_DELAY_MILLI: // delay arg_int * 1 milli second
            ESP_LOGD(TAG, "u8x8 delay %u milli", arg_int);
            thread_helper::sleep(arg_int);
            break;
        case U8X8_MSG_GPIO_RESET:
            ESP_LOGD(TAG, "u8x8 reset");
            break;
        case U8X8_MSG_GPIO_MENU_SELECT:
            u8x8_SetGPIOResult(u8x8, /* get menu select pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_NEXT:
            u8x8_SetGPIOResult(u8x8, /* get menu next pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_PREV:
            u8x8_SetGPIOResult(u8x8, /* get menu prev pin state */ 0);
            break;
        case U8X8_MSG_GPIO_MENU_HOME:
            u8x8_SetGPIOResult(u8x8, /* get menu home pin state */ 0);
            break;
        default:
            ESP_LOGI(TAG, "u8x8 default msg %d", msg);
            u8x8_SetGPIOResult(u8x8, 1); // default return value
            break;
        }
        return 1;
    }
    static uint8_t byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
    {
        ui *pui = (ui *)u8x8_GetUserPtr(u8x8);
        switch (msg)
        {
        case U8X8_MSG_BYTE_INIT:
        {
            ESP_LOGD(TAG, "byte_cb init");
            break;
        }
        case U8X8_MSG_BYTE_SEND:
        {
            ESP_LOGD(TAG, "byte_cb send %d", arg_int);
            if (arg_int + pui->i2c_write_data_size > I2C_BUFFER_SIZE)
            {
                ESP_LOGE(TAG, "I2C transfer size is too large %u > %u.", pui->i2c_write_data_size + arg_int, I2C_BUFFER_SIZE);
                arg_int = I2C_BUFFER_SIZE - pui->i2c_write_data_size;
                ESP_LOGE(TAG, "I2C transfer size is truncated to %u.", arg_int);
            }
            memcpy(&pui->i2c_write_data[pui->i2c_write_data_size], arg_ptr, arg_int);
            pui->i2c_write_data_size += arg_int;
            break;
        }

        case U8X8_MSG_BYTE_START_TRANSFER:
        {
            ESP_LOGD(TAG, "Start I2C transfer.");
            memset(pui->i2c_write_data, 0, sizeof(pui->i2c_write_data));
            pui->i2c_write_data_size = 0;
            break;
        }

        case U8X8_MSG_BYTE_END_TRANSFER:
        {
            ESP_LOGD(TAG, "End I2C transfer %u.", pui->i2c_write_data_size);
            i2c_master_transmit(pui->board_obj->lcd_obj->i2c_master_dev_handle, pui->i2c_write_data, pui->i2c_write_data_size, 100);
            break;
        }

        default:
        {
            ESP_LOGW(TAG, "byte_cb: default case");
            return 0;
        }
        }
        return 1;
    }
    void keyboard_callback(keyboard::keys key, uint32_t continue_ms, bool press, bool change);

public:
    ui(controller *controller_obj);
};
