#include <helper.hpp>
class keyboard
{
public:
    typedef enum
    {
        press_key,
        right_key,
        left_key,
    } keys;
    typedef enum
    {
        short_press,
        long_press,
        continue_press,
    } actions;

private:
    const static gpio_num_t d_key_pin = GPIO_NUM_10;
    const static gpio_num_t b_key_pin = GPIO_NUM_7;
    const static gpio_num_t a_key_pin = GPIO_NUM_6;
    typedef enum
    {
        null,
        down,
        up,
    } key_wait;
    typedef struct
    {
        bool level;
        gpio_num_t pin;
    } key_changes;
    struct key_obj
    {
        keys key;
        gpio_num_t pin;
        int level = 1;
        key_wait wait = key_wait::null;
        TickType_t report_tick = 0;
        TickType_t changed_tick = 0;
        TickType_t short_tick = 100;
        TickType_t long_tick = 350;
        TickType_t continue_tick = 800;
    } press_key_obj, right_key_obj, left_key_obj;
    std::vector<std::function<void(keys key, actions action)>> caller_list;
    thread_helper *message_task_handle;
    QueueHandle_t message_queue;

    static void IRAM_ATTR press_key_pin_isr_handler(void *arg);
    static void IRAM_ATTR right_key_pin_isr_handler(void *arg);
    static void IRAM_ATTR left_key_pin_isr_handler(void *arg);
    void message_task(void *param);

public:
    keyboard();
    ~keyboard();
    const static char *key_to_name(keys key);
    const static char *action_to_name(keyboard::actions action);
    void register_callback(std::function<void(keys key, actions action)> caller);
    void unregister_callback(std::function<void(keys key, actions action)> caller);
};