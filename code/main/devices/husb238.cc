#include "husb238.hpp"
husb238::husb238(i2c_master_bus_handle_t &i2c_master_bus_handle)
{
    i2c_device_config_t i2c_device_config = {};
    i2c_device_config.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    i2c_device_config.device_address = 0x8;
    i2c_device_config.scl_speed_hz = 300000;
    i2c_device_config.scl_wait_us = 0;
    i2c_device_config.flags.disable_ack_check = false;
    ESP_ERROR_CHECK(i2c_master_bus_add_device(i2c_master_bus_handle, &i2c_device_config, &i2c_master_dev_handle));
    pdo_cap[src_pdo_5v] = 0;
    pdo_cap[src_pdo_9v] = 0;
    pdo_cap[src_pdo_12v] = 0;
    pdo_cap[src_pdo_15v] = 0;
    pdo_cap[src_pdo_18v] = 0;
    pdo_cap[src_pdo_20v] = 0;
    get_pdo_cap(NULL);
}
husb238::~husb238()
{
}
float husb238::read_pdo_cap(src_pdo_voltage voltage)
{
    auto cap = pdo_cap[voltage];
    if (cap & 0x80)
        return reg_to_current(cap & 0xf);
    else
        return 0;
}
helper_err::code husb238::get_status0(status0_t *_status0)
{
    uint8_t data;
    helper_err::code err = read(0, &data);
    if (err != helper_err::code::success)
        return err;
    status0.src_pd_voltage = (src_pdo_voltage)(data >> 4);
    status0.src_pd_current = (src_pdo_current)(data & 0b1111);
    if (_status0)
        *_status0 = status0;
    return helper_err::code::success;
}
helper_err::code husb238::get_status1(status1_t *_status1)
{
    uint8_t data;
    helper_err::code err = read(1, &data);
    if (err != helper_err::code::success)
        return err;
    status1.cc_dir = data >> 7;
    status1.attach = (data >> 6) & 0b1;
    status1.pd_response = (pd_response_e)((data >> 3) & 0b111);
    status1._5v_voltage = (uint8_t)((data >> 2) & 0b1);
    status1._5v_current = (_5v_current_e)((data >> 1) & 0b11);
    if (_status1)
        *_status1 = status1;
    return helper_err::code::success;
}
helper_err::code husb238::get_pdo_cap(pdo_cap_t *_pdo_cap)
{
    auto get_src_pdo_cap = [&](uint8_t pdo_addr) -> helper_err::code
    {
        uint8_t data;
        helper_err::code err;
        err = read(pdo_addr, &data);
        if (err != helper_err::code::success)
        {
            ESP_LOGI("HSUSB238", "%s: read addr %x err %s", __func__, pdo_addr, helper_err::to_string(err).c_str());
            return err;
        }
        pdo_cap[addr_to_voltage(pdo_addr)] = data;
        return err;
    };
    helper_err::code err;
    pdo_cap.clear();
    for (auto addr : {2, 3, 4, 5, 6, 7})
    {
        err = get_src_pdo_cap(addr);
        if (err != helper_err::code::success)
            return err;
    }
    if (_pdo_cap)
        *_pdo_cap = pdo_cap;
    return helper_err::code::success;
}
helper_err::code husb238::set_pdo(src_pdo_voltage pdo)
{
    return write(0x8, pdo << 4);
}
helper_err::code husb238::req_pdo()
{
    return write(0x9, 0b1);
}
helper_err::code husb238::req_pdo_cap()
{
    return write(0x9, 0b100);
}
helper_err::code husb238::req_reset()
{
    return write(0x9, 0b10000);
}
