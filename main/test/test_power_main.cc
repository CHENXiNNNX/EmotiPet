#include "uuid.hpp"
#include "system/power/power.hpp"
#include "system/task/task.hpp"

#include <memory>

#include "esp_log.h"
#include "esp_sleep.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* const TAG = "Main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Power Management Test ===");

    // 检查唤醒原因
    esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
    if (wakeup_cause == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        ESP_LOGI(TAG, "首次启动");
    }
    else if (wakeup_cause == ESP_SLEEP_WAKEUP_TIMER)
    {
        ESP_LOGI(TAG, "从 Deep Sleep 定时器唤醒");
    }
    else
    {
        ESP_LOGI(TAG, "从低功耗唤醒，原因: %d", wakeup_cause);
    }

    auto& power = app::sys::power::PowerManager::getInstance();

    // 设置退出低功耗的回调函数
    power.setExitLowPowerCallback(
        []()
        {
            ESP_LOGI(TAG, "退出低功耗模式，执行恢复操作");
            // 这里可以添加你的恢复逻辑，比如：
            // - 重新初始化外设
            // - 恢复 WiFi 连接
            // - 更新显示等
        });

    // 测试 Light Sleep
    if (wakeup_cause == ESP_SLEEP_WAKEUP_UNDEFINED)
    {
        ESP_LOGI(TAG, "--- 测试 Light Sleep ---");
        // 配置定时器唤醒（3秒后唤醒）
        esp_sleep_enable_timer_wakeup(3 * 1000000ULL); // 3秒 = 3000000微秒

        ESP_LOGI(TAG, "准备进入 Light Sleep（3秒后唤醒）...");
        app::sys::task::TaskManager::delayMs(1000); // 等待1秒，让日志输出完成

        // 进入 Light Sleep
        power.enterLightSleep();

        // 唤醒后会继续执行这里
        ESP_LOGI(TAG, "已从 Light Sleep 唤醒");
        ESP_LOGI(TAG, "");
    }

    // 测试 Deep Sleep
    ESP_LOGI(TAG, "--- 测试 Deep Sleep ---");
    // 配置定时器唤醒（5秒后唤醒）
    esp_sleep_enable_timer_wakeup(5 * 1000000ULL); // 5秒 = 5000000微秒

    ESP_LOGI(TAG, "准备进入 Deep Sleep（5秒后唤醒，将重启）...");
    app::sys::task::TaskManager::delayMs(2000); // 等待2秒，让日志输出完成

    // 进入 Deep Sleep（会重启，不会返回）
    power.enterDeepSleep();

    // 不会执行到这里
    ESP_LOGI(TAG, "这行不会执行");
}
