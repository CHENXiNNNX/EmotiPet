#pragma once

#include <cstddef>
#include <memory>
#include <functional>
#include <string>

#include "esp_afe_sr_iface.h"
#include "esp_afe_config.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "tool/memory/memory.hpp"

namespace app
{
    namespace media
    {
        namespace audio
        {
            namespace process
            {
                namespace afe
                {

                    /**
                     * @brief AFE 配置结构
                     */
                    struct Config
                    {
                        std::string input_format =
                            "MM"; // 输入格式，例如 "MM" 表示双麦克风，"MMR" 表示双麦克风+参考通道
                        int        sample_rate = 16000;      // 采样率，通常为 16000
                        bool       enable_aec  = false;      // 是否启用 AEC（声学回声消除）
                        bool       enable_vad  = true;       // 是否启用 VAD（语音活动检测）
                        bool       enable_ns   = false;      // 是否启用 NS（噪声抑制）
                        bool       enable_agc  = false;      // 是否启用 AGC（自动增益控制）
                        vad_mode_t vad_mode    = VAD_MODE_0; // VAD 模式：VAD_MODE_0 到 VAD_MODE_4
                        int        vad_min_noise_ms = 100;   // VAD 最小静音时长（毫秒）
                        afe_type_t afe_type = AFE_TYPE_VC;   // AFE 类型：AFE_TYPE_SR 或 AFE_TYPE_VC
                        afe_mode_t afe_mode =
                            AFE_MODE_HIGH_PERF; // AFE 模式：AFE_MODE_LOW_COST 或 AFE_MODE_HIGH_PERF
                        srmodel_list_t* models_list =
                            nullptr; // 模型列表（可选，nullptr 表示使用默认模型）
                    };

                    /**
                     * @brief AFE 配置删除器（RAII）
                     */
                    struct AfeConfigDeleter
                    {
                        void operator()(afe_config_t* ptr) const
                        {
                            if (ptr != nullptr)
                            {
                                afe_config_free(ptr);
                            }
                        }
                    };

                    /**
                     * @brief AFE 类（RAII + 智能指针封装）
                     *
                     * 使用 RAII 原则自动管理 AFE 生命周期，支持 VAD 检测和音频处理
                     */
                    class Afe
                    {
                    public:
                        /**
                         * @brief VAD 状态回调函数类型
                         * @param is_speaking true 表示检测到语音，false 表示静音
                         */
                        using VadStateCallback = std::function<void(bool is_speaking)>;

                        /**
                         * @brief 音频输出回调函数类型
                         * @param data 处理后的音频数据（PCM 16-bit）
                         * @param samples 样本数（单声道）
                         */
                        using AudioOutputCallback =
                            std::function<void(const int16_t* data, size_t samples)>;

                        /**
                         * @brief 构造函数
                         * @param config AFE 配置
                         * @param memory_pool 可选的内存池，用于分配缓冲区
                         */
                        explicit Afe(const Config&             config,
                                     tool::memory::MemoryPool* memory_pool = nullptr);

                        /**
                         * @brief 析构函数，自动释放资源
                         */
                        ~Afe();

                        // 禁止拷贝构造和赋值
                        Afe(const Afe&)            = delete;
                        Afe& operator=(const Afe&) = delete;

                        // 允许移动构造和赋值
                        Afe(Afe&& other) noexcept;
                        Afe& operator=(Afe&& other) noexcept;

                        /**
                         * @brief 检查 AFE 是否有效
                         * @return true 如果 AFE 已初始化，false 否则
                         */
                        bool isValid() const
                        {
                            return afe_data_ != nullptr && afe_iface_ != nullptr;
                        }

                        /**
                         * @brief 获取输入帧大小（每个通道的样本数）
                         * @return 输入帧大小，失败返回 0
                         */
                        size_t getFeedSize() const;

                        /**
                         * @brief 获取输出帧大小（样本数）
                         * @return 输出帧大小，失败返回 0
                         */
                        size_t getFetchSize() const;

                        /**
                         * @brief 获取通道数
                         * @return 通道数，失败返回 0
                         */
                        int getChannelNum() const;

                        /**
                         * @brief 获取采样率
                         * @return 采样率，失败返回 0
                         */
                        int getSampleRate() const;

                        /**
                         * @brief 输入音频数据
                         *
                         * @param data 输入音频数据（交错格式，例如双麦克风：[mic1, mic2, mic1,
                         * mic2, ...]）
                         * @param samples 每个通道的样本数
                         * @return 成功返回 true，失败返回 false
                         */
                        bool feed(const int16_t* data, size_t samples);

                        /**
                         * @brief 获取处理后的音频数据（阻塞）
                         *
                         * @param timeout_ms 超时时间（毫秒），0 表示不阻塞，portMAX_DELAY
                         * 表示永久阻塞
                         * @return 成功返回 true，失败或超时返回 false
                         *
                         * @note 此方法会触发 VAD 状态回调和音频输出回调
                         */
                        bool fetch(int timeout_ms = 0);

                        /**
                         * @brief 重置缓冲区
                         *
                         * 清空内部缓冲区，重置处理状态
                         */
                        void reset();

                        /**
                         * @brief 启用/禁用 VAD
                         * @param enable true 启用，false 禁用
                         */
                        void enableVad(bool enable);

                        /**
                         * @brief 启用/禁用 AEC
                         * @param enable true 启用，false 禁用
                         */
                        void enableAec(bool enable);

                        /**
                         * @brief 设置 VAD 状态回调
                         * @param callback VAD 状态回调函数
                         */
                        void setVadStateCallback(VadStateCallback callback)
                        {
                            vad_state_callback_ = callback;
                        }

                        /**
                         * @brief 设置音频输出回调
                         * @param callback 音频输出回调函数
                         */
                        void setAudioOutputCallback(AudioOutputCallback callback)
                        {
                            audio_output_callback_ = callback;
                        }

                    private:
                        /**
                         * @brief 清理资源
                         */
                        void cleanup();

                        /**
                         * @brief 初始化 AFE
                         * @return 成功返回 true，失败返回 false
                         */
                        bool initialize();

                        /**
                         * @brief 处理 fetch 结果
                         * @param result fetch 结果
                         */
                        void processFetchResult(const afe_fetch_result_t* result);

                        Config                    config_;      // AFE 配置
                        tool::memory::MemoryPool* memory_pool_; // 内存池（可选）
                        std::unique_ptr<afe_config_t, AfeConfigDeleter>
                                                  afe_config_; // AFE 配置（智能指针管理）
                        const esp_afe_sr_iface_t* afe_iface_;  // AFE 接口
                        esp_afe_sr_data_t*        afe_data_;   // AFE 数据

                        VadStateCallback    vad_state_callback_;    // VAD 状态回调
                        AudioOutputCallback audio_output_callback_; // 音频输出回调

                        bool is_speaking_; // 当前 VAD 状态
                        bool initialized_; // 是否已初始化
                    };

                } // namespace afe
            } // namespace process
        } // namespace audio
    } // namespace media
} // namespace app
