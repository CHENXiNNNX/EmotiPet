#include "i2c/i2c.hpp"
#include "media/audio/audio.hpp"
#include "media/audio/capture/capture.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include <cstddef>

static const char* const TAG = "Main";

// 音频采集统计
static struct {
    uint32_t callback_count;
    uint32_t total_samples;
    uint32_t playback_count;
} capture_stats = {0, 0, 0};

// Audio 实例指针（用于回环播放）
static app::media::audio::Audio* g_audio = nullptr;

// 音频数据回调函数（回环测试：采集后立即播放）
void onAudioData(const int16_t* data, size_t samples, int channels, int sample_rate)
{
    capture_stats.callback_count++;
    capture_stats.total_samples += samples;

    // 回环播放：将采集的音频数据播放出来
    if (g_audio != nullptr)
    {
        // 波束成形：将多通道转换为单声道（用于播放）
        // 方法：所有通道的平均值
        int16_t mono_buffer[160]; // 假设最大帧大小 160
        size_t mono_samples = samples;
        
        if (channels > 1)
        {
            // 多通道转单声道
            for (size_t i = 0; i < samples && i < 160; i++)
            {
                int32_t sum = 0;
                for (int ch = 0; ch < channels; ch++)
                {
                    sum += static_cast<int32_t>(data[(i * channels) + ch]);
                }
                mono_buffer[i] = static_cast<int16_t>(sum / channels);
            }
        }
        else
        {
            // 已经是单声道，直接复制
            for (size_t i = 0; i < samples && i < 160; i++)
            {
                mono_buffer[i] = data[i];
            }
        }

        // 播放音频（回环）
        int written = g_audio->write(mono_buffer, static_cast<int>(mono_samples));
        if (written > 0)
        {
            capture_stats.playback_count++;
        }
    }

    // 每 100 次回调显示一次统计信息
    if (capture_stats.callback_count % 100 == 0)
    {
        ESP_LOGI(TAG, "音频采集回调统计: 回调次数=%lu, 总样本数=%lu, 播放次数=%lu, "
                 "通道数=%d, 采样率=%d Hz",
                 capture_stats.callback_count, capture_stats.total_samples,
                 capture_stats.playback_count, channels, sample_rate);
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "AudioCapture 测试程序");
    ESP_LOGI(TAG, "========================================");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化事件循环
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    // 初始化 I2C
    app::i2c::I2c i2c;
    app::i2c::Config i2c_cfg;
    i2c_cfg.port    = I2C_NUM_1;
    i2c_cfg.sda_pin = GPIO_NUM_17;
    i2c_cfg.scl_pin = GPIO_NUM_18;

    if (!i2c.init(&i2c_cfg))
    {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }

    i2c_master_bus_handle_t i2c_bus = i2c.getBusHandle();
    if (i2c_bus == nullptr)
    {
        ESP_LOGE(TAG, "I2C bus 句柄为空");
        return;
    }

    ESP_LOGI(TAG, "I2C 初始化成功");
    i2c.scan(200);

    // 初始化 Audio
    app::media::audio::Config audio_cfg;
    audio_cfg.i2c_master_handle  = i2c_bus;
    audio_cfg.input_sample_rate  = 16000;
    audio_cfg.output_sample_rate = 16000;
    audio_cfg.input_reference    = false;

    audio_cfg.mclk = GPIO_NUM_16;
    audio_cfg.ws   = GPIO_NUM_45;
    audio_cfg.bclk = GPIO_NUM_9;
    audio_cfg.din  = GPIO_NUM_10;
    audio_cfg.dout = GPIO_NUM_8;

    audio_cfg.pa_pin      = GPIO_NUM_48;
    audio_cfg.es8311_addr = ES8311_CODEC_DEFAULT_ADDR;
    audio_cfg.es7210_addr = ES7210_CODEC_DEFAULT_ADDR;

    app::media::audio::Audio audio;
    if (!audio.init(&audio_cfg))
    {
        ESP_LOGE(TAG, "Audio 初始化失败");
        return;
    }

    ESP_LOGI(TAG, "Audio 初始化成功");
    ESP_LOGI(TAG, "  - 输入采样率: %d Hz", audio.getInputSampleRate());
    ESP_LOGI(TAG, "  - 输出采样率: %d Hz", audio.getOutputSampleRate());
    ESP_LOGI(TAG, "  - 输入通道数: %d", audio.getInputChannels());
    ESP_LOGI(TAG, "  - 参考信号模式: %s", audio.isInputReference() ? "是" : "否");

    // 启用音频输出（用于回环测试）
    audio.setOutputVolume(70); // 音量设置为 70，避免反馈过大
    audio.enableOutput(true);
    ESP_LOGI(TAG, "音频输出已启用（音量=70）");

    // 保存 Audio 实例指针（用于回环播放）
    g_audio = &audio;

    // 初始化 AudioCapture
    auto& capture = app::media::audio::capture::AudioCapture::getInstance();
    
    if (!capture.init(&audio, 160))
    {
        ESP_LOGE(TAG, "AudioCapture 初始化失败");
        return;
    }

    ESP_LOGI(TAG, "AudioCapture 初始化成功");
    ESP_LOGI(TAG, "  - 帧大小: %u 样本", (unsigned int)capture.getFrameSize());
    ESP_LOGI(TAG, "  - 采样率: %d Hz", capture.getSampleRate());
    ESP_LOGI(TAG, "  - 通道数: %d", capture.getChannels());

    // 注册音频数据回调
    int callback_id = capture.registerCallback(onAudioData);
    if (callback_id < 0)
    {
        ESP_LOGE(TAG, "注册音频数据回调失败");
        return;
    }

    ESP_LOGI(TAG, "音频数据回调已注册 (ID=%d)", callback_id);

    // 启动音频采集
    if (!capture.start())
    {
        ESP_LOGE(TAG, "启动音频采集失败");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "音频采集已启动（回环测试模式）");
    ESP_LOGI(TAG, "请对着麦克风说话，应该能听到回声");
    ESP_LOGI(TAG, "========================================");

    // 主循环：定期显示统计信息
    while (true)
    {
        app::sys::task::TaskManager::delayMs(5000); // 5秒间隔

        ESP_LOGI(TAG, "========================================");
        ESP_LOGI(TAG, "采集状态: %s", capture.isCapturing() ? "运行中" : "已停止");
        ESP_LOGI(TAG, "回调统计: 总回调次数=%lu, 总样本数=%lu, 播放次数=%lu",
                 capture_stats.callback_count, capture_stats.total_samples,
                 capture_stats.playback_count);
        
        if (capture_stats.callback_count > 0)
        {
            float avg_samples_per_callback = (float)capture_stats.total_samples / 
                                            (float)capture_stats.callback_count;
            float playback_ratio = (float)capture_stats.playback_count / 
                                  (float)capture_stats.callback_count * 100.0f;
            ESP_LOGI(TAG, "平均每次回调样本数: %.2f", avg_samples_per_callback);
            ESP_LOGI(TAG, "播放成功率: %.2f%%", playback_ratio);
        }
        ESP_LOGI(TAG, "========================================");
    }
}
