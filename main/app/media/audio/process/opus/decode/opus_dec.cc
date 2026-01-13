#include "opus_dec.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_opus_dec.h"
#include "esp_heap_caps.h"

static const char* const TAG = "OpusDecoder";

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

                        OpusDecoder::OpusDecoder(const DecoderConfig&      config,
                                                 tool::memory::MemoryPool* memory_pool)
                            : decoder_handle_(nullptr), config_(config), memory_pool_(memory_pool),
                              input_buffer_(nullptr), output_buffer_(nullptr),
                              input_buffer_size_(4096),  // 初始输入缓冲区大小
                              output_buffer_size_(8192), // 初始输出缓冲区大小（PCM 16bit 立体声）
                              needed_output_size_(0), initialized_(false)
                        {
                            // 注册 Opus 解码器（如果尚未注册）
                            esp_opus_dec_register();

                            // 转换为 ESP 解码器配置
                            esp_opus_dec_cfg_t esp_cfg = {};
                            esp_cfg.sample_rate        = config_.sample_rate;
                            esp_cfg.channel            = config_.channel;
                            esp_cfg.frame_duration     = config_.frame_duration;
                            esp_cfg.self_delimited     = config_.self_delimited;

                            // 打开解码器
                            esp_audio_err_t ret = esp_opus_dec_open(
                                &esp_cfg, sizeof(esp_opus_dec_cfg_t), &decoder_handle_);
                            if (ret != ESP_AUDIO_ERR_OK)
                            {
                                ESP_LOGE(TAG, "打开 Opus 解码器失败: %d", ret);
                                return;
                            }

                            // 分配输入缓冲区
                            uint8_t* input_ptr = allocateBuffer(input_buffer_size_);
                            if (input_ptr == nullptr)
                            {
                                ESP_LOGE(TAG, "分配输入缓冲区失败");
                                cleanup();
                                return;
                            }
                            input_buffer_ =
                                std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter>(input_ptr);

                            // 分配输出缓冲区（使用 uint8_t* 类型，因为 EspHeapDeleter 期望
                            // uint8_t*）
                            uint8_t* output_ptr = allocateBuffer(output_buffer_size_);
                            if (output_ptr == nullptr)
                            {
                                ESP_LOGE(TAG, "分配输出缓冲区失败");
                                cleanup();
                                return;
                            }
                            output_buffer_ =
                                std::unique_ptr<uint8_t[], tool::memory::EspHeapDeleter>(
                                    output_ptr);

                            initialized_ = true;
                            ESP_LOGI(TAG, "Opus 解码器初始化成功: sample_rate=%u, channel=%u",
                                     config_.sample_rate, config_.channel);
                        }

                        OpusDecoder::~OpusDecoder()
                        {
                            cleanup();
                        }

                        OpusDecoder::OpusDecoder(OpusDecoder&& other) noexcept
                            : decoder_handle_(other.decoder_handle_), config_(other.config_),
                              memory_pool_(other.memory_pool_),
                              input_buffer_(std::move(other.input_buffer_)),
                              output_buffer_(std::move(other.output_buffer_)),
                              input_buffer_size_(other.input_buffer_size_),
                              output_buffer_size_(other.output_buffer_size_),
                              needed_output_size_(other.needed_output_size_),
                              initialized_(other.initialized_)
                        {
                            other.decoder_handle_ = nullptr;
                            other.initialized_    = false;
                        }

                        OpusDecoder& OpusDecoder::operator=(OpusDecoder&& other) noexcept
                        {
                            if (this != &other)
                            {
                                cleanup();

                                decoder_handle_     = other.decoder_handle_;
                                config_             = other.config_;
                                memory_pool_        = other.memory_pool_;
                                input_buffer_       = std::move(other.input_buffer_);
                                output_buffer_      = std::move(other.output_buffer_);
                                input_buffer_size_  = other.input_buffer_size_;
                                output_buffer_size_ = other.output_buffer_size_;
                                needed_output_size_ = other.needed_output_size_;
                                initialized_        = other.initialized_;

                                other.decoder_handle_ = nullptr;
                                other.initialized_    = false;
                            }
                            return *this;
                        }

                        bool OpusDecoder::decode(const uint8_t* input_data, size_t input_size,
                                                 const int16_t** output_data, size_t* decoded_bytes,
                                                 size_t* consumed_bytes)
                        {
                            if (!isValid() || input_data == nullptr || output_data == nullptr ||
                                decoded_bytes == nullptr || consumed_bytes == nullptr)
                            {
                                return false;
                            }

                            // 准备输入数据
                            esp_audio_dec_in_raw_t in_raw = {};
                            in_raw.buffer                 = const_cast<uint8_t*>(input_data);
                            in_raw.len                    = static_cast<uint32_t>(input_size);
                            in_raw.consumed               = 0;
                            in_raw.frame_recover          = ESP_AUDIO_DEC_RECOVERY_NONE;

                            // 准备输出帧
                            esp_audio_dec_out_frame_t out_frame = {};
                            out_frame.buffer                    = output_buffer_.get();
                            out_frame.len          = static_cast<uint32_t>(output_buffer_size_);
                            out_frame.needed_size  = 0;
                            out_frame.decoded_size = 0;

                            // 执行解码
                            esp_audio_dec_info_t dec_info = {};
                            esp_audio_err_t      ret = esp_opus_dec_decode(decoder_handle_, &in_raw,
                                                                           &out_frame, &dec_info);

                            // 处理缓冲区不足的情况
                            if (ret == ESP_AUDIO_ERR_BUFF_NOT_ENOUGH)
                            {
                                needed_output_size_ = out_frame.needed_size;
                                ESP_LOGW(TAG, "输出缓冲区不足，需要 %u 字节",
                                         out_frame.needed_size);

                                // 尝试扩展输出缓冲区
                                if (!expandOutputBuffer(out_frame.needed_size))
                                {
                                    ESP_LOGE(TAG, "扩展输出缓冲区失败");
                                    return false;
                                }

                                // 重新设置输出帧
                                out_frame.buffer       = output_buffer_.get();
                                out_frame.len          = static_cast<uint32_t>(output_buffer_size_);
                                out_frame.needed_size  = 0;
                                out_frame.decoded_size = 0;

                                // 重新解码
                                esp_audio_dec_info_t dec_info_retry = {};
                                ret = esp_opus_dec_decode(decoder_handle_, &in_raw, &out_frame,
                                                          &dec_info_retry);
                            }

                            if (ret != ESP_AUDIO_ERR_OK)
                            {
                                ESP_LOGE(TAG, "解码失败: %d", ret);
                                *output_data    = nullptr;
                                *decoded_bytes  = 0;
                                *consumed_bytes = 0;
                                return false;
                            }

                            // 返回解码结果（转换为 int16_t*）
                            *output_data   = reinterpret_cast<const int16_t*>(output_buffer_.get());
                            *decoded_bytes = out_frame.decoded_size;
                            *consumed_bytes     = in_raw.consumed;
                            needed_output_size_ = 0;

                            return true;
                        }

                        bool OpusDecoder::reset()
                        {
                            if (!isValid())
                            {
                                return false;
                            }

                            esp_audio_err_t ret = esp_opus_dec_reset(decoder_handle_);
                            if (ret == ESP_AUDIO_ERR_OK)
                            {
                                needed_output_size_ = 0;
                                return true;
                            }
                            return false;
                        }

                        bool OpusDecoder::getInfo(esp_audio_dec_info_t* info) const
                        {
                            if (!isValid() || info == nullptr)
                            {
                                return false;
                            }

                            esp_audio_err_t ret = esp_audio_dec_get_info(decoder_handle_, info);
                            return (ret == ESP_AUDIO_ERR_OK);
                        }

                        void OpusDecoder::cleanup()
                        {
                            if (decoder_handle_ != nullptr)
                            {
                                esp_opus_dec_close(decoder_handle_);
                                decoder_handle_ = nullptr;
                            }

                            input_buffer_.reset();
                            output_buffer_.reset();
                            needed_output_size_ = 0;
                            initialized_        = false;
                        }

                        uint8_t* OpusDecoder::allocateBuffer(size_t size)
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

                        void OpusDecoder::deallocateBuffer(uint8_t* ptr)
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

                        bool OpusDecoder::expandOutputBuffer(size_t new_size)
                        {
                            if (new_size <= output_buffer_size_)
                            {
                                return true; // 不需要扩展
                            }

                            // 分配新的缓冲区
                            uint8_t* new_buffer_ptr = allocateBuffer(new_size);
                            if (new_buffer_ptr == nullptr)
                            {
                                ESP_LOGE(TAG, "分配新输出缓冲区失败: size=%u",
                                         (unsigned int)new_size);
                                return false;
                            }

                            // 释放旧缓冲区并设置新缓冲区（智能指针会自动处理）
                            output_buffer_.reset(new_buffer_ptr);
                            output_buffer_size_ = new_size;

                            ESP_LOGI(TAG, "输出缓冲区已扩展至 %u 字节", (unsigned int)new_size);
                            return true;
                        }

                    } // namespace decode
                } // namespace opus
            } // namespace process
        } // namespace audio
    } // namespace media
} // namespace app
