#include "leida.hpp"
leida::leida()
{
    leida_hard_obj = new leida_hard;
    wifi_obj = new leida_wifi;
    web_obj = new leida_web;
    main_thread = new thread_helper(std::bind(&leida::main_thread_task, this, std::placeholders::_1));
}
leida::~leida()
{
    delete main_thread;
    delete web_obj;
    delete wifi_obj;
    delete leida_hard_obj;
}
void leida::main_thread_task(void *param)
{
    leida_hard::leida::leida_target_t targets[3];
    leida_hard_obj->led(true);
    leida_hard_obj->leida_obj->power(true);
    ESP_LOGI("leida", "on");
    while (true)
    {
        thread_helper::sleep(400);
        leida_hard_obj->leida_obj->get_targets(targets, 3);
        for (int i = 0; i < 3; i++)
            if (targets[i].xpos_mm != 0 || targets[i].ypos_mm != 0)
                ESP_LOGI("leida", "T%d x:[%d] y:[%d] speed:[%d] reslusion:[%u]", i, targets[i].xpos_mm, targets[i].ypos_mm, targets[i].speed_cms, targets[i].reslusion_mm);
    }
}