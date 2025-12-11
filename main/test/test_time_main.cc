#include "time.hpp"

#include "esp_log.h"
#include "system/task/task.hpp"

static const char* const TAG = "TimeTest";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== 时间工具模块测试 ===");
    ESP_LOGI(TAG, "");

    // ==========================================
    // 获取系统启动时间
    // ==========================================
    ESP_LOGI(TAG, "--- 系统启动时间测试 ---");

    int64_t uptime_sec = app::tool::time::uptimeSec();
    int64_t uptime_ms  = app::tool::time::uptimeMs();
    int64_t uptime_us  = app::tool::time::uptimeUs();

    ESP_LOGI(TAG, "系统运行时间: %ld 秒", (long)uptime_sec);
    ESP_LOGI(TAG, "系统运行时间: %ld 毫秒", (long)uptime_ms);
    ESP_LOGI(TAG, "系统运行时间: %ld 微秒", (long)uptime_us);
    ESP_LOGI(TAG, "");

    // ==========================================
    // 获取 Unix 时间戳（需要 NTP 同步）
    // ==========================================
    ESP_LOGI(TAG, "--- Unix 时间戳测试 ---");

    int64_t unix_sec = app::tool::time::unixTimestampSec();
    int64_t unix_ms  = app::tool::time::unixTimestampMs();

    ESP_LOGI(TAG, "Unix 时间戳(秒): %ld", (long)unix_sec);
    ESP_LOGI(TAG, "Unix 时间戳(毫秒): %ld", (long)unix_ms);
    ESP_LOGI(TAG, "注意: 如果未同步 NTP，这个时间可能不准确");
    ESP_LOGI(TAG, "");

    // ==========================================
    // 演示时间变化（延时测试）
    // ==========================================
    ESP_LOGI(TAG, "--- 时间变化测试 ---");

    int64_t start_us = app::tool::time::uptimeUs();
    ESP_LOGI(TAG, "开始时间: %ld 微秒", (long)start_us);

    // 延时 1 秒
    sys::task::TaskManager::delayMs(pdMS_TO_TICKS(1000));

    int64_t end_us     = app::tool::time::uptimeUs();
    int64_t elapsed_us = end_us - start_us;

    ESP_LOGI(TAG, "结束时间: %ld 微秒", (long)end_us);
    constexpr double MS_PER_SEC = 1000.0;
    ESP_LOGI(TAG, "经过时间: %ld 微秒 (%.3f 毫秒)", (long)elapsed_us, elapsed_us / MS_PER_SEC);
    ESP_LOGI(TAG, "");

    // ==========================================
    // 循环打印时间（每 2 秒一次）
    // ==========================================
    ESP_LOGI(TAG, "--- 循环时间打印（每 2 秒）---");
    ESP_LOGI(TAG, "按 Ctrl+C 停止");
    ESP_LOGI(TAG, "");

    int count = 0;
    while (count < 5)
    { // 只打印 5 次，避免无限循环
        int64_t current_uptime = app::tool::time::uptimeMs();
        int64_t current_unix   = app::tool::time::unixTimestampMs();

        ESP_LOGI(TAG, "[%d] 运行时间: %ld ms | Unix: %ld ms", count + 1, (long)current_uptime,
                 (long)current_unix);

        sys::task::TaskManager::delayMs(pdMS_TO_TICKS(2000)); // 延时 2 秒
        count++;
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试完成 ===");
}
