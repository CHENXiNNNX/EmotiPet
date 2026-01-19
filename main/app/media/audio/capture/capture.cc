#include "capture.hpp"

#include <cstddef>

#include "esp_log.h"
#include "esp_heap_caps.h"
#include "freertos/task.h"

static const char* const TAG = "AudioCapture";

namespace app
{
    namespace media
    {
        namespace audio
        {
            namespace capture
            {
                AudioCapture& AudioCapture::getInstance()
                {
                    static AudioCapture instance;
                    return instance;
                }

                bool AudioCapture::init(Audio* audio, size_t frame_size)
                {
                    if (initialized_)
                    {
                        ESP_LOGW(TAG, "AudioCapture 已经初始化");
                        return true;
                    }

                    if (audio == nullptr)
                    {
                        ESP_LOGE(TAG, "Audio 指针为空");
                        return false;
                    }

                    if (!audio->isInitialized())
                    {
                        ESP_LOGE(TAG, "Audio 未初始化");
                        return false;
                    }

                    audio_      = audio;
                    frame_size_ = frame_size;

                    // 从 Audio 类获取配置信息
                    sample_rate_ = audio_->getInputSampleRate();
                    channels_    = audio_->getInputChannels();

                    if (sample_rate_ <= 0 || channels_ <= 0)
                    {
                        ESP_LOGE(TAG, "无法获取 Audio 配置信息 (采样率=%d, 通道数=%d)",
                                 sample_rate_, channels_);
                        return false;
                    }

                    initialized_ = true;
                    ESP_LOGI(TAG, "AudioCapture 初始化成功 (帧大小=%u, 采样率=%d, 通道数=%d)",
                             (unsigned int)frame_size_, sample_rate_, channels_);
                    return true;
                }

                void AudioCapture::deinit()
                {
                    stop();

                    std::lock_guard<std::mutex> lock(callbacks_mutex_);
                    callbacks_.clear();
                    audio_       = nullptr;
                    initialized_ = false;
                    sample_rate_ = 0;
                    channels_    = 0;

                    ESP_LOGI(TAG, "AudioCapture 已反初始化");
                }

                int AudioCapture::registerCallback(AudioDataCallback callback)
                {
                    if (!callback)
                    {
                        ESP_LOGE(TAG, "回调函数为空");
                        return -1;
                    }

                    std::lock_guard<std::mutex> lock(callbacks_mutex_);

                    CallbackInfo info;
                    info.callback_id = next_callback_id_++;
                    info.callback    = callback;
                    callbacks_.push_back(info);

                    ESP_LOGI(TAG, "注册音频数据回调 (ID=%d, 当前回调数=%u)", info.callback_id,
                             (unsigned int)callbacks_.size());
                    return info.callback_id;
                }

                bool AudioCapture::unregisterCallback(int callback_id)
                {
                    std::lock_guard<std::mutex> lock(callbacks_mutex_);

                    for (auto it = callbacks_.begin(); it != callbacks_.end(); ++it)
                    {
                        if (it->callback_id == callback_id)
                        {
                            callbacks_.erase(it);
                            ESP_LOGI(TAG, "取消注册音频数据回调 (ID=%d, 剩余回调数=%u)",
                                     callback_id, (unsigned int)callbacks_.size());
                            return true;
                        }
                    }

                    ESP_LOGW(TAG, "未找到回调 ID=%d", callback_id);
                    return false;
                }

                bool AudioCapture::start()
                {
                    if (!initialized_)
                    {
                        ESP_LOGE(TAG, "AudioCapture 未初始化");
                        return false;
                    }

                    if (capturing_)
                    {
                        ESP_LOGW(TAG, "音频采集已在运行");
                        return true;
                    }

                    if (audio_ == nullptr)
                    {
                        ESP_LOGE(TAG, "Audio 指针为空");
                        return false;
                    }

                    // 启用音频输入
                    bool input_enabled = false;
                    for (int retry = 0; retry < 3; retry++)
                    {
                        if (audio_->enableInput(true))
                        {
                            input_enabled = true;
                            break;
                        }

                        if (retry < 2)
                        {
                            ESP_LOGW(TAG, "启用音频输入失败，%d 毫秒后重试...", (retry + 1) * 100);
                            vTaskDelay(pdMS_TO_TICKS((retry + 1) * 100));
                        }
                    }

                    // 创建采集任务
                    sys::task::Config task_config =
                        sys::task::Config::createLarge("audio_capture", sys::task::Priority::HIGH);
                    task_config.stack_size = 32 * 1024; 
                    task_config.core_id    = 1;     // 绑定到核心 1，避免与其他任务冲突

                    capture_task_ =
                        std::make_unique<sys::task::Task>(captureTask, task_config, this);

                    if (!capture_task_)
                    {
                        ESP_LOGE(TAG, "创建采集任务对象失败");
                        audio_->enableInput(false);
                        return false;
                    }

                    if (!capture_task_->start())
                    {
                        ESP_LOGE(TAG, "启动采集任务失败");
                        capture_task_.reset();
                        audio_->enableInput(false);
                        return false;
                    }

                    capturing_ = true;
                    ESP_LOGI(TAG, "音频采集已启动");
                    return true;
                }

                void AudioCapture::stop()
                {
                    if (!capturing_)
                    {
                        return;
                    }

                    capturing_ = false;

                    // 停止任务
                    if (capture_task_)
                    {
                        capture_task_->destroy();
                        capture_task_.reset();
                    }

                    // 禁用音频输入
                    if (audio_)
                    {
                        audio_->enableInput(false);
                    }

                    ESP_LOGI(TAG, "音频采集已停止");
                }

                void AudioCapture::captureTask(void* param)
                {
                    auto* capture = static_cast<AudioCapture*>(param);
                    if (capture == nullptr)
                    {
                        ESP_LOGE(TAG, "任务参数为空");
                        return;
                    }

                    capture->captureLoop();
                }

                void AudioCapture::captureLoop()
                {
                    if (audio_ == nullptr)
                    {
                        ESP_LOGE(TAG, "Audio 指针为空");
                        return;
                    }

                    // 分配音频缓冲区
                    // 4 通道：frame_size * 4，8 通道：frame_size * 8
                    const size_t buffer_size  = frame_size_ * channels_;
                    int16_t*     audio_buffer = static_cast<int16_t*>(heap_caps_malloc(
                        buffer_size * sizeof(int16_t), MALLOC_CAP_8BIT | MALLOC_CAP_INTERNAL));

                    if (audio_buffer == nullptr)
                    {
                        ESP_LOGE(TAG, "分配音频缓冲区失败");
                        return;
                    }

                    ESP_LOGI(TAG, "音频采集任务启动 (缓冲区大小=%u 样本)",
                             (unsigned int)buffer_size);

                    uint32_t capture_count = 0;
                    uint32_t error_count   = 0;

                    // 采集循环
                    while (capturing_)
                    {
                        // 从 Audio 读取数据
                        int samples_read =
                            audio_->read(audio_buffer, static_cast<int>(frame_size_));

                        if (samples_read > 0)
                        {
                            capture_count++;

                            // 分发数据给所有注册的回调
                            {
                                std::lock_guard<std::mutex> lock(callbacks_mutex_);

                                for (const auto& cb_info : callbacks_)
                                {
                                    if (cb_info.callback)
                                    {
                                        try
                                        {
                                            cb_info.callback(audio_buffer,
                                                             static_cast<size_t>(samples_read),
                                                             channels_, sample_rate_);
                                        }
                                        catch (...)
                                        {
                                            ESP_LOGE(TAG, "回调函数执行异常 (ID=%d)",
                                                     cb_info.callback_id);
                                        }
                                    }
                                }
                            }

                            // 每 1000 次显示一次统计信息（调试用）
                            if (capture_count % 1000 == 0)
                            {
                                std::lock_guard<std::mutex> lock(callbacks_mutex_);
                                ESP_LOGD(TAG, "采集统计: 成功=%lu, 错误=%lu, 回调数=%u",
                                         capture_count, error_count,
                                         (unsigned int)callbacks_.size());
                            }
                        }
                        else
                        {
                            error_count++;

                            // 如果连续失败，稍作延迟
                            if (error_count % 100 == 0)
                            {
                                ESP_LOGW(TAG, "音频读取失败次数: %lu", error_count);
                            }

                            vTaskDelay(pdMS_TO_TICKS(10));
                        }

                        // 短暂延迟，避免 CPU 占用过高
                        vTaskDelay(pdMS_TO_TICKS(1));
                    }

                    // 清理缓冲区
                    heap_caps_free(audio_buffer);
                    ESP_LOGI(TAG, "音频采集任务结束");
                }

            } // namespace capture
        } // namespace audio
    } // namespace media
} // namespace app
