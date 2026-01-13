#pragma once

#include <cstdint>
#include <cstddef>
#include <memory>

#include "esp_audio_dec.h"
#include "esp_opus_dec.h"

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
                    namespace decode
                    {

                        /**
                         * @brief Opus 解码器配置结构
                         */
                        struct DecoderConfig
                        {
                            uint32_t sample_rate; // 采样率: 8000, 12000, 16000, 24000, 48000
                            uint8_t  channel;     // 声道数: 1 (单声道) 或 2 (双声道)
                            esp_opus_dec_frame_duration_t
                                frame_duration;  // 帧时长，或使用
                                                 // ESP_OPUS_DEC_FRAME_DURATION_INVALID 自动检测
                            bool self_delimited; // 是否使用自分隔包（必须与编码器一致）

                            /**
                             * @brief 默认配置构造函数
                             */
                            DecoderConfig()
                                : sample_rate(ESP_AUDIO_SAMPLE_RATE_16K), channel(ESP_AUDIO_MONO),
                                  frame_duration(ESP_OPUS_DEC_FRAME_DURATION_INVALID),
                                  self_delimited(false)
                            {
                            }
                        };

                        /**
                         * @brief Opus 解码器类（RAII + 智能指针封装）
                         *
                         * 使用 RAII 原则自动管理解码器生命周期，支持流式解码
                         */
                        class OpusDecoder
                        {
                        public:
                            /**
                             * @brief 构造函数
                             * @param config 解码器配置
                             * @param memory_pool 可选的内存池，用于分配缓冲区
                             */
                            explicit OpusDecoder(const DecoderConfig&      config,
                                                 tool::memory::MemoryPool* memory_pool = nullptr);

                            /**
                             * @brief 析构函数，自动释放资源
                             */
                            ~OpusDecoder();

                            // 禁止拷贝构造和赋值
                            OpusDecoder(const OpusDecoder&)            = delete;
                            OpusDecoder& operator=(const OpusDecoder&) = delete;

                            // 允许移动构造和赋值
                            OpusDecoder(OpusDecoder&& other) noexcept;
                            OpusDecoder& operator=(OpusDecoder&& other) noexcept;

                            /**
                             * @brief 检查解码器是否有效
                             * @return true 如果解码器已初始化，false 否则
                             */
                            bool isValid() const
                            {
                                return decoder_handle_ != nullptr;
                            }

                            /**
                             * @brief 流式解码
                             *
                             * @param input_data 输入编码数据
                             * @param input_size 输入数据大小（字节）
                             * @param[out] output_data 输出 PCM 数据（由内部缓冲区管理）
                             * @param[out] decoded_bytes 实际解码的字节数
                             * @param[out] consumed_bytes 实际消耗的输入字节数
                             * @return 成功返回 true，失败返回 false
                             *
                             * @note 此方法支持流式解码，可以分多次输入数据
                             *       如果输出缓冲区不足，会返回 false，需要根据 needed_size 重新分配
                             */
                            bool decode(const uint8_t* input_data, size_t input_size,
                                        const int16_t** output_data, size_t* decoded_bytes,
                                        size_t* consumed_bytes);

                            /**
                             * @brief 重置解码器
                             *
                             * 重置解码器到初始状态，清空内部缓冲区
                             */
                            bool reset();

                            /**
                             * @brief 获取解码器信息
                             * @param[out] info 解码器信息结构
                             * @return 成功返回 true，失败返回 false
                             */
                            bool getInfo(esp_audio_dec_info_t* info) const;

                            /**
                             * @brief 获取所需的输出缓冲区大小
                             * @return 输出缓冲区大小（字节），如果未知返回 0
                             */
                            size_t getNeededOutputSize() const
                            {
                                return needed_output_size_;
                            }

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

                            /**
                             * @brief 扩展输出缓冲区
                             * @param new_size 新的缓冲区大小
                             * @return 成功返回 true，失败返回 false
                             */
                            bool expandOutputBuffer(size_t new_size);

                            void*                     decoder_handle_; // 解码器句柄
                            DecoderConfig             config_;         // 解码器配置
                            tool::memory::MemoryPool* memory_pool_;    // 内存池（可选）

                            // 使用智能指针管理缓冲区
                            // 注意：output_buffer_ 使用 uint8_t* 类型，因为 EspHeapDeleter 期望
                            // uint8_t*
                            std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter> input_buffer_;
                            std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter> output_buffer_;

                            size_t input_buffer_size_;  // 输入缓冲区大小
                            size_t output_buffer_size_; // 输出缓冲区大小
                            size_t needed_output_size_; // 所需的输出缓冲区大小

                            bool initialized_; // 是否已初始化
                        };

                    } // namespace decode
                } // namespace opus
            } // namespace process
        } // namespace audio
    } // namespace media
} // namespace app
