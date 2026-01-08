#include "common/i2c/i2c.hpp"
#include "media/camera/camera.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* const TAG = "Main";

extern "C" void app_main(void)
{
    // 创建 I2C 实例
    app::common::i2c::I2c i2c;

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

    // 初始化摄像头
    ESP_LOGI(TAG, "初始化摄像头...");
    app::media::camera::Camera camera;
    app::media::camera::Config cam_cfg;
    cam_cfg.i2c_master_handle = i2c.getBusHandle();

    if (camera.init(&cam_cfg))
    {
        ESP_LOGI(TAG, "摄像头初始化成功");
    }
    else
    {
        ESP_LOGE(TAG, "摄像头初始化失败");
    }
}
