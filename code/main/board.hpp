#pragma once
#include <helper.hpp>
#include "devices/fan.hpp"
#include "devices/husb238.hpp"
#include "devices/keyboard.hpp"
class board
{
private:
    void i2c_init(void)
    {
        i2c_master_bus_config_t i2c_master_bus_config = {};
        i2c_master_bus_config.i2c_port = I2C_NUM_0;
        i2c_master_bus_config.sda_io_num = sda_pin;
        i2c_master_bus_config.scl_io_num = scl_pin;
        i2c_master_bus_config.clk_source = I2C_CLK_SRC_DEFAULT;
        i2c_master_bus_config.glitch_ignore_cnt = 7;
        i2c_master_bus_config.intr_priority = 0;
        i2c_master_bus_config.flags.enable_internal_pullup = 1;
        ESP_ERROR_CHECK(i2c_new_master_bus(&i2c_master_bus_config, &i2c_master_bus_handle));
    }
    void adc_init(void)
    {
        adc_oneshot_unit_init_cfg_t adc_init_config = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        ESP_ERROR_CHECK(adc_oneshot_new_unit(&adc_init_config, &adc_oneshot_unit_handle));

        adc_oneshot_chan_cfg_t adc_chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_oneshot_config_channel(adc_oneshot_unit_handle, ADC_CHANNEL_3, &adc_chan_cfg));

        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = ADC_CHANNEL_3,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
    }
    void pwm_init(void)
    {
        ledc_timer_config_t ledc_timer = {};
        ledc_timer.speed_mode = LEDC_LOW_SPEED_MODE;
        ledc_timer.timer_num = LEDC_TIMER_0;
        ledc_timer.duty_resolution = LEDC_TIMER_13_BIT;
        ledc_timer.freq_hz = 1500; // Set output frequency at 1 kHz
        ledc_timer.clk_cfg = LEDC_AUTO_CLK;
        ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));
    }
    void nvs_init(void)
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
    }

public:
    gpio_num_t sda_pin = GPIO_NUM_9;
    gpio_num_t scl_pin = GPIO_NUM_8;

    adc_oneshot_unit_handle_t adc_oneshot_unit_handle;
    adc_cali_handle_t adc_cali_handle;
    i2c_master_bus_handle_t i2c_master_bus_handle;

    fan *fan_obj;
    husb238 *husb238_obj;
    keyboard *keyboard_obj;
    board();
    ~board();
};