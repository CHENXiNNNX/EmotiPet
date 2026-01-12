#include "app/i2c/i2c.hpp"
#include "app/media/camera/camera.hpp"
#include "app/config/config.hpp"
#include "app/system/task/task.hpp"
#include "app/system/info/info.hpp"
#include <esp_log.h>
#include <nvs_flash.h>

static const char* TAG = "CameraTest";

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
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化 I2C
    app::i2c::I2c i2c;
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
    cam_cfg.xclk_freq = app::config::CAM_XCLK_FREQ;

    if (!camera.init(&cam_cfg))
    {
        ESP_LOGE(TAG, "摄像头初始化失败");
        return;
    }

    ESP_LOGI(TAG, "摄像头就绪: %s %dx%d %s",
             camera.getSensorName().c_str(),
             camera.getResolution().width, camera.getResolution().height,
             getFormatName(camera.getPixelFormat()));

    // 开始测试循环
    int frame_count = 0;
    
    while (true)
    {
        app::media::camera::FrameBuffer frame;
        if (camera.capture(frame, 2))
        {
            frame_count++;
            ESP_LOGI(TAG, "[#%d] %dx%d %s %.1fKB",
                     frame_count,
                     frame.res.width, frame.res.height,
                     getFormatName(frame.format),
                     frame.len / 1024.0f);
        }
        else
        {
            ESP_LOGE(TAG, "[#%d] 捕获失败", frame_count + 1);
        }

        // 测试镜像翻转
        if (frame_count == 10)
        {
            camera.setHMirror(true);
            ESP_LOGI(TAG, "启用水平镜像");
        }
        else if (frame_count == 20)
        {
            camera.setVFlip(true);
            ESP_LOGI(TAG, "启用垂直翻转");
        }
        else if (frame_count == 30)
        {
            camera.setHMirror(false);
            camera.setVFlip(false);
            ESP_LOGI(TAG, "恢复默认设置");
        }

        app::sys::task::TaskManager::delayMs(3000);
    }
}
