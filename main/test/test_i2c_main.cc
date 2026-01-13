#include "i2c/i2c.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* const TAG = "Main";

using namespace app::i2c;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== I2C Test Start ===");

    // 创建 I2C 实例
    I2c i2c;

    // 使用默认配置初始化 I2C
    ESP_LOGI(TAG, "Initializing I2C with default config...");
    if (!i2c.init())
    {
        ESP_LOGE(TAG, "Failed to initialize I2C");
        return;
    }

    // 等待一下让 I2C 稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    // 扫描 I2C 总线
    ESP_LOGI(TAG, "Starting I2C bus scan...");
    int device_count = i2c.scan(200);

    if (device_count > 0)
    {
        ESP_LOGI(TAG, "Scan completed: Found %d device(s)", device_count);
    }
    else if (device_count == 0)
    {
        ESP_LOGW(TAG, "Scan completed: No devices found");
    }
    else
    {
        ESP_LOGE(TAG, "Scan failed");
    }
}
