#include "opus_enc.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_opus_enc.h"
#include "esp_heap_caps.h"

static const char* const TAG = "OpusEncoder";

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

                        OpusEncoder::OpusEncoder(const EncoderConfig&      config,
                                                 tool::memory::MemoryPool* memory_pool)
                            : encoder_handle_(nullptr), config_(config), memory_pool_(memory_pool),
                              input_buffer_(nullptr), output_buffer_(nullptr),
                              input_buffer_size_(0), output_buffer_size_(0), input_buffer_used_(0),
                              initialized_(false)
                        {
                            // 注册 Opus 编码器（如果尚未注册）
                            esp_opus_enc_register();

                            // 转换为 ESP 编码器配置
                            esp_opus_enc_config_t esp_cfg = {};
                            esp_cfg.sample_rate           = config_.sample_rate;
                            esp_cfg.channel               = config_.channel;
                            esp_cfg.bits_per_sample       = config_.bits_per_sample;
                            esp_cfg.bitrate               = config_.bitrate;
                            esp_cfg.frame_duration        = config_.frame_duration;
                            esp_cfg.application_mode      = config_.application_mode;
                            esp_cfg.complexity            = config_.complexity;
                            esp_cfg.enable_fec            = config_.enable_fec;
                            esp_cfg.enable_dtx            = config_.enable_dtx;
                            esp_cfg.enable_vbr            = config_.enable_vbr;

                            // 打开编码器
                            esp_audio_err_t ret = esp_opus_enc_open(
                                &esp_cfg, sizeof(esp_opus_enc_config_t), &encoder_handle_);
                            if (ret != ESP_AUDIO_ERR_OK)
                            {
                                ESP_LOGE(TAG, "打开 Opus 编码器失败: %d", ret);
                                return;
                            }

                            // 获取帧大小信息
                            int in_size  = 0;
                            int out_size = 0;
                            ret = esp_opus_enc_get_frame_size(encoder_handle_, &in_size, &out_size);
                            if (ret != ESP_AUDIO_ERR_OK)
                            {
                                ESP_LOGE(TAG, "获取帧大小失败: %d", ret);
                                esp_opus_enc_close(encoder_handle_);
                                encoder_handle_ = nullptr;
                                return;
                            }

                            // 分配输入和输出缓冲区（使用 16 倍帧大小以支持流式编码）
                            // 增加缓冲区以避免 AFE 高速输出时的溢出
                            input_buffer_size_  = in_size * 16;
                            output_buffer_size_ = out_size * 16;

                            input_buffer_ =
                                std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter>(
                                    allocateBuffer(input_buffer_size_));
                            output_buffer_ =
                                std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter>(
                                    allocateBuffer(output_buffer_size_));

                            if (input_buffer_ == nullptr || output_buffer_ == nullptr)
                            {
                                ESP_LOGE(TAG, "分配缓冲区失败");
                                cleanup();
                                return;
                            }

                            initialized_ = true;
                            ESP_LOGI(
                                TAG,
                                "Opus 编码器初始化成功: sample_rate=%d, channel=%d, bitrate=%d",
                                config_.sample_rate, config_.channel, config_.bitrate);
                        }

                        OpusEncoder::~OpusEncoder()
                        {
                            cleanup();
                        }

                        OpusEncoder::OpusEncoder(OpusEncoder&& other) noexcept
                            : encoder_handle_(other.encoder_handle_), config_(other.config_),
                              memory_pool_(other.memory_pool_),
                              input_buffer_(std::move(other.input_buffer_)),
                              output_buffer_(std::move(other.output_buffer_)),
                              input_buffer_size_(other.input_buffer_size_),
                              output_buffer_size_(other.output_buffer_size_),
                              input_buffer_used_(other.input_buffer_used_),
                              initialized_(other.initialized_)
                        {
                            other.encoder_handle_ = nullptr;
                            other.initialized_    = false;
                        }

                        OpusEncoder& OpusEncoder::operator=(OpusEncoder&& other) noexcept
                        {
                            if (this != &other)
                            {
                                cleanup();

                                encoder_handle_     = other.encoder_handle_;
                                config_             = other.config_;
                                memory_pool_        = other.memory_pool_;
                                input_buffer_       = std::move(other.input_buffer_);
                                output_buffer_      = std::move(other.output_buffer_);
                                input_buffer_size_  = other.input_buffer_size_;
                                output_buffer_size_ = other.output_buffer_size_;
                                input_buffer_used_  = other.input_buffer_used_;
                                initialized_        = other.initialized_;

                                other.encoder_handle_ = nullptr;
                                other.initialized_    = false;
                            }
                            return *this;
                        }

                        bool OpusEncoder::getFrameSize(int* in_size, int* out_size) const
                        {
                            if (!isValid() || in_size == nullptr || out_size == nullptr)
                            {
                                return false;
                            }

                            esp_audio_err_t ret =
                                esp_opus_enc_get_frame_size(encoder_handle_, in_size, out_size);
                            return (ret == ESP_AUDIO_ERR_OK);
                        }

                        bool OpusEncoder::encode(const uint8_t* input_data, size_t input_size,
                                                 const uint8_t** output_data, size_t* encoded_bytes)
                        {
                            if (!isValid() || input_data == nullptr || output_data == nullptr ||
                                encoded_bytes == nullptr)
                            {
                                return false;
                            }

                            // 将输入数据追加到输入缓冲区
                            if (input_buffer_used_ + input_size > input_buffer_size_)
                            {
                                ESP_LOGE(TAG, "输入缓冲区溢出: used=%u, size=%u, new=%u",
                                         (unsigned int)input_buffer_used_,
                                         (unsigned int)input_buffer_size_,
                                         (unsigned int)input_size);
                                return false;
                            }

                            memcpy(input_buffer_.get() + input_buffer_used_, input_data,
                                   input_size);
                            input_buffer_used_ += input_size;

                            // 获取帧大小
                            int frame_in_size  = 0;
                            int frame_out_size = 0;
                            if (!getFrameSize(&frame_in_size, &frame_out_size))
                            {
                                return false;
                            }

                            // 如果输入缓冲区中的数据不足以编码一帧，返回成功但不输出数据
                            if (input_buffer_used_ < static_cast<size_t>(frame_in_size))
                            {
                                *output_data   = nullptr;
                                *encoded_bytes = 0;
                                return true; // 数据不足，但这是正常情况（流式编码）
                            }

                            // 准备输入帧
                            esp_audio_enc_in_frame_t in_frame = {};
                            in_frame.buffer                   = input_buffer_.get();
                            // 确保输入长度是帧长度的整数倍
                            size_t encodeable_len =
                                (input_buffer_used_ / frame_in_size) * frame_in_size;
                            in_frame.len = static_cast<uint32_t>(encodeable_len);

                            // 准备输出帧
                            esp_audio_enc_out_frame_t out_frame = {};
                            out_frame.buffer                    = output_buffer_.get();
                            out_frame.len           = static_cast<uint32_t>(output_buffer_size_);
                            out_frame.encoded_bytes = 0;
                            out_frame.pts           = 0;

                            // 执行编码
                            esp_audio_err_t ret =
                                esp_opus_enc_process(encoder_handle_, &in_frame, &out_frame);
                            if (ret != ESP_AUDIO_ERR_OK && ret != ESP_AUDIO_ERR_DATA_LACK)
                            {
                                ESP_LOGE(TAG, "编码失败: %d", ret);
                                return false;
                            }

                            // 如果编码成功，更新输入缓冲区
                            if (ret == ESP_AUDIO_ERR_OK && out_frame.encoded_bytes > 0)
                            {
                                // 计算已使用的输入数据大小（假设按帧大小对齐）
                                size_t consumed =
                                    (input_buffer_used_ / frame_in_size) * frame_in_size;

                                // 移动剩余数据到缓冲区开头
                                if (consumed < input_buffer_used_)
                                {
                                    size_t remaining = input_buffer_used_ - consumed;
                                    memmove(input_buffer_.get(), input_buffer_.get() + consumed,
                                            remaining);
                                    input_buffer_used_ = remaining;
                                }
                                else
                                {
                                    input_buffer_used_ = 0;
                                }

                                *output_data   = output_buffer_.get();
                                *encoded_bytes = out_frame.encoded_bytes;
                                return true;
                            }

                            // 数据不足，等待更多数据
                            *output_data   = nullptr;
                            *encoded_bytes = 0;
                            return true;
                        }

                        bool OpusEncoder::flush(const uint8_t** output_data, size_t* encoded_bytes)
                        {
                            if (!isValid() || output_data == nullptr || encoded_bytes == nullptr)
                            {
                                return false;
                            }

                            // 如果没有剩余数据，返回 false
                            if (input_buffer_used_ == 0)
                            {
                                *output_data   = nullptr;
                                *encoded_bytes = 0;
                                return false;
                            }

                            // 获取帧大小
                            int frame_in_size  = 0;
                            int frame_out_size = 0;
                            if (!getFrameSize(&frame_in_size, &frame_out_size))
                            {
                                return false;
                            }

                            // 准备输入帧（使用剩余的所有数据）
                            esp_audio_enc_in_frame_t in_frame = {};
                            in_frame.buffer                   = input_buffer_.get();
                            in_frame.len = static_cast<uint32_t>(input_buffer_used_);

                            // 准备输出帧
                            esp_audio_enc_out_frame_t out_frame = {};
                            out_frame.buffer                    = output_buffer_.get();
                            out_frame.len           = static_cast<uint32_t>(output_buffer_size_);
                            out_frame.encoded_bytes = 0;
                            out_frame.pts           = 0;

                            // 执行编码（即使数据不足一帧也尝试编码）
                            esp_audio_err_t ret =
                                esp_opus_enc_process(encoder_handle_, &in_frame, &out_frame);

                            // 清空输入缓冲区
                            input_buffer_used_ = 0;

                            if (ret == ESP_AUDIO_ERR_OK && out_frame.encoded_bytes > 0)
                            {
                                *output_data   = output_buffer_.get();
                                *encoded_bytes = out_frame.encoded_bytes;
                                return true;
                            }

                            *output_data   = nullptr;
                            *encoded_bytes = 0;
                            return false;
                        }

                        bool OpusEncoder::reset()
                        {
                            if (!isValid())
                            {
                                return false;
                            }

                            esp_audio_err_t ret = esp_opus_enc_reset(encoder_handle_);
                            if (ret == ESP_AUDIO_ERR_OK)
                            {
                                input_buffer_used_ = 0;
                                return true;
                            }
                            return false;
                        }

                        bool OpusEncoder::setBitrate(int bitrate)
                        {
                            if (!isValid())
                            {
                                return false;
                            }

                            esp_audio_err_t ret =
                                esp_opus_enc_set_bitrate(encoder_handle_, bitrate);
                            if (ret == ESP_AUDIO_ERR_OK)
                            {
                                config_.bitrate = bitrate;
                                return true;
                            }
                            return false;
                        }

                        bool OpusEncoder::getInfo(esp_audio_enc_info_t* info) const
                        {
                            if (!isValid() || info == nullptr)
                            {
                                return false;
                            }

                            esp_audio_err_t ret = esp_opus_enc_get_info(encoder_handle_, info);
                            return (ret == ESP_AUDIO_ERR_OK);
                        }

                        void OpusEncoder::cleanup()
                        {
                            if (encoder_handle_ != nullptr)
                            {
                                esp_opus_enc_close(encoder_handle_);
                                encoder_handle_ = nullptr;
                            }

                            input_buffer_.reset();
                            output_buffer_.reset();
                            input_buffer_used_ = 0;
                            initialized_       = false;
                        }

                        uint8_t* OpusEncoder::allocateBuffer(size_t size)
                        {
                            if (memory_pool_ != nullptr)
                            {
                                return static_cast<uint8_t*>(memory_pool_->allocate(size));
                            }
                            else
                            {
                                // 使用 heap_caps_malloc 分配内存
                                uint8_t* ptr = static_cast<uint8_t*>(
                                    heap_caps_malloc(size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                                if (ptr == nullptr)
                                {
                                    ptr = static_cast<uint8_t*>(
                                        heap_caps_malloc(size, MALLOC_CAP_DEFAULT));
                                }
                                return ptr;
                            }
                        }

                        void OpusEncoder::deallocateBuffer(uint8_t* ptr)
                        {
                            if (ptr == nullptr)
                            {
                                return;
                            }

                            if (memory_pool_ != nullptr)
                            {
                                memory_pool_->deallocate(ptr);
                            }
                            else
                            {
                                // 使用智能指针的删除器会自动释放
                                // 这里不需要手动释放
                            }
                        }

                    } // namespace encode
                } // namespace opus
            } // namespace process
        } // namespace audio
    } // namespace media
} // namespace app
