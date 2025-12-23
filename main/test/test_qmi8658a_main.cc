#include "common/i2c/i2c.hpp"
#include "common/i2c/qmi8658a/qmi8658a.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* const TAG = "Main";

using namespace app::common::i2c;
using namespace app::common::i2c::qmi8658a;

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== QMI8658A 姿态测试 ===");

    I2c i2c;
    if (!i2c.init())
    {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }

    Qmi8658a                           imu;
    app::common::i2c::qmi8658a::Config cfg;
    cfg.i2c_addr    = QMI8658A_ADDR_LOW;
    cfg.accel_range = AccelRange::RANGE_8G;
    cfg.gyro_range  = GyroRange::RANGE_512DPS;
    cfg.accel_odr   = AccelOdr::ODR_500HZ;
    cfg.gyro_odr    = GyroOdr::ODR_500HZ;

    if (!imu.init(i2c.getBusHandle(), &cfg))
    {
        ESP_LOGE(TAG, "IMU 初始化失败");
        return;
    }

    ESP_LOGI(TAG, "开始读取传感器数据...\n");

    SensorData data;
    int        count = 0;

    while (true)
    {
        if (imu.read(data))
        {
            count++;
            ESP_LOGI(TAG, "[%d] ========================================", count);
            ESP_LOGI(TAG, "  加速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f m/s²", data.accel_x, data.accel_y,
                     data.accel_z);
            ESP_LOGI(TAG, "  角速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f rad/s", data.gyro_x, data.gyro_y,
                     data.gyro_z);
            ESP_LOGI(TAG, "  姿态:   Roll=%+7.1f°  Pitch=%+7.1f°  Yaw=%+7.1f°", data.roll, data.pitch,
                     data.yaw);
            ESP_LOGI(TAG, "  温度:   %.1f°C", data.temperature);
            ESP_LOGI(TAG, "========================================\n");
        }

        vTaskDelay(pdMS_TO_TICKS(200)); // 5Hz 更新
    }
}
