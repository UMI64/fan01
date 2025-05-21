#pragma once
#include <helper.hpp>
#undef TAG
#define TAG "board"
class board
{
#define ADC_SAMPLE_DEEP 1000
#define ADC_SAMPLE_CHANNEL_NUM 3
private:
    thread_helper *adc_filter_task_handle;
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
        adc_atten_t adc_atten = ADC_ATTEN_DB_12;
        adc_bitwidth_t adc_bitwidth = ADC_BITWIDTH_12;
        adc_unit_t adc_unit = ADC_UNIT_1;
        adc_continuous_handle_cfg_t adc_config;
        adc_digi_pattern_config_t adc_pattern[channel_num];
        adc_continuous_config_t dig_cfg;
        adc_continuous_evt_cbs_t adc_continuous_evt_cbs;
        adc_cali_curve_fitting_config_t adc_cali_config;
        memset(adc_channel_value, 0, sizeof(adc_channel_value));
        adc_config.conv_frame_size = SOC_ADC_DIGI_DATA_BYTES_PER_CONV * channel_num * ADC_SAMPLE_DEEP;
        adc_config.max_store_buf_size = adc_config.conv_frame_size;
        adc_config.flags.flush_pool = 0;
        // 风扇电流采样
        adc_pattern[0].atten = adc_atten;
        adc_pattern[0].channel = ADC_CHANNEL_0;
        adc_pattern[0].unit = adc_unit;
        adc_pattern[0].bit_width = adc_bitwidth;
        // 风扇电压采样
        adc_pattern[1].atten = adc_atten;
        adc_pattern[1].channel = ADC_CHANNEL_1;
        adc_pattern[1].unit = adc_unit;
        adc_pattern[1].bit_width = adc_bitwidth;
        // 输入电压采样
        adc_pattern[2].atten = adc_atten;
        adc_pattern[2].channel = ADC_CHANNEL_3;
        adc_pattern[2].unit = adc_unit;
        adc_pattern[2].bit_width = adc_bitwidth;

        dig_cfg.pattern_num = channel_num;
        dig_cfg.adc_pattern = adc_pattern;
        dig_cfg.sample_freq_hz = 30000;
        dig_cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
        dig_cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;

        adc_continuous_evt_cbs.on_conv_done = on_adc_conv_done;
        adc_continuous_evt_cbs.on_pool_ovf = nullptr;

        adc_cali_config.unit_id = adc_unit;
        adc_cali_config.atten = adc_atten;
        adc_cali_config.bitwidth = adc_bitwidth;

        ESP_ERROR_CHECK(adc_continuous_new_handle(&adc_config, &adc_continuous_handle));
        ESP_ERROR_CHECK(adc_continuous_config(adc_continuous_handle, &dig_cfg));
        ESP_ERROR_CHECK(adc_continuous_register_event_callbacks(adc_continuous_handle, &adc_continuous_evt_cbs, this));
        adc_cali_config.chan = (adc_channel_t)adc_pattern[0].channel;
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&adc_cali_config, &adc_channel_value[0].adc_cali_handle));
        adc_cali_config.chan = (adc_channel_t)adc_pattern[1].channel;
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&adc_cali_config, &adc_channel_value[1].adc_cali_handle));
        adc_cali_config.chan = (adc_channel_t)adc_pattern[2].channel;
        ESP_ERROR_CHECK(adc_cali_create_scheme_curve_fitting(&adc_cali_config, &adc_channel_value[2].adc_cali_handle));
        adc_filter_task_handle = new thread_helper(std::bind(&board::adc_filter_task, this), TAG ":adc_filter_task");
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
        nvs_handle = nvs::open_nvs_handle("storage", NVS_READWRITE, &ret);
        ESP_ERROR_CHECK(ret);
    }
    static int adc_channel_to_array(adc_channel_t channel)
    {
        switch (channel)
        {
        case ADC_CHANNEL_0:
            return 0;
        case ADC_CHANNEL_1:
            return 1;
        case ADC_CHANNEL_3:
            return 2;
        default:
            return -1;
        }
    }
    static bool IRAM_ATTR on_adc_conv_done(adc_continuous_handle_t handle, const adc_continuous_evt_data_t *edata, void *user_data)
    {
        BaseType_t mustYield = pdFALSE;
        board *board_obj = (board *)user_data;
        board_obj->adc_filter_task_handle->notify_isr(&mustYield);
        // for (int i = 0; i < edata->size; i += SOC_ADC_DIGI_RESULT_BYTES)
        // {
        //     adc_digi_output_data_t *p = (adc_digi_output_data_t *)&edata->conv_frame_buffer[i];
        //     struct adc_value_t *adc_value = &board_obj->adc_channel_value[board_obj->adc_channel_to_array((adc_channel_t)(p)->type2.channel)];
        //     adc_value->value[adc_value->value_index] = (p)->type2.data;
        //     adc_value->value_index = (adc_value->value_index + 1) % ADC_SAMPLE_DEEP;
        // }
        return mustYield;
    }
    int adc_filter_task()
    {
        uint8_t value[ADC_SAMPLE_DEEP * ADC_SAMPLE_CHANNEL_NUM];
        ESP_ERROR_CHECK(adc_continuous_start(adc_continuous_handle));
        while (!thread_helper::thread_is_exit())
        {
            uint32_t out_length = 0;
            if (adc_continuous_read(adc_continuous_handle, value, sizeof(value), &out_length, 50) == ESP_OK)
            {
                for (int i = 0; i < out_length; i += SOC_ADC_DIGI_RESULT_BYTES)
                {
                    adc_digi_output_data_t *p = (adc_digi_output_data_t *)&value[i];
                    struct adc_value_t *adc_value = &adc_channel_value[adc_channel_to_array((adc_channel_t)(p)->type2.channel)];
                    adc_value->__temp_value += (p)->type2.data;
                    adc_value->__temp_count++;
                }
                for (int i = 0; i < ADC_SAMPLE_CHANNEL_NUM; i++)
                {
                    adc_channel_value[i].filter_value = adc_channel_value[i].__temp_value / adc_channel_value[i].__temp_count;
                    adc_channel_value[i].__temp_count = 0;
                    adc_channel_value[i].__temp_value = 0;
                }
            }
        }
        return 0;
    }

public:
    struct adc_value_t
    {
        adc_cali_handle_t adc_cali_handle;
        uint32_t __temp_value;
        uint32_t __temp_count;
        uint16_t filter_value;
    } adc_channel_value[3];

public:
    gpio_num_t sda_pin = GPIO_NUM_9;
    gpio_num_t scl_pin = GPIO_NUM_8;
    adc_continuous_handle_t adc_continuous_handle;
    i2c_master_bus_handle_t i2c_master_bus_handle;
    std::unique_ptr<nvs::NVSHandle> nvs_handle;

    board();
    ~board();

    int read_voltage(adc_channel_t channel)
    {
        int voltage = 0;
        int array_index = adc_channel_to_array(channel);
        if (array_index >= 0)
        {
            struct adc_value_t *adc_value = &adc_channel_value[array_index];
            adc_cali_raw_to_voltage(adc_value->adc_cali_handle, adc_value->filter_value, &voltage);
        }
        return voltage;
    }
};