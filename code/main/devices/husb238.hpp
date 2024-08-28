#pragma once
#include "helper.hpp"
class husb238
{
public:
    typedef std::map<int, uint8_t> pdo_cap_t;
    typedef enum src_pdo_voltage
    {
        src_pdo_not_select = 0,
        src_pdo_5v = 0b0001,
        src_pdo_9v = 0b0010,
        src_pdo_12v = 0b0011,
        src_pdo_15v = 0b1000,
        src_pdo_18v = 0b1001,
        src_pdo_20v = 0b1010
    } src_pdo_voltage;
    typedef enum src_pdo_current
    {
        src_pdo_0_5a = 0b0000,
        src_pdo_0_7a = 0b0001,
        src_pdo_1a = 0b0010,
        src_pdo_1_25a = 0b0011,
        src_pdo_1_5a = 0b0100,
        src_pdo_1_75a = 0b0101,
        src_pdo_2a = 0b0110,
        src_pdo_2_25a = 0b0111,
        src_pdo_2_5a = 0b1000,
        src_pdo_2_75a = 0b1001,
        src_pdo_3a = 0b1010,
        src_pdo_3_25a = 0b1011,
        src_pdo_3_5a = 0b1100,
        src_pdo_4a = 0b1101,
        src_pdo_4_5a = 0b1110,
        src_pdo_5a = 0b1111
    } src_pdo_current;
    typedef enum pd_response_e
    {
        pd_response_no_response = 0b000,
        pd_response_success = 0b001,
        pd_response_invalid_command_or_argument = 0b011,
        pd_response_command_not_supported = 0b100,
        pd_response_transaction_fail_no_goodCRC_is_received_after_sending = 0b101
    } pd_response_e;
    typedef enum _5v_current_e
    {
        _5v_current_1_5a = 0b01,
        _5v_current_2_4a = 0b10,
        _5v_current_3a = 0b11
    } _5v_current_e;
    class status0_t
    {
    public:
        src_pdo_voltage src_pd_voltage;
        src_pdo_current src_pd_current;
    };
    class status1_t
    {
    public:
        uint8_t cc_dir;
        uint8_t attach;
        pd_response_e pd_response;
        uint8_t _5v_voltage;
        _5v_current_e _5v_current;
    };
    static constexpr src_pdo_voltage support_voltages[6] = {src_pdo_5v, src_pdo_9v, src_pdo_12v, src_pdo_15v, src_pdo_18v, src_pdo_20v};
    static uint8_t src_pdo_voltage_to_float(src_pdo_voltage voltage)
    {
        switch (voltage)
        {
        case src_pdo_5v:
            return 5;
        case src_pdo_9v:
            return 9;
        case src_pdo_12v:
            return 12;
        case src_pdo_15v:
            return 15;
        case src_pdo_18v:
            return 18;
        case src_pdo_20v:
            return 20;
        default:
            return 0;
        }
    }

private:
    i2c_master_dev_handle_t i2c_master_dev_handle;
    helper_err::code read(uint8_t addr, uint8_t *data)
    {
        esp_err_t err = i2c_master_transmit_receive(i2c_master_dev_handle, &addr, sizeof(addr), data, sizeof(*data), 100);
        return (helper_err::code)err;
    }
    helper_err::code write(uint8_t addr, uint8_t data)
    {
        uint8_t buffer[2] = {addr, data};
        esp_err_t err = i2c_master_transmit(i2c_master_dev_handle, buffer, sizeof(buffer), 100);
        return (helper_err::code)err;
    }
    static float reg_to_current(uint8_t reg)
    {
        switch (reg)
        {
        case 0b0000:
            return 0.5;
        case 0b0001:
            return 0.7;
        case 0b0010:
            return 1;
        case 0b0011:
            return 1.25;
        case 0b0100:
            return 1.5;
        case 0b0101:
            return 1.75;
        case 0b0110:
            return 2;
        case 0b0111:
            return 2.25;
        case 0b1000:
            return 2.50;
        case 0b1001:
            return 2.75;
        case 0b1010:
            return 3;
        case 0b1011:
            return 3.25;
        case 0b1100:
            return 3.5;
        case 0b1101:
            return 4;
        case 0b1110:
            return 4.5;
        case 0b1111:
            return 5;
        default:
            return 0;
        }
    }
    static src_pdo_voltage addr_to_voltage(uint8_t voltage_addr)
    {
        switch (voltage_addr)
        {
        case 2:
            return src_pdo_5v;
        case 3:
            return src_pdo_9v;
        case 4:
            return src_pdo_12v;
        case 5:
            return src_pdo_15v;
        case 6:
            return src_pdo_18v;
        case 7:
            return src_pdo_20v;
        default:
            return src_pdo_not_select;
        }
    }

public:
    pdo_cap_t pdo_cap;
    status0_t status0;
    status1_t status1;
    husb238(i2c_master_bus_handle_t &i2c_master_bus_handle);
    ~husb238();
    float read_pdo_cap(src_pdo_voltage voltage);
    helper_err::code get_status0(status0_t *_status0);
    helper_err::code get_status1(status1_t *_status1);
    helper_err::code get_pdo_cap(pdo_cap_t *_pdo_cap);
    helper_err::code set_pdo(src_pdo_voltage pdo);
    helper_err::code req_pdo();
    helper_err::code req_pdo_cap();
    helper_err::code req_reset();
};