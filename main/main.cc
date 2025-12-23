#include "app/app.hpp"
#include "esp_log.h"

extern "C" void app_main(void)
{
    app::App app;
    if (!app.setup())
    {
        ESP_LOGE("Main", "应用初始化失败，程序退出");
        return;
    }
    app.run();
}
