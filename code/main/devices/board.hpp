#pragma once
#include <helper.hpp>
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
        uint8_t channel_num = 3;
        adc_continuous_handle_cfg_t adc_config = {
            .max_store_buf_size = 256,
            .conv_frame_size = 64,
        };
        adc_digi_pattern_config_t adc_pattern[channel_num] = {0};
        adc_continuous_config_t dig_cfg = {
            .pattern_num = channel_num,
            .adc_pattern = adc_pattern,
            .sample_freq_hz = 80 * 1000,
            .conv_mode = ADC_CONV_SINGLE_UNIT_1,
            .format = ADC_DIGI_OUTPUT_FORMAT_TYPE2,
        };
        adc_continuous_evt_cbs_t cbs = {
            .on_conv_done = on_adc_conv_done,
        };
        adc_cali_curve_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .chan = ADC_CHANNEL_3,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        // 风扇电流采样
        adc_pattern[0].atten = ADC_ATTEN_DB_12;
        adc_pattern[0].channel = ADC_CHANNEL_0;
        adc_pattern[0].unit = ADC_UNIT_1;
        adc_pattern[0].bit_width = ADC_BITWIDTH_12;
        // 风扇电压采样
        adc_pattern[1].atten = ADC_ATTEN_DB_12;
        adc_pattern[1].channel = ADC_CHANNEL_1;
        adc_pattern[1].unit = ADC_UNIT_1;
        adc_pattern[1].bit_width = ADC_BITWIDTH_12;
        // 输入电压采样
        adc_pattern[2].atten = ADC_ATTEN_DB_12;
        adc_pattern[2].channel = ADC_CHANNEL_3;
        adc_pattern[2].unit = ADC_UNIT_1;
        adc_pattern[2].bit_width = ADC_BITWIDTH_12;

        ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_continuous_handle));
        ESP_ERROR_CHECK(adc_continuous_config(adc_continuous_handle, &dig_cfg));
        ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_continuous_handle, &cbs, this));
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&cali_config, &adc_cali_handle));
        ESP_ERROR_CHECK(adc_continuous_start(adc_continuous_handle));
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
    static bool IRAM_ATTR on_adc_conv_done(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
    {
        BaseType_t mustYield = pdFALSE;
        board *board_obj = (board *)user_data;
        for (int i = 0; i < edata->size; i += SOC_ADC_DIGI_RESULT_BYTES)
        {
            adc_digi_output_data_t *p = (adc_digi_output_data_t *)&edata->conv_frame_buffer[i];
            switch ((p)->type2.channel)
            {
            case ADC_CHANNEL_0:
                board_obj->adc_channel0_value = (p)->type2.data;
                break;
            case ADC_CHANNEL_1:
                board_obj->adc_channel1_value = (p)->type2.data;
                break;
            case ADC_CHANNEL_2:
                board_obj->adc_channel2_value = (p)->type2.data;
                break;
            default:
                break;
            };
        }
        return mustYield;
    }

public:
    uint16_t adc_channel0_value = 0;
    uint16_t adc_channel1_value = 0;
    uint16_t adc_channel2_value = 0;

public:
    gpio_num_t sda_pin = GPIO_NUM_9;
    gpio_num_t scl_pin = GPIO_NUM_8;
    adc_continuous_handle_t adc_continuous_handle;
    adc_cali_handle_t adc_cali_handle;
    i2c_master_bus_handle_t i2c_master_bus_handle;

    board();
    ~board();
};