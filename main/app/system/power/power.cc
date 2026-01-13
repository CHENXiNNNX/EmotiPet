#include "power.hpp"

#include "esp_log.h"

static const char* const TAG = "POWER";

namespace app
{
    namespace sys
    {
        namespace power
        {

            PowerManager& PowerManager::getInstance()
            {
                static PowerManager instance;
                return instance;
            }

            void PowerManager::setExitLowPowerCallback(ExitLowPowerCallback callback)
            {
                exit_callback_ = callback;
            }

            void PowerManager::enterLightSleep()
            {
                ESP_LOGI(TAG, "进入 Light Sleep");
                esp_light_sleep_start();
                if (exit_callback_)
                {
                    exit_callback_();
                }
            }

            void PowerManager::enterDeepSleep()
            {
                ESP_LOGI(TAG, "进入 Deep Sleep");
                esp_deep_sleep_start();
            }

        } // namespace power
    } // namespace sys
} // namespace app
