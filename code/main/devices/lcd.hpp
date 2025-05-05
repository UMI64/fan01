#pragma once
#include <helper.hpp>
class lcd
{
private:
    std::function<void(void)> color_trans_done_cb = nullptr;
    i2c_master_bus_handle_t i2c_master_bus_handle = NULL;

public:
    i2c_master_dev_handle_t i2c_master_dev_handle = NULL;
    static const uint16_t width = 128;
    static const uint16_t height = 32;
    lcd(i2c_master_bus_handle_t &i2c_master_bus_handle);
    void register_flush_done_cb(std::function<void(void)> cb);
    static void color_trans_done(void *ctx);
    helper_err::code read(uint8_t addr, uint8_t *data)
    {
        esp_err_t err = i2c_master_transmit_receive(i2c_master_dev_handle, &addr, sizeof(addr), data, sizeof(*data), 100);
        return (helper_err::code)err;
    }
    helper_err::code write(uint8_t *data, uint8_t size, int timeout_ms)
    {
        esp_err_t err = i2c_master_transmit(i2c_master_dev_handle, data, size, timeout_ms);
        return (helper_err::code)err;
    }
};