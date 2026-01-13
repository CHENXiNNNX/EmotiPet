#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>

#include "esp_audio_enc.h"
#include "esp_opus_enc.h"

#include "tool/memory/memory.hpp"

namespace app
{
    namespace media
    {
        namespace audio
        {
            namespace process
            {
                namespace opus
                {
                    namespace encode
                    {

                        /**
                         * @brief Opus 编码器配置结构
                         */
                        struct EncoderConfig
                        {
                            int sample_rate;     // 采样率: 8000, 12000, 16000, 24000, 48000
                            int channel;         // 声道数: 1 (单声道) 或 2 (双声道)
                            int bits_per_sample; // 采样位宽: 16
                            int bitrate;         // 比特率 (bps)，或使用 ESP_OPUS_BITRATE_AUTO
                            esp_opus_enc_frame_duration_t frame_duration;   // 帧时长
                            esp_opus_enc_application_t    application_mode; // 应用模式
                            int                           complexity;       // 复杂度: [0, 10]
                            bool                          enable_fec;       // 前向纠错
                            bool                          enable_dtx;       // 不连续传输
                            bool                          enable_vbr;       // 可变比特率

                            /**
                             * @brief 默认配置构造函数
                             */
                            EncoderConfig()
                                : sample_rate(ESP_AUDIO_SAMPLE_RATE_16K), channel(ESP_AUDIO_MONO),
                                  bits_per_sample(ESP_AUDIO_BIT16), bitrate(64000),
                                  frame_duration(ESP_OPUS_ENC_FRAME_DURATION_20_MS),
                                  application_mode(ESP_OPUS_ENC_APPLICATION_VOIP), complexity(0),
                                  enable_fec(false), enable_dtx(false), enable_vbr(false)
                            {
                            }
                        };

                        /**
                         * @brief Opus 编码器类（RAII + 智能指针封装）
                         *
                         * 使用 RAII 原则自动管理编码器生命周期，支持流式编码
                         */
                        class OpusEncoder
                        {
                        public:
                            /**
                             * @brief 构造函数
                             * @param config 编码器配置
                             * @param memory_pool 可选的内存池，用于分配缓冲区
                             */
                            explicit OpusEncoder(const EncoderConfig&      config,
                                                 tool::memory::MemoryPool* memory_pool = nullptr);

                            /**
                             * @brief 析构函数，自动释放资源
                             */
                            ~OpusEncoder();

                            // 禁止拷贝构造和赋值
                            OpusEncoder(const OpusEncoder&)            = delete;
                            OpusEncoder& operator=(const OpusEncoder&) = delete;

                            // 允许移动构造和赋值
                            OpusEncoder(OpusEncoder&& other) noexcept;
                            OpusEncoder& operator=(OpusEncoder&& other) noexcept;

                            /**
                             * @brief 检查编码器是否有效
                             * @return true 如果编码器已初始化，false 否则
                             */
                            bool isValid() const
                            {
                                return encoder_handle_ != nullptr;
                            }

                            /**
                             * @brief 获取输入帧大小和输出缓冲区大小
                             * @param[out] in_size 输入帧大小（字节）
                             * @param[out] out_size 输出缓冲区大小（字节）
                             * @return 成功返回 true，失败返回 false
                             */
                            bool getFrameSize(int* in_size, int* out_size) const;

                            /**
                             * @brief 流式编码
                             *
                             * @param input_data 输入 PCM 数据
                             * @param input_size 输入数据大小（字节）
                             * @param[out] output_data 输出编码数据（由内部缓冲区管理）
                             * @param[out] encoded_bytes 实际编码的字节数
                             * @return 成功返回 true，失败返回 false
                             *
                             * @note 此方法支持流式编码，可以分多次输入数据
                             */
                            bool encode(const uint8_t* input_data, size_t input_size,
                                        const uint8_t** output_data, size_t* encoded_bytes);

                            /**
                             * @brief 刷新编码器缓冲区
                             *
                             * 将内部缓存的剩余数据编码输出
                             *
                             * @param[out] output_data 输出编码数据
                             * @param[out] encoded_bytes 实际编码的字节数
                             * @return 成功返回 true，失败或没有剩余数据返回 false
                             */
                            bool flush(const uint8_t** output_data, size_t* encoded_bytes);

                            /**
                             * @brief 重置编码器
                             *
                             * 重置编码器到初始状态，清空内部缓冲区
                             */
                            bool reset();

                            /**
                             * @brief 设置比特率
                             * @param bitrate 新的比特率（bps）
                             * @return 成功返回 true，失败返回 false
                             */
                            bool setBitrate(int bitrate);

                            /**
                             * @brief 获取编码器信息
                             * @param[out] info 编码器信息结构
                             * @return 成功返回 true，失败返回 false
                             */
                            bool getInfo(esp_audio_enc_info_t* info) const;

                        private:
                            /**
                             * @brief 清理资源
                             */
                            void cleanup();

                            /**
                             * @brief 分配缓冲区
                             * @param size 缓冲区大小
                             * @return 缓冲区指针，失败返回 nullptr
                             */
                            uint8_t* allocateBuffer(size_t size);

                            /**
                             * @brief 释放缓冲区
                             * @param ptr 缓冲区指针
                             */
                            void deallocateBuffer(uint8_t* ptr);

                            void*                     encoder_handle_; // 编码器句柄
                            EncoderConfig             config_;         // 编码器配置
                            tool::memory::MemoryPool* memory_pool_;    // 内存池（可选）

                            // 使用智能指针管理缓冲区
                            std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter> input_buffer_;
                            std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter> output_buffer_;

                            size_t input_buffer_size_;  // 输入缓冲区大小
                            size_t output_buffer_size_; // 输出缓冲区大小
                            size_t input_buffer_used_;  // 输入缓冲区已使用大小（用于流式编码）

                            bool initialized_; // 是否已初始化
                        };

                    } // namespace encode
                } // namespace opus
            } // namespace process
        } // namespace audio
    } // namespace media
} // namespace app
