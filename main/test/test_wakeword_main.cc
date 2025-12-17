#include "assets/assets.hpp"
#include "common/i2c/i2c.hpp"
#include "media/audio/audio.hpp"
#include "media/audio/wakeword/wakeword.hpp"
#include "system/event/event.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* const TAG = "Main";

using namespace app::assets;
using namespace app::common::i2c;
using namespace app::media::audio;
using namespace app::media::audio::wakeword;
using namespace app::sys::event;

extern WakeWord* createCustomWakeWord();

static int       g_detection_count = 0;
static WakeWord* g_wakeword        = nullptr;
static Audio*    g_audio           = nullptr;

// 音频处理任务
void audioTask(void* param)
{
    size_t               feed_size = g_wakeword->getFeedSize();
    std::vector<int16_t> audio_buffer(feed_size);
    uint32_t             frame_count   = 0;
    uint32_t             last_log_time = 0;

    while (true)
    {
        int samples = g_audio->read(audio_buffer.data(), feed_size);

        if (samples <= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        audio_buffer.resize(samples);
        g_wakeword->feed(audio_buffer);
        frame_count++;

        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
        if (current_time - last_log_time >= 10000)
        {
            float duration = frame_count * feed_size / 16000.0f;
            ESP_LOGI(TAG, "运行中 (%.0f 秒音频)", duration);
            last_log_time = current_time;
        }

        if (!g_wakeword->isRunning())
        {
            vTaskDelay(pdMS_TO_TICKS(1000));
            g_wakeword->start();
        }
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  EmotiPet 唤醒词功能测试");
    ESP_LOGI(TAG, "========================================");

    // NVS 初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // ========== 1. 事件系统测试 ==========
    ESP_LOGI(TAG, "\n[测试 1] 事件系统");
    auto& event_mgr = EventManager::getInstance();
    if (!event_mgr.init())
    {
        ESP_LOGE(TAG, "  事件系统初始化失败");
        return;
    }
    ESP_LOGI(TAG, "  事件系统初始化成功");

    // 注册唤醒词事件
    event_mgr.registerHandler(WAKEWORD_EVENT_BASE, WAKEWORD_EVENT_DETECTED,
                              [](esp_event_base_t, EventId, const EventData& data)
                              {
                                  const auto* event = static_cast<const WakeWordEventData*>(data.data);
                                  g_detection_count++;
                                  ESP_LOGI(TAG, "");
                                  ESP_LOGI(TAG, "  ★ 检测到唤醒词: %s", event->text);
                                  ESP_LOGI(TAG, "    置信度: %.2f, 累计次数: %d",
                                           event->probability, g_detection_count);
                              });

    event_mgr.registerHandler(WAKEWORD_EVENT_BASE, WAKEWORD_EVENT_STARTED,
                              [](esp_event_base_t, EventId, const EventData&)
                              { ESP_LOGI(TAG, "  [事件] 唤醒词检测已启动"); });

    event_mgr.registerHandler(WAKEWORD_EVENT_BASE, WAKEWORD_EVENT_STOPPED,
                              [](esp_event_base_t, EventId, const EventData&)
                              { ESP_LOGI(TAG, "  [事件] 唤醒词检测已停止"); });
    ESP_LOGI(TAG, "  事件处理器注册成功");

    // ========== 2. Assets 测试 ==========
    ESP_LOGI(TAG, "\n[测试 2] Assets 系统");
    auto& assets = Assets::getInstance();
    if (!assets.init())
    {
        ESP_LOGE(TAG, "  Assets 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "  Assets 分区加载成功");

    if (!assets.apply())
    {
        ESP_LOGE(TAG, "  Assets 配置应用失败");
        return;
    }
    ESP_LOGI(TAG, "  Assets 配置应用成功");

    srmodel_list_t* models = assets.getModelsList();
    if (models == nullptr || models->num == 0)
    {
        ESP_LOGE(TAG, "  未找到 SR 模型");
        return;
    }
    ESP_LOGI(TAG, "  加载了 %d 个 SR 模型", models->num);

    // ========== 3. 硬件初始化 ==========
    ESP_LOGI(TAG, "\n[测试 3] 硬件初始化");

    I2c                      i2c;
    app::common::i2c::Config i2c_cfg;
    i2c_cfg.port    = I2C_NUM_1;
    i2c_cfg.sda_pin = GPIO_NUM_17;
    i2c_cfg.scl_pin = GPIO_NUM_18;
    if (!i2c.init(&i2c_cfg))
    {
        ESP_LOGE(TAG, "  I2C 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "  I2C 初始化成功");

    app::media::audio::Config audio_cfg;
    audio_cfg.i2c_master_handle  = i2c.getBusHandle();
    audio_cfg.input_sample_rate  = 16000;
    audio_cfg.output_sample_rate = 16000;
    audio_cfg.input_reference    = false;
    audio_cfg.mclk               = GPIO_NUM_16;
    audio_cfg.ws                 = GPIO_NUM_45;
    audio_cfg.bclk               = GPIO_NUM_9;
    audio_cfg.din                = GPIO_NUM_10;
    audio_cfg.dout               = GPIO_NUM_8;
    audio_cfg.pa_pin             = GPIO_NUM_48;
    audio_cfg.es8389_addr        = ES8389_CODEC_DEFAULT_ADDR;
    audio_cfg.es7210_addr        = ES7210_CODEC_DEFAULT_ADDR;

    static Audio audio;
    g_audio = &audio;
    if (!audio.init(&audio_cfg))
    {
        ESP_LOGE(TAG, "  音频初始化失败");
        return;
    }
    audio.setOutputVolume(70);
    audio.enableInput(true);
    ESP_LOGI(TAG, "  音频系统初始化成功");

    // ========== 4. 唤醒词检测器测试 ==========
    ESP_LOGI(TAG, "\n[测试 4] 唤醒词检测器");

    g_wakeword = createCustomWakeWord();
    if (g_wakeword == nullptr)
    {
        ESP_LOGE(TAG, "  创建唤醒词检测器失败");
        return;
    }
    ESP_LOGI(TAG, "  唤醒词检测器创建成功");

    if (!g_wakeword->init(models, 16000, 1))
    {
        ESP_LOGE(TAG, "  唤醒词检测器初始化失败");
        delete g_wakeword;
        return;
    }
    ESP_LOGI(TAG, "  唤醒词检测器初始化成功");

    // ========== 5. 命令词管理测试 ==========
    ESP_LOGI(TAG, "\n[测试 5] 命令词管理 (中文模型)");

    // 添加中文唤醒词
    g_wakeword->addCommand("ni hao xiao zhi", "你好小智", "wake");
    ESP_LOGI(TAG, "  添加: 你好小智");

    g_wakeword->addCommand("ni hao xiao ke", "你好小可", "wake");
    ESP_LOGI(TAG, "  添加: 你好小可");

    g_wakeword->addCommand("xiao ai tong xue", "小爱同学", "wake");
    ESP_LOGI(TAG, "  添加: 小爱同学");

    // 测试删除命令词
    if (g_wakeword->removeCommand("小爱同学"))
    {
        ESP_LOGI(TAG, "  删除: 小爱同学");
    }

    // ========== 6. 模型切换测试 ==========
    ESP_LOGI(TAG, "\n[测试 6] 模型切换");

    // 切换到英文模型
    if (g_wakeword->switchModel("en"))
    {
        ESP_LOGI(TAG, "  切换到英文模型");

        g_wakeword->addCommand("hello", "Hello", "wake");
        ESP_LOGI(TAG, "  添加英文唤醒词: Hello");

        g_wakeword->addCommand("hi there", "Hi There", "wake");
        ESP_LOGI(TAG, "  添加英文唤醒词: Hi There");
    }
    else
    {
        ESP_LOGW(TAG, "  英文模型不可用，跳过英文测试");
    }

    // 切换回中文模型（之前的中文命令词应该自动恢复）
    if (g_wakeword->switchModel("cn"))
    {
        ESP_LOGI(TAG, "  切换回中文模型 (命令词已恢复)");
    }

    // ========== 7. 清除命令词测试 ==========
    ESP_LOGI(TAG, "\n[测试 7] 清除命令词");

    // 先切换到英文清除
    g_wakeword->switchModel("en");
    g_wakeword->clearCommands();
    ESP_LOGI(TAG, "  清除英文命令词");

    // 重新添加一个英文唤醒词
    g_wakeword->addCommand("hey robot", "Hey Robot", "wake");
    ESP_LOGI(TAG, "  重新添加: Hey Robot");

    // 切换回中文
    g_wakeword->switchModel("cn");
    ESP_LOGI(TAG, "  切换回中文模型");

    // ========== 8. 获取信息测试 ==========
    ESP_LOGI(TAG, "\n[测试 8] 信息获取");

    size_t feed_size = g_wakeword->getFeedSize();
    ESP_LOGI(TAG, "  Feed 大小: %zu 采样点 (%.1f ms)", feed_size, feed_size * 1000.0f / 16000.0f);

    ESP_LOGI(TAG, "  运行状态: %s", g_wakeword->isRunning() ? "运行中" : "已停止");

    const std::string& last_word = g_wakeword->getLastDetectedWakeWord();
    ESP_LOGI(TAG, "  上次检测: %s", last_word.empty() ? "(无)" : last_word.c_str());

    // ========== 9. 启动检测 ==========
    ESP_LOGI(TAG, "\n[测试 9] 启动唤醒词检测");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  当前模式: 中文");
    ESP_LOGI(TAG, "  可用唤醒词:");
    ESP_LOGI(TAG, "    - 你好小智 (ni hao xiao zhi)");
    ESP_LOGI(TAG, "    - 你好小可 (ni hao xiao ke)");
    ESP_LOGI(TAG, "========================================");

    g_wakeword->start();

    // 创建音频处理任务
    xTaskCreatePinnedToCore(audioTask, "audio_task", 8192, nullptr, 5, nullptr, 1);

    // 主任务保持运行
    while (true)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }

    delete g_wakeword;
}
