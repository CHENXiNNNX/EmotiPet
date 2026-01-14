#include "i2c/i2c.hpp"
#include "media/audio/audio.hpp"
#include "media/audio/process/afe/afe.hpp"
#include "assets/assets.hpp"
#include "system/event/event.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_heap_caps.h"
#include <cstddef>
#include <cstdint>

static const char* const TAG = "VAD_Test";

// VAD 状态统计
static struct
{
    bool     is_speaking;
    uint32_t speech_count;
    uint32_t silence_count;
    uint32_t state_change_count;
} vad_stats = {false, 0, 0, 0};

// VAD 状态回调函数
void on_vad_state_change(bool is_speaking)
{
    if (is_speaking != vad_stats.is_speaking)
    {
        vad_stats.is_speaking = is_speaking;
        vad_stats.state_change_count++;

        if (is_speaking)
        {
            vad_stats.speech_count++;
            ESP_LOGI(TAG, "========== 检测到语音 (第 %lu 次) ==========", vad_stats.speech_count);
        }
        else
        {
            vad_stats.silence_count++;
            ESP_LOGI(TAG, "========== 检测到静音 (第 %lu 次) ==========", vad_stats.silence_count);
        }
    }
}

// 音频输出回调函数（可选，用于调试）
void on_audio_output(const int16_t* data, size_t samples)
{
    // 可以在这里处理处理后的音频数据
    // 例如：编码、发送等
    (void)data;
    (void)samples;
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "VAD 测试程序启动");
    ESP_LOGI(TAG, "========================================");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化事件系统
    auto& event_mgr = app::sys::event::EventManager::getInstance();
    if (!event_mgr.init())
    {
        ESP_LOGE(TAG, "事件系统初始化失败");
        return;
    }

    // 初始化 I2C
    app::i2c::I2c    i2c;
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
    app::media::audio::Config audio_cfg; // 使用完整命名空间避免歧义
    audio_cfg.i2c_master_handle  = i2c_bus;
    audio_cfg.input_sample_rate  = 16000;
    audio_cfg.output_sample_rate = 16000;
    audio_cfg.input_reference    = false; // 不使用参考信号

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
    audio.setOutputVolume(100);
    audio.enableOutput(false); // 测试时关闭输出，只测试输入和 VAD
    audio.enableInput(true);

    // 初始化 Assets（用于加载模型）
    app::assets::Assets& assets      = app::assets::Assets::getInstance();
    srmodel_list_t*      models_list = nullptr;

    if (assets.init())
    {
        ESP_LOGI(TAG, "Assets 分区初始化成功");
        if (assets.apply())
        {
            models_list = assets.getModelsList();
            if (models_list != nullptr)
            {
                ESP_LOGI(TAG, "模型加载成功，模型数量: %d", models_list->num);
            }
            else
            {
                ESP_LOGW(TAG, "未加载模型，将使用 WebRTC VAD");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Assets 应用失败，将使用 WebRTC VAD");
        }
    }
    else
    {
        ESP_LOGW(TAG, "Assets 分区初始化失败，将使用 WebRTC VAD");
    }

    // 配置 AFE
    // 注意：AFE_TYPE_VC 模式只支持单麦克风通道，多通道输入时只会选择第一个通道
    // 对于 4 个麦克风，我们需要先做波束成形，转换为单声道
    app::media::audio::process::afe::Config afe_config;
    afe_config.input_format     = "M"; // 单麦克风（从 4 个麦克风波束成形得到）
    afe_config.sample_rate      = 16000;
    afe_config.enable_aec       = false;              // 不使用 AEC
    afe_config.enable_vad       = true;               // 启用 VAD
    afe_config.enable_ns        = false;              // 可选：启用噪声抑制
    afe_config.enable_agc       = false;              // 可选：启用自动增益控制
    afe_config.vad_mode         = VAD_MODE_0;         // VAD 模式（0 最宽松，4 最严格）
    afe_config.vad_min_noise_ms = 100;                // 最小静音时长（毫秒）
    afe_config.afe_type         = AFE_TYPE_VC;        // 语音通信模式
    afe_config.afe_mode         = AFE_MODE_HIGH_PERF; // 高性能模式
    afe_config.models_list      = models_list;        // 使用 Assets 加载的模型

    // 创建 AFE 实例
    app::media::audio::process::afe::Afe afe(afe_config);

    if (!afe.isValid())
    {
        ESP_LOGE(TAG, "AFE 初始化失败");
        return;
    }

    ESP_LOGI(TAG, "AFE 初始化成功");
    ESP_LOGI(TAG, "  - 输入帧大小: %u 样本", (unsigned int)afe.getFeedSize());
    ESP_LOGI(TAG, "  - 输出帧大小: %u 样本", (unsigned int)afe.getFetchSize());
    ESP_LOGI(TAG, "  - 通道数: %d", afe.getChannelNum());
    ESP_LOGI(TAG, "  - 采样率: %d Hz", afe.getSampleRate());

    // 设置 VAD 状态回调
    afe.setVadStateCallback(on_vad_state_change);

    // 设置音频输出回调（可选）
    afe.setAudioOutputCallback(on_audio_output);

    // 获取 AFE 需要的输入帧大小
    size_t afe_feed_size = afe.getFeedSize();
    if (afe_feed_size == 0)
    {
        ESP_LOGE(TAG, "无法获取 AFE 输入帧大小");
        return;
    }

    // 根据麦克风数量确定缓冲区大小
    const int    mic_channels      = 4;                            // 4 个麦克风输入
    const int    afe_channels      = 1;                            // AFE_TYPE_VC 只需要单声道输入
    const size_t audio_buffer_size = afe_feed_size * mic_channels; // 4 通道原始数据
    const size_t afe_buffer_size   = afe_feed_size * afe_channels; // 1 通道波束成形后数据

    int16_t* audio_buffer = static_cast<int16_t*>(heap_caps_malloc(
        audio_buffer_size * sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));
    int16_t* afe_buffer   = static_cast<int16_t*>(
        heap_caps_malloc(afe_buffer_size * sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));

    if (audio_buffer == nullptr || afe_buffer == nullptr)
    {
        ESP_LOGE(TAG, "分配音频缓冲区失败");
        if (audio_buffer)
            heap_caps_free(audio_buffer);
        if (afe_buffer)
            heap_caps_free(afe_buffer);
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始 VAD 测试");
    ESP_LOGI(TAG, "请对着麦克风说话，观察 VAD 检测结果");
    ESP_LOGI(TAG, "========================================");

    uint32_t feed_count          = 0;
    uint32_t fetch_count         = 0;
    uint32_t fetch_success_count = 0;

    // 主循环：采集音频 -> 输入 AFE -> 获取 VAD 结果
    while (true)
    {
        // 从音频设备读取数据
        int samples_read = audio.read(audio_buffer, static_cast<int>(afe_feed_size));

        if (samples_read > 0)
        {
            feed_count++;

            // 波束成形：将 4 个麦克风转换为单声道
            // 方法：4 个麦克风的平均值（简单波束成形）
            for (size_t i = 0; i < static_cast<size_t>(samples_read); i++)
            {
                // 单声道：4 个麦克风的平均
                int32_t sum = static_cast<int32_t>(audio_buffer[(i * 4) + 0]) +
                              static_cast<int32_t>(audio_buffer[(i * 4) + 1]) +
                              static_cast<int32_t>(audio_buffer[(i * 4) + 2]) +
                              static_cast<int32_t>(audio_buffer[(i * 4) + 3]);
                afe_buffer[i] = static_cast<int16_t>(sum / 4);
            }

            // 输入到 AFE（单声道格式）
            if (afe.feed(afe_buffer, static_cast<size_t>(samples_read)))
            {
                // 尝试获取处理结果（非阻塞）
                fetch_count++;
                if (afe.fetch(0)) // 0 表示非阻塞
                {
                    fetch_success_count++;
                }
            }

            // 每 1000 次显示一次统计信息
            if (feed_count % 1000 == 0)
            {
                ESP_LOGI(TAG,
                         "统计: feed=%lu, fetch=%lu (成功=%lu), "
                         "语音=%lu次, 静音=%lu次, 状态变化=%lu次",
                         feed_count, fetch_count, fetch_success_count, vad_stats.speech_count,
                         vad_stats.silence_count, vad_stats.state_change_count);
            }
        }
        else
        {
            // 如果读取失败，稍作延迟
            app::sys::task::TaskManager::delayMs(10);
        }

        // 短暂延迟，避免 CPU 占用过高
        app::sys::task::TaskManager::delayMs(1);
    }

    // 清理（实际上不会执行到这里，因为 while(true)）
    heap_caps_free(audio_buffer);
    heap_caps_free(afe_buffer);
}
