#include "lcd.hpp"
lcd::lcd(i2c_master_bus_handle_t &i2c_master_bus_handle) : i2c_master_bus_handle(i2c_master_bus_handle)
{
    i2c_device_config_t i2c_device_config = {};
    i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    i2c_device_config.device_address = 0b0111100;
    i2c_device_config.scl_speed_hz = 400000;
    i2c_device_config.scl_wait_us = 0;
    i2c_device_config.flags.disable_ack_check = false;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_master_bus_handle, &i2c_device_config, &i2c_master_dev_handle));
}
void lcd::register_flush_done_cb(std::function<void(void)> cb)
{
    color_trans_done_cb = cb;
}