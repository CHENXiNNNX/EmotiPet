#pragma once

#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include <mutex>
#include <memory>

#include "audio.hpp"
#include "system/task/task.hpp"

namespace app
{
    namespace media
    {
        namespace audio
        {
            namespace capture
            {
                /**
                 * @brief 音频数据回调函数类型
                 * @param data 音频数据指针（交错格式）
                 * @param samples 每个通道的样本数
                 * @param channels 通道数（4 或 8）
                 * @param sample_rate 采样率
                 */
                using AudioDataCallback = std::function<void(const int16_t* data, size_t samples,
                                                             int channels, int sample_rate)>;

                /**
                 * @brief 音频采集管理器
                 *
                 * 统一管理音频采集，通过回调机制将音频数据分发给多个消费者
                 * 避免多个模块同时调用 audio.read() 导致的资源冲突
                 */
                class AudioCapture
                {
                public:
                    /**
                     * @brief 获取单例实例
                     */
                    static AudioCapture& getInstance();

                    /**
                     * @brief 初始化音频采集
                     * @param audio Audio 实例指针（必须已初始化）
                     * @param frame_size 每次采集的帧大小（每个通道的样本数），默认 160
                     * @return true 成功, false 失败
                     */
                    bool init(Audio* audio, size_t frame_size = 160);

                    /**
                     * @brief 反初始化音频采集
                     */
                    void deinit();

                    /**
                     * @brief 注册音频数据回调
                     * @param callback 回调函数
                     * @return 回调 ID（用于后续取消注册），失败返回 -1
                     */
                    int registerCallback(AudioDataCallback callback);

                    /**
                     * @brief 取消注册回调
                     * @param callback_id 回调 ID（由 registerCallback 返回）
                     * @return true 成功, false 失败（ID 不存在）
                     */
                    bool unregisterCallback(int callback_id);

                    /**
                     * @brief 启动音频采集
                     * @return true 成功, false 失败
                     */
                    bool start();

                    /**
                     * @brief 停止音频采集
                     */
                    void stop();

                    /**
                     * @brief 检查是否正在采集
                     * @return true 正在采集, false 未采集
                     */
                    bool isCapturing() const
                    {
                        return capturing_;
                    }

                    /**
                     * @brief 获取当前采样率
                     * @return 采样率，未初始化返回 0
                     */
                    int getSampleRate() const
                    {
                        return sample_rate_;
                    }

                    /**
                     * @brief 获取当前通道数
                     * @return 通道数，未初始化返回 0
                     */
                    int getChannels() const
                    {
                        return channels_;
                    }

                    /**
                     * @brief 获取帧大小
                     * @return 帧大小（每个通道的样本数）
                     */
                    size_t getFrameSize() const
                    {
                        return frame_size_;
                    }

                private:
                    AudioCapture()                               = default;
                    ~AudioCapture()                              = default;
                    AudioCapture(const AudioCapture&)            = delete;
                    AudioCapture& operator=(const AudioCapture&) = delete;

                    /**
                     * @brief 后台采集任务函数
                     */
                    static void captureTask(void* param);

                    /**
                     * @brief 执行采集循环
                     */
                    void captureLoop();

                    Audio* audio_       = nullptr; // Audio 实例指针
                    size_t frame_size_  = 160;     // 每次采集的帧大小
                    int    sample_rate_ = 0;       // 采样率
                    int    channels_    = 0;       // 通道数

                    std::unique_ptr<sys::task::Task> capture_task_; // 采集任务
                    bool                             initialized_ = false;
                    bool                             capturing_   = false;

                    // 回调管理
                    struct CallbackInfo
                    {
                        int               callback_id;
                        AudioDataCallback callback;
                    };
                    std::vector<CallbackInfo> callbacks_;
                    mutable std::mutex        callbacks_mutex_;
                    int                       next_callback_id_ = 1; // 回调 ID 计数器
                };

            } // namespace capture
        } // namespace audio
    } // namespace media
} // namespace app
