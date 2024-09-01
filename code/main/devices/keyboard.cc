#include "keyboard.hpp"
keyboard::keyboard()
{
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = 1ULL << d_pin | 1ULL << b_pin | 1ULL << a_pin;
    io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    message_queue = xQueueCreate(8, sizeof(int8_t));
    message_task_handle = new thread_helper(std::bind(&keyboard::message_task, this, std::placeholders::_1), 4096);
    // check_timer_call_item = hotpad_hard_obj->timer_call_obj->add_timer_callback(encoder_check_timer_call, 1000, true, this);
}
keyboard::~keyboard()
{
}
// static void check_timer_call(void *param)
// {
//     keyboard *encoder_obj = (keyboard *)param;
//     if (encoder_pin_filter(&encoder_obj->a_pin))
//         xQueueSendFromISR(encoder_obj->message_queue, &encoder_obj->a_pin.pin, NULL);
//     if (encoder_pin_filter(&encoder_obj->b_pin))
//         xQueueSendFromISR(encoder_obj->message_queue, &encoder_obj->b_pin.pin, NULL);
//     if (encoder_pin_filter(&encoder_obj->d_pin))
//         xQueueSendFromISR(encoder_obj->message_queue, &encoder_obj->d_pin.pin, NULL);
// }
void keyboard::message_task(void *param)
{
    while (true)
    {
        int8_t pin_change_msg;
        if (xQueuePeek(message_queue, &pin_change_msg, pdMS_TO_TICKS(10)) == pdTRUE)
        {
            xQueueReceive(message_queue, &pin_change_msg, 0);
        }
    }
}
