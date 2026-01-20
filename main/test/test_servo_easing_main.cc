#include "app/app.hpp"
#include "app/move/move.hpp"
#include "esp_log.h"
#include "freertos/task.h"

static const char* const TAG = "TestServoEasing";

/**
 * @brief 测试舵机变速转动功能
 * 展示不同的缓动函数效果
 */
extern "C" void app_main(void)
{
    app::App app;
    if (!app.setup())
    {
        ESP_LOGE(TAG, "应用初始化失败，程序退出");
        return;
    }

    // 初始化 PCA9685 舵机驱动
    auto bus_handle = app.getI2CBusHandle();
    if (bus_handle == nullptr)
    {
        ESP_LOGE(TAG, "I2C 总线句柄为空，无法初始化 PCA9685");
        return;
    }

    if (!app::move::PCA9685::init(bus_handle))
    {
        ESP_LOGE(TAG, "PCA9685 初始化失败");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "舵机变速转动测试程序");
    ESP_LOGI(TAG, "========================================");

    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(500));

    // 测试通道（可根据实际硬件修改）
    const uint8_t test_channel = 0; // 测试通道 0 (h1)

    while (true)
    {
        ESP_LOGI(TAG, "\n========== 开始新一轮测试 ==========");

        // 测试1：线性转动（匀速）
        ESP_LOGI(TAG, "线性转动 (LINEAR) - 45° -> 135°, 持续2秒");
        app::move::PCA9685::setServoAngleWithEasing(
            test_channel,
            45.0f, 135.0f, 2000,
            app::move::PCA9685::EasingType::LINEAR
        );
        vTaskDelay(pdMS_TO_TICKS(500));

        // 测试2：缓入（开始慢，逐渐加速）
        ESP_LOGI(TAG, "缓入转动 (EASE_IN) - 135° -> 45°, 持续2秒");
        app::move::PCA9685::setServoAngleWithEasing(
            test_channel,
            135.0f, 45.0f, 2000,
            app::move::PCA9685::EasingType::EASE_IN
        );
        vTaskDelay(pdMS_TO_TICKS(500));

        // 测试3：缓出（开始快，逐渐减速）
        ESP_LOGI(TAG, "缓出转动 (EASE_OUT) - 45° -> 135°, 持续2秒");
        app::move::PCA9685::setServoAngleWithEasing(
            test_channel,
            45.0f, 135.0f, 2000,
            app::move::PCA9685::EasingType::EASE_OUT
        );
        vTaskDelay(pdMS_TO_TICKS(500));

        // 测试4：缓入缓出（开始慢，中间快，结束慢）
        ESP_LOGI(TAG, "缓入缓出转动 (EASE_IN_OUT) - 135° -> 45°, 持续2秒");
        app::move::PCA9685::setServoAngleWithEasing(
            test_channel,
            135.0f, 45.0f, 2000,
            app::move::PCA9685::EasingType::EASE_IN_OUT
        );
        vTaskDelay(pdMS_TO_TICKS(500));

        // 测试5：三次缓入缓出（最平滑）
        ESP_LOGI(TAG, "三次缓入缓出 (EASE_IN_OUT_CUBIC) - 45° -> 135°, 持续2秒");
        app::move::PCA9685::setServoAngleWithEasing(
            test_channel,
            45.0f, 135.0f, 2000,
            app::move::PCA9685::EasingType::EASE_IN_OUT_CUBIC
        );
        vTaskDelay(pdMS_TO_TICKS(1000));

        // 回到中位
        ESP_LOGI(TAG, "回到中位: 90°");
        app::move::PCA9685::setServoAngle(test_channel, 90.0f);
        vTaskDelay(pdMS_TO_TICKS(2000));

        ESP_LOGI(TAG, "========== 本轮测试完成，等待3秒后继续 ==========\n");
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}





























