#include "app/i2c/i2c.hpp"
#include "app/media/camera/camera.hpp"
#include "app/media/camera/process/jpeg/encode/jpeg_enc.hpp"
#include "app/config/config.hpp"
#include "app/system/task/task.hpp"
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>

static const char* TAG = "JPEGTest";

// 获取格式名称
const char* getFormatName(app::media::camera::PixelFormat format)
{
    switch (format)
    {
    case app::media::camera::PixelFormat::RGB565:
        return "RGB565";
    case app::media::camera::PixelFormat::RGB24:
        return "RGB24";
    case app::media::camera::PixelFormat::YUV422:
        return "YUV422";
    case app::media::camera::PixelFormat::YUV420:
        return "YUV420";
    case app::media::camera::PixelFormat::JPEG:
        return "JPEG";
    default:
        return "UNKNOWN";
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== YUV422 → JPEG 编码测试 ===");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化 I2C
    app::i2c::I2c    i2c;
    app::i2c::Config i2c_cfg;
    i2c_cfg.sda_pin = app::config::I2C_SDA;
    i2c_cfg.scl_pin = app::config::I2C_SCL;
    i2c_cfg.port    = I2C_NUM_1;

    if (!i2c.init(&i2c_cfg))
    {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }

    // 初始化摄像头
    app::media::camera::Camera camera;
    app::media::camera::Config cam_cfg;
    cam_cfg.i2c_handle = i2c.getBusHandle();
    cam_cfg.xclk_freq  = app::config::CAM_XCLK_FREQ;

    if (!camera.init(&cam_cfg))
    {
        ESP_LOGE(TAG, "摄像头初始化失败");
        return;
    }

    // 检查格式
    if (camera.getPixelFormat() != app::media::camera::PixelFormat::YUV422)
    {
        ESP_LOGE(TAG, "当前格式不是 YUV422: %s", getFormatName(camera.getPixelFormat()));
        return;
    }

    ESP_LOGI(TAG, "摄像头就绪: %s %dx%d YUV422", camera.getSensorName().c_str(),
             camera.getResolution().width, camera.getResolution().height);

    // 预热
    for (int i = 0; i < 3; i++)
    {
        app::media::camera::FrameBuffer frame;
        camera.capture(frame, 0);
        app::sys::task::TaskManager::delayMs(100);
    }

    // 开始测试循环
    int test_count = 0;

    while (true)
    {
        test_count++;
        int64_t start_time = esp_timer_get_time();

        // 捕获帧
        app::media::camera::FrameBuffer frame;
        if (!camera.capture(frame, 2))
        {
            ESP_LOGE(TAG, "[#%d] 捕获失败", test_count);
            app::sys::task::TaskManager::delayMs(3000);
            continue;
        }

        // JPEG 编码
        app::media::camera::process::jpeg::encode::EncodeConfig encode_config;
        encode_config.quality   = 80;
        encode_config.use_psram = true;

        int64_t encode_start = esp_timer_get_time();
        auto    jpeg_result  = app::media::camera::process::jpeg::encode::encodeYUV422ToJPEG(
            frame.data, frame.res.width, frame.res.height, &encode_config);
        int64_t encode_time = esp_timer_get_time() - encode_start;
        int64_t total_time  = esp_timer_get_time() - start_time;

        if (jpeg_result)
        {
            float compression_ratio = (jpeg_result.len() * 100.0f) / frame.len;
            ESP_LOGI(
                TAG, "[#%d] 成功 | YUV: %.1fKB → JPEG: %.1fKB (%.1f%%) | 编码: %ums | 总计: %ums",
                test_count, frame.len / 1024.0f, jpeg_result.len() / 1024.0f, compression_ratio,
                (unsigned int)(encode_time / 1000), (unsigned int)(total_time / 1000));
        }
        else
        {
            ESP_LOGE(TAG, "[#%d] JPEG 编码失败", test_count);
        }

        app::sys::task::TaskManager::delayMs(3000);
    }
}
