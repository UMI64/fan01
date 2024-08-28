#include <helper.hpp>
class keyboard
{
private:
    gpio_num_t d_pin = GPIO_NUM_5;
    gpio_num_t b_pin = GPIO_NUM_7;
    gpio_num_t a_pin = GPIO_NUM_6;
public:
    keyboard();
    ~keyboard();
};