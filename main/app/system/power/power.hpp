#pragma once

#include <functional>

#include "esp_sleep.h"

namespace app
{
    namespace sys
    {
        namespace power
        {

            /**
             * @brief 电源管理类
             *
             * 提供进入和退出低功耗模式的接口
             */
            class PowerManager
            {
            public:
                using ExitLowPowerCallback = std::function<void()>; // 退出低功耗回调函数类型

                /**
                 * @brief 获取电源管理器实例
                 * @return 电源管理器引用
                 */
                static PowerManager& getInstance();

                /**
                 * @brief 设置退出低功耗回调函数
                 * @param callback 回调函数
                 */
                void setExitLowPowerCallback(ExitLowPowerCallback callback);

                /**
                 * @brief 进入 Light Sleep
                 */
                void enterLightSleep();

                /**
                 * @brief 进入 Deep Sleep
                 *
                 * 注意：此函数不会返回，芯片会重启
                 */
                void enterDeepSleep();

            private:
                PowerManager()                               = default;
                ~PowerManager()                              = default;
                PowerManager(const PowerManager&)            = delete;
                PowerManager& operator=(const PowerManager&) = delete;

                ExitLowPowerCallback exit_callback_;
            };

        } // namespace power
    }     // namespace sys
} // namespace app
