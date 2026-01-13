#include "afe.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_afe_sr_models.h"

static const char* const TAG = "Afe";

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

                    Afe::Afe(const Config& config, tool::memory::MemoryPool* memory_pool)
                        : config_(config), memory_pool_(memory_pool), afe_config_(nullptr),
                          afe_iface_(nullptr), afe_data_(nullptr), is_speaking_(false),
                          initialized_(false)
                    {
                        if (!initialize())
                        {
                            ESP_LOGE(TAG, "AFE 初始化失败");
                            cleanup();
                        }
                    }

                    Afe::~Afe()
                    {
                        cleanup();
                    }

                    Afe::Afe(Afe&& other) noexcept
                        : config_(std::move(other.config_)), memory_pool_(other.memory_pool_),
                          afe_config_(std::move(other.afe_config_)), afe_iface_(other.afe_iface_),
                          afe_data_(other.afe_data_),
                          vad_state_callback_(std::move(other.vad_state_callback_)),
                          audio_output_callback_(std::move(other.audio_output_callback_)),
                          is_speaking_(other.is_speaking_), initialized_(other.initialized_)
                    {
                        other.afe_iface_   = nullptr;
                        other.afe_data_    = nullptr;
                        other.initialized_ = false;
                    }

                    Afe& Afe::operator=(Afe&& other) noexcept
                    {
                        if (this != &other)
                        {
                            cleanup();

                            config_                = std::move(other.config_);
                            memory_pool_           = other.memory_pool_;
                            afe_config_            = std::move(other.afe_config_);
                            afe_iface_             = other.afe_iface_;
                            afe_data_              = other.afe_data_;
                            vad_state_callback_    = std::move(other.vad_state_callback_);
                            audio_output_callback_ = std::move(other.audio_output_callback_);
                            is_speaking_           = other.is_speaking_;
                            initialized_           = other.initialized_;

                            other.afe_iface_   = nullptr;
                            other.afe_data_    = nullptr;
                            other.initialized_ = false;
                        }
                        return *this;
                    }

                    bool Afe::initialize()
                    {
                        // 初始化模型列表（如果未提供）
                        srmodel_list_t* models = config_.models_list;
                        if (models == nullptr)
                        {
                            models = esp_srmodel_init("model");
                            if (models == nullptr)
                            {
                                ESP_LOGW(TAG, "无法初始化模型列表，将使用默认配置");
                            }
                        }

                        // 查找 VAD 和 NS 模型
                        char* vad_model_name = nullptr;
                        char* ns_model_name  = nullptr;

                        if (models != nullptr)
                        {
                            vad_model_name = esp_srmodel_filter(models, ESP_VADN_PREFIX, NULL);
                            ns_model_name  = esp_srmodel_filter(models, ESP_NSNET_PREFIX, NULL);
                        }

                        // 创建 AFE 配置
                        afe_config_t* config_ptr =
                            afe_config_init(config_.input_format.c_str(), models, config_.afe_type,
                                            config_.afe_mode);

                        if (config_ptr == nullptr)
                        {
                            ESP_LOGE(TAG, "创建 AFE 配置失败");
                            return false;
                        }

                        // 使用智能指针管理配置
                        afe_config_.reset(config_ptr);

                        // 配置 AEC
                        if (config_.enable_aec)
                        {
                            afe_config_->aec_init = true;
                            afe_config_->aec_mode = AEC_MODE_VOIP_HIGH_PERF;
                            afe_config_->vad_init = false; // AEC 启用时通常禁用 VAD
                        }
                        else
                        {
                            afe_config_->aec_init = false;
                        }

                        // 配置 VAD
                        if (config_.enable_vad && !config_.enable_aec)
                        {
                            afe_config_->vad_init         = true;
                            afe_config_->vad_mode         = config_.vad_mode;
                            afe_config_->vad_min_noise_ms = config_.vad_min_noise_ms;
                            if (vad_model_name != nullptr)
                            {
                                afe_config_->vad_model_name = vad_model_name;
                                ESP_LOGI(TAG, "使用 VADNet 模型: %s", vad_model_name);
                            }
                            else
                            {
                                afe_config_->vad_model_name = nullptr;
                                ESP_LOGI(TAG, "使用 WebRTC VAD");
                            }
                        }
                        else
                        {
                            afe_config_->vad_init = false;
                        }

                        // 配置 NS
                        if (config_.enable_ns && ns_model_name != nullptr)
                        {
                            afe_config_->ns_init       = true;
                            afe_config_->ns_model_name = ns_model_name;
                            afe_config_->afe_ns_mode   = AFE_NS_MODE_NET;
                            ESP_LOGI(TAG, "使用 NSNet 模型: %s", ns_model_name);
                        }
                        else
                        {
                            afe_config_->ns_init = false;
                        }

                        // 配置 AGC
                        afe_config_->agc_init = config_.enable_agc;

                        // 配置内存分配模式
                        afe_config_->memory_alloc_mode = AFE_MEMORY_ALLOC_MORE_PSRAM;

                        // 检查并修正配置
                        afe_config_check(afe_config_.get());

                        // 获取 AFE 接口
                        afe_iface_ = esp_afe_handle_from_config(afe_config_.get());
                        if (afe_iface_ == nullptr)
                        {
                            ESP_LOGE(TAG, "获取 AFE 接口失败");
                            return false;
                        }

                        // 创建 AFE 数据
                        afe_data_ = afe_iface_->create_from_config(afe_config_.get());
                        if (afe_data_ == nullptr)
                        {
                            ESP_LOGE(TAG, "创建 AFE 数据失败");
                            return false;
                        }

                        initialized_ = true;

                        ESP_LOGI(TAG,
                                 "AFE 初始化成功: input_format=%s, sample_rate=%d, "
                                 "aec=%d, vad=%d, ns=%d, agc=%d",
                                 config_.input_format.c_str(), config_.sample_rate,
                                 config_.enable_aec, config_.enable_vad, config_.enable_ns,
                                 config_.enable_agc);

                        return true;
                    }

                    void Afe::cleanup()
                    {
                        if (afe_data_ != nullptr && afe_iface_ != nullptr)
                        {
                            afe_iface_->destroy(afe_data_);
                            afe_data_ = nullptr;
                        }

                        afe_iface_ = nullptr;
                        afe_config_.reset();
                        initialized_ = false;
                    }

                    size_t Afe::getFeedSize() const
                    {
                        if (!isValid())
                        {
                            return 0;
                        }
                        return afe_iface_->get_feed_chunksize(afe_data_);
                    }

                    size_t Afe::getFetchSize() const
                    {
                        if (!isValid())
                        {
                            return 0;
                        }
                        return afe_iface_->get_fetch_chunksize(afe_data_);
                    }

                    int Afe::getChannelNum() const
                    {
                        if (!isValid())
                        {
                            return 0;
                        }
                        return afe_iface_->get_channel_num(afe_data_);
                    }

                    int Afe::getSampleRate() const
                    {
                        if (!isValid())
                        {
                            return 0;
                        }
                        return afe_iface_->get_samp_rate(afe_data_);
                    }

                    bool Afe::feed(const int16_t* data, size_t samples)
                    {
                        if (!isValid())
                        {
                            return false;
                        }

                        if (data == nullptr || samples == 0)
                        {
                            return false;
                        }

                        int ret = afe_iface_->feed(afe_data_, data);
                        return ret >= 0;
                    }

                    bool Afe::fetch(int timeout_ms)
                    {
                        if (!isValid())
                        {
                            return false;
                        }

                        TickType_t ticks_to_wait = 0;
                        if (timeout_ms > 0)
                        {
                            ticks_to_wait = pdMS_TO_TICKS(timeout_ms);
                        }
                        else if (timeout_ms == 0)
                        {
                            ticks_to_wait = 0;
                        }
                        else
                        {
                            ticks_to_wait = portMAX_DELAY;
                        }

                        afe_fetch_result_t* result =
                            afe_iface_->fetch_with_delay(afe_data_, ticks_to_wait);
                        if (result == nullptr)
                        {
                            return false;
                        }

                        if (result->ret_value == ESP_FAIL)
                        {
                            ESP_LOGW(TAG, "AFE fetch 返回错误: %d", result->ret_value);
                            return false;
                        }

                        processFetchResult(result);
                        return true;
                    }

                    void Afe::processFetchResult(const afe_fetch_result_t* result)
                    {
                        if (result == nullptr)
                        {
                            return;
                        }

                        // 处理 VAD 状态变化
                        if (vad_state_callback_)
                        {
                            bool new_speaking = (result->vad_state == VAD_SPEECH);
                            if (new_speaking != is_speaking_)
                            {
                                is_speaking_ = new_speaking;
                                vad_state_callback_(is_speaking_);
                            }
                        }
                        else
                        {
                            // 即使没有回调，也更新内部状态
                            is_speaking_ = (result->vad_state == VAD_SPEECH);
                        }

                        // 处理音频输出
                        if (audio_output_callback_ && result->data != nullptr &&
                            result->data_size > 0)
                        {
                            size_t samples = result->data_size / sizeof(int16_t);
                            audio_output_callback_(result->data, samples);
                        }
                    }

                    void Afe::reset()
                    {
                        if (!isValid())
                        {
                            return;
                        }

                        afe_iface_->reset_buffer(afe_data_);
                        is_speaking_ = false;
                    }

                    void Afe::enableVad(bool enable)
                    {
                        if (!isValid())
                        {
                            return;
                        }

                        if (enable)
                        {
                            afe_iface_->enable_vad(afe_data_);
                        }
                        else
                        {
                            afe_iface_->disable_vad(afe_data_);
                        }
                    }

                    void Afe::enableAec(bool enable)
                    {
                        if (!isValid())
                        {
                            return;
                        }

                        if (enable)
                        {
                            afe_iface_->enable_aec(afe_data_);
                        }
                        else
                        {
                            afe_iface_->disable_aec(afe_data_);
                        }
                    }

                } // namespace afe
            } // namespace process
        } // namespace audio
    } // namespace media
} // namespace app
