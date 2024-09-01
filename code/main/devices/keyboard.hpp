#include <helper.hpp>
class keyboard
{
private:
    gpio_num_t d_pin = GPIO_NUM_5;
    gpio_num_t b_pin = GPIO_NUM_7;
    gpio_num_t a_pin = GPIO_NUM_6;
    thread_helper *message_task_handle;
    QueueHandle_t message_queue;
    // timer_helper check_timer_call_item;
    static void check_timer_call(void *param);
    void message_task(void *param);
public:
    keyboard();
    ~keyboard();
};