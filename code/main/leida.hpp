#include "leida_hard.hpp"
#include "leida_wifi.hpp"
#include "leida_web.hpp"
class leida
{
private:
    leida_wifi *wifi_obj;
    leida_web *web_obj;
    leida_hard *leida_hard_obj;
    thread_helper *main_thread;
    void main_thread_task(void *param);

public:
    leida();
    ~leida();
};