#include "assets/assets.hpp"
#include "common/i2c/i2c.hpp"
#include "media/audio/audio.hpp"
#include "media/audio/wakeword/wakeword.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* const TAG = "Main";

using namespace app::assets;
using namespace app::common::i2c;
using namespace app::media::audio;
using namespace app::media::audio::wakeword;

// 外部工厂函数
extern WakeWord* createCustomWakeWord();

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  EmotiPet 唤醒词检测测试");
    ESP_LOGI(TAG, "========================================");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 步骤 1: 初始化 Assets
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[1/5] 初始化 Assets 系统...");
    auto& assets = Assets::getInstance();
    
    if (!assets.init())
    {
        ESP_LOGE(TAG, "Assets 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "Assets 分区加载成功");

    if (!assets.apply())
    {
        ESP_LOGE(TAG, "Assets 配置应用失败");
        return;
    }
    ESP_LOGI(TAG, "Assets 配置应用成功");

    srmodel_list_t* models = assets.getModelsList();
    if (models == nullptr || models->num == 0)
    {
        ESP_LOGE(TAG, "未找到 SR 模型");
        return;
    }
    ESP_LOGI(TAG, "共加载 %d 个 SR 模型", models->num);

    // 步骤 2: 初始化 I2C
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[2/5] 初始化 I2C 总线...");
    I2c i2c;
    app::common::i2c::Config i2c_cfg;
    i2c_cfg.port    = I2C_NUM_1;
    i2c_cfg.sda_pin = GPIO_NUM_17;
    i2c_cfg.scl_pin = GPIO_NUM_18;

    if (!i2c.init(&i2c_cfg))
    {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "I2C 总线初始化成功");

    // 步骤 3: 初始化音频系统
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[3/5] 初始化音频系统...");
    app::media::audio::Config audio_cfg;
    audio_cfg.i2c_master_handle  = i2c.getBusHandle();
    audio_cfg.input_sample_rate  = 16000;
    audio_cfg.output_sample_rate = 16000;
    audio_cfg.input_reference    = false;

    // GPIO 配置
    audio_cfg.mclk = GPIO_NUM_16;
    audio_cfg.ws   = GPIO_NUM_45;
    audio_cfg.bclk = GPIO_NUM_9;
    audio_cfg.din  = GPIO_NUM_10;
    audio_cfg.dout = GPIO_NUM_8;

    // Codec 地址配置
    audio_cfg.pa_pin      = GPIO_NUM_48;
    audio_cfg.es8389_addr = ES8389_CODEC_DEFAULT_ADDR;
    audio_cfg.es7210_addr = ES7210_CODEC_DEFAULT_ADDR;

    Audio audio;
    if (!audio.init(&audio_cfg))
    {
        ESP_LOGE(TAG, "音频系统初始化失败");
        return;
    }
    ESP_LOGI(TAG, "音频硬件初始化成功");

    // 设置音量并启用输入
    audio.setOutputVolume(70);
    audio.enableInput(true);
    ESP_LOGI(TAG, "麦克风已启用");

    // 步骤 4: 初始化唤醒词检测器
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[4/5] 初始化唤醒词检测器...");
    WakeWord* wakeword = createCustomWakeWord();
    if (wakeword == nullptr)
    {
        ESP_LOGE(TAG, "创建唤醒词检测器失败");
        return;
    }

    if (!wakeword->init(models, 16000, 1))
    {
        ESP_LOGE(TAG, "唤醒词检测器初始化失败");
        delete wakeword;
        return;
    }
    ESP_LOGI(TAG, "唤醒词检测器初始化成功");

    // 动态添加唤醒词
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "添加自定义唤醒词...");
    wakeword->addCommand("ni hao xiao zhi", "你好小智", "wake");
    wakeword->addCommand("ni hao xiao ke", "你好小可", "wake");
    wakeword->addCommand("hi esp", "Hi ESP", "wake");
    ESP_LOGI(TAG, "唤醒词添加完成");

    // 获取 feed 参数
    size_t feed_size = wakeword->getFeedSize();
    if (feed_size == 0)
    {
        ESP_LOGE(TAG, "无效的 feed size");
        delete wakeword;
        return;
    }
    ESP_LOGI(TAG, "Feed 大小: %u 采样点 (%.1f ms)", 
             feed_size, feed_size * 1000.0f / 16000.0f);

    // 步骤 5: 设置检测回调
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "[5/5] 设置检测回调...");
    int detection_count = 0;
    
    wakeword->setWakeWordDetected([&](const std::string& wake_word) {
        detection_count++;
        ESP_LOGI(TAG, "");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "       检测到唤醒词！");
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "  唤醒词: %s", wake_word.c_str());
        ESP_LOGI(TAG, "  检测次数: %d", detection_count);
        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "");
    });
    ESP_LOGI(TAG, "检测回调设置完成");

    // 启动检测
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  系统就绪！");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "请说出以下唤醒词进行测试:");
    ESP_LOGI(TAG, "  - 你好小智 (ni hao xiao zhi)");
    ESP_LOGI(TAG, "  - 你好小可 (ni hao xiao ke)");
    ESP_LOGI(TAG, "  - Hi ESP");
    ESP_LOGI(TAG, "");

    wakeword->start();

    // 主循环
    std::vector<int16_t> audio_buffer(feed_size);
    uint32_t frame_count   = 0;
    uint32_t last_log_time = 0;

    while (true)
    {
        // 读取音频数据
        int samples = audio.read(audio_buffer.data(), feed_size);
        
        if (samples <= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 调整 buffer 大小
        audio_buffer.resize(samples);

        // 喂给唤醒词检测器
        wakeword->feed(audio_buffer);

        // 计数
        frame_count++;

        // 每 5 秒打印一次状态
        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_time - last_log_time >= 5000)
        {
            float duration = frame_count * feed_size / 16000.0f;
            ESP_LOGI(TAG, "运行中... (已处理 %lu 帧, %.1f 秒音频)", 
                     frame_count, duration);
            last_log_time = current_time;
        }

        // 如果检测到唤醒词，自动重启检测
        if (!wakeword->isRunning())
        {
            ESP_LOGI(TAG, "");
            ESP_LOGI(TAG, "等待 1 秒后重新启动检测...");
            vTaskDelay(pdMS_TO_TICKS(1000));
            
            wakeword->start();
            ESP_LOGI(TAG, "唤醒词检测已重新启动");
            ESP_LOGI(TAG, "");
        }
    }

    // 清理（实际不会到达）
    delete wakeword;
}
