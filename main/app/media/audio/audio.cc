#include "audio.hpp"
#include "esp_log.h"
#include <driver/i2c_master.h>
#include <driver/i2s_tdm.h>
#include <driver/i2s_std.h>
#include <driver/gpio.h>
#include <algorithm>

#define AUDIO_CODEC_DMA_DESC_NUM 6
#define AUDIO_CODEC_DMA_FRAME_NUM 240

static const char* const TAG = "Audio";

namespace app
{
    namespace media
    {
        namespace audio
        {
            Audio::Audio()
                : initialized_(false), input_enabled_(false), output_enabled_(false),
                  output_volume_(70), input_gain_(30)
            {
            }

            Audio::~Audio()
            {
                if (initialized_)
                {
                    if (output_dev_ != nullptr)
                    {
                        esp_codec_dev_close(output_dev_);
                        esp_codec_dev_delete(output_dev_);
                        output_dev_ = nullptr;
                    }
                    if (input_dev_ != nullptr)
                    {
                        esp_codec_dev_close(input_dev_);
                        esp_codec_dev_delete(input_dev_);
                        input_dev_ = nullptr;
                    }

                    if (in_codec_if_ != nullptr)
                    {
                        audio_codec_delete_codec_if(in_codec_if_);
                        in_codec_if_ = nullptr;
                    }
                    if (in_ctrl_if_ != nullptr)
                    {
                        audio_codec_delete_ctrl_if(in_ctrl_if_);
                        in_ctrl_if_ = nullptr;
                    }
                    if (out_codec_if_ != nullptr)
                    {
                        audio_codec_delete_codec_if(out_codec_if_);
                        out_codec_if_ = nullptr;
                    }
                    if (out_ctrl_if_ != nullptr)
                    {
                        audio_codec_delete_ctrl_if(out_ctrl_if_);
                        out_ctrl_if_ = nullptr;
                    }
                    if (gpio_if_ != nullptr)
                    {
                        audio_codec_delete_gpio_if(gpio_if_);
                        gpio_if_ = nullptr;
                    }
                    if (data_if_ != nullptr)
                    {
                        audio_codec_delete_data_if(data_if_);
                        data_if_ = nullptr;
                    }

                    if (tx_handle_ != nullptr)
                    {
                        i2s_channel_disable(tx_handle_);
                        i2s_del_channel(tx_handle_);
                        tx_handle_ = nullptr;
                    }
                    if (rx_handle_ != nullptr)
                    {
                        i2s_channel_disable(rx_handle_);
                        i2s_del_channel(rx_handle_);
                        rx_handle_ = nullptr;
                    }
                }
            }

            bool Audio::init(const Config* config)
            {
                if (initialized_)
                {
                    return false;
                }

                if (config != nullptr)
                {
                    config_ = *config;
                }

                if (config_.i2c_master_handle == nullptr)
                {
                    ESP_LOGE(TAG, "I2C master handle 为空");
                    return false;
                }

                if (config_.input_sample_rate != config_.output_sample_rate)
                {
                    ESP_LOGE(TAG, "输入和输出采样率必须相同");
                    return false;
                }

                CreateDuplexChannels(config_.mclk, config_.bclk, config_.ws, config_.dout,
                                     config_.din);
                audio_codec_i2s_cfg_t i2s_cfg = {
                    .port      = I2S_NUM_0,
                    .rx_handle = rx_handle_,
                    .tx_handle = tx_handle_,
                };
                data_if_ = audio_codec_new_i2s_data(&i2s_cfg);
                if (data_if_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建 I2S data interface 失败");
                    return false;
                }
                audio_codec_i2c_cfg_t i2c_cfg = {
                    .port       = (i2c_port_t)1,
                    .addr       = config_.es8389_addr,
                    .bus_handle = config_.i2c_master_handle,
                };
                out_ctrl_if_ = audio_codec_new_i2c_ctrl(&i2c_cfg);
                if (out_ctrl_if_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建 ES8389 I2C control interface 失败");
                    return false;
                }

                gpio_if_ = audio_codec_new_gpio();
                if (gpio_if_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建 GPIO interface 失败");
                    return false;
                }

                es8389_codec_cfg_t es8389_cfg        = {};
                es8389_cfg.ctrl_if                   = out_ctrl_if_;
                es8389_cfg.gpio_if                   = gpio_if_;
                es8389_cfg.codec_mode                = ESP_CODEC_DEV_WORK_MODE_DAC;
                es8389_cfg.pa_pin                    = config_.pa_pin;
                es8389_cfg.use_mclk                  = true;
                es8389_cfg.hw_gain.pa_voltage        = 5.0;
                es8389_cfg.hw_gain.codec_dac_voltage = 3.3;
                out_codec_if_                        = es8389_codec_new(&es8389_cfg);
                if (out_codec_if_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建 ES8389 codec interface 失败");
                    return false;
                }

                esp_codec_dev_cfg_t dev_cfg = {
                    .dev_type = ESP_CODEC_DEV_TYPE_OUT,
                    .codec_if = out_codec_if_,
                    .data_if  = data_if_,
                };
                output_dev_ = esp_codec_dev_new(&dev_cfg);
                if (output_dev_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建输出设备失败");
                    return false;
                }

                // 创建输入 codec (ES7210)
                i2c_cfg.addr = config_.es7210_addr;
                in_ctrl_if_  = audio_codec_new_i2c_ctrl(&i2c_cfg);
                if (in_ctrl_if_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建 ES7210 I2C control interface 失败");
                    return false;
                }

                es7210_codec_cfg_t es7210_cfg = {};
                es7210_cfg.ctrl_if            = in_ctrl_if_;
                es7210_cfg.mic_selected =
                    ES7210_SEL_MIC1 | ES7210_SEL_MIC2 | ES7210_SEL_MIC3 | ES7210_SEL_MIC4;
                in_codec_if_ = es7210_codec_new(&es7210_cfg);
                if (in_codec_if_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建 ES7210 codec interface 失败");
                    return false;
                }

                dev_cfg.dev_type = ESP_CODEC_DEV_TYPE_IN;
                dev_cfg.codec_if = in_codec_if_;
                input_dev_       = esp_codec_dev_new(&dev_cfg);
                if (input_dev_ == nullptr)
                {
                    ESP_LOGE(TAG, "创建输入设备失败");
                    return false;
                }

                initialized_ = true;
                return true;
            }

            void Audio::CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                                             gpio_num_t dout, gpio_num_t din)
            {
                i2s_chan_config_t chan_cfg = {
                    .id                   = I2S_NUM_0,
                    .role                 = I2S_ROLE_MASTER,
                    .dma_desc_num         = AUDIO_CODEC_DMA_DESC_NUM,
                    .dma_frame_num        = AUDIO_CODEC_DMA_FRAME_NUM,
                    .auto_clear_after_cb  = true,
                    .auto_clear_before_cb = false,
                    .intr_priority        = 0,
                };
                ESP_ERROR_CHECK(i2s_new_channel(&chan_cfg, &tx_handle_, &rx_handle_));

                i2s_std_config_t std_cfg = {
                    .clk_cfg  = {.sample_rate_hz  = (uint32_t)config_.output_sample_rate,
                                 .clk_src         = I2S_CLK_SRC_DEFAULT,
                                 .ext_clk_freq_hz = 0,
                                 .mclk_multiple   = I2S_MCLK_MULTIPLE_256},
                    .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                                 .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                                 .slot_mode      = I2S_SLOT_MODE_STEREO,
                                 .slot_mask      = I2S_STD_SLOT_BOTH,
                                 .ws_width       = I2S_DATA_BIT_WIDTH_16BIT,
                                 .ws_pol         = false,
                                 .bit_shift      = true,
                                 .left_align     = true,
                                 .big_endian     = false,
                                 .bit_order_lsb  = false},
                    .gpio_cfg = {
                        .mclk         = mclk,
                        .bclk         = bclk,
                        .ws           = ws,
                        .dout         = dout,
                        .din          = I2S_GPIO_UNUSED,
                        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};

                i2s_tdm_config_t tdm_cfg = {
                    .clk_cfg =
                        {
                            .sample_rate_hz  = (uint32_t)config_.input_sample_rate,
                            .clk_src         = I2S_CLK_SRC_DEFAULT,
                            .ext_clk_freq_hz = 0,
                            .mclk_multiple   = I2S_MCLK_MULTIPLE_256,
                            .bclk_div        = 8,
                        },
                    .slot_cfg = {.data_bit_width = I2S_DATA_BIT_WIDTH_16BIT,
                                 .slot_bit_width = I2S_SLOT_BIT_WIDTH_AUTO,
                                 .slot_mode      = I2S_SLOT_MODE_STEREO,
                                 .slot_mask  = i2s_tdm_slot_mask_t(I2S_TDM_SLOT0 | I2S_TDM_SLOT1 |
                                                                   I2S_TDM_SLOT2 | I2S_TDM_SLOT3),
                                 .ws_width   = I2S_TDM_AUTO_WS_WIDTH,
                                 .ws_pol     = false,
                                 .bit_shift  = true,
                                 .left_align = false,
                                 .big_endian = false,
                                 .bit_order_lsb = false,
                                 .skip_mask     = false,
                                 .total_slot    = I2S_TDM_AUTO_SLOT_NUM},
                    .gpio_cfg = {
                        .mclk         = mclk,
                        .bclk         = bclk,
                        .ws           = ws,
                        .dout         = I2S_GPIO_UNUSED,
                        .din          = din,
                        .invert_flags = {.mclk_inv = false, .bclk_inv = false, .ws_inv = false}}};

                ESP_ERROR_CHECK(i2s_channel_init_std_mode(tx_handle_, &std_cfg));
                ESP_ERROR_CHECK(i2s_channel_init_tdm_mode(rx_handle_, &tdm_cfg));
            }

            int Audio::read(int16_t* dest, int samples)
            {
                if (!initialized_ || !input_enabled_ || input_dev_ == nullptr)
                {
                    return 0;
                }

                if (!config_.input_reference)
                {
                    esp_err_t ret =
                        esp_codec_dev_read(input_dev_, (void*)dest, samples * sizeof(int16_t));
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "读取音频数据失败: %s", esp_err_to_name(ret));
                        return 0;
                    }
                    return samples;
                }
                else
                {
                    esp_err_t ret =
                        esp_codec_dev_read(input_dev_, (void*)dest, samples * 2 * sizeof(int16_t));
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "读取音频数据失败: %s", esp_err_to_name(ret));
                        return 0;
                    }
                    return samples;
                }
            }

            int Audio::write(const int16_t* data, int samples)
            {
                if (!initialized_ || !output_enabled_ || output_dev_ == nullptr)
                {
                    return 0;
                }

                esp_err_t ret =
                    esp_codec_dev_write(output_dev_, (void*)data, samples * sizeof(int16_t));
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "写入音频数据失败: %s", esp_err_to_name(ret));
                    return 0;
                }
                return samples;
            }

            void Audio::setOutputVolume(int volume)
            {
                volume         = std::max(0, std::min(100, volume));
                output_volume_ = volume;

                if (initialized_ && output_dev_ != nullptr)
                {
                    esp_codec_dev_set_out_vol(output_dev_, volume);
                }
            }

            void Audio::enableInput(bool enable)
            {
                std::lock_guard<std::mutex> lock(data_if_mutex_);

                if (enable == input_enabled_)
                {
                    return;
                }

                if (!initialized_ || input_dev_ == nullptr)
                {
                    ESP_LOGE(TAG, "Audio 未初始化，无法启用输入");
                    return;
                }

                if (enable)
                {
                    esp_codec_dev_sample_info_t fs = {
                        .bits_per_sample = 16,
                        .channel         = 1,
                        .channel_mask    = ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0),
                        .sample_rate     = (uint32_t)config_.input_sample_rate,
                        .mclk_multiple   = 0,
                    };

                    if (config_.input_reference)
                    {
                        fs.channel_mask |= ESP_CODEC_DEV_MAKE_CHANNEL_MASK(1);
                        fs.channel = 2;
                    }

                    esp_err_t ret = esp_codec_dev_open(input_dev_, &fs);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "打开输入设备失败: %s", esp_err_to_name(ret));
                        return;
                    }

                    esp_codec_dev_set_in_channel_gain(
                        input_dev_, ESP_CODEC_DEV_MAKE_CHANNEL_MASK(0), input_gain_);
                }
                else
                {
                    esp_codec_dev_close(input_dev_);
                }

                input_enabled_ = enable;
            }

            void Audio::enableOutput(bool enable)
            {
                std::lock_guard<std::mutex> lock(data_if_mutex_);

                if (enable == output_enabled_)
                {
                    return;
                }

                if (!initialized_ || output_dev_ == nullptr)
                {
                    ESP_LOGE(TAG, "Audio 未初始化，无法启用输出");
                    return;
                }

                if (enable)
                {
                    esp_codec_dev_sample_info_t fs = {
                        .bits_per_sample = 16,
                        .channel         = 1,
                        .channel_mask    = 0,
                        .sample_rate     = (uint32_t)config_.output_sample_rate,
                        .mclk_multiple   = 0,
                    };
                    esp_err_t ret = esp_codec_dev_open(output_dev_, &fs);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "打开输出设备失败: %s", esp_err_to_name(ret));
                        return;
                    }
                    esp_codec_dev_set_out_vol(output_dev_, output_volume_);
                    if (config_.pa_pin != GPIO_NUM_NC)
                    {
                        gpio_set_level(config_.pa_pin, 1);
                    }
                }
                else
                {
                    esp_codec_dev_close(output_dev_);
                    if (config_.pa_pin != GPIO_NUM_NC)
                    {
                        gpio_set_level(config_.pa_pin, 0);
                    }
                }

                output_enabled_ = enable;
            }
        } // namespace audio
    }     // namespace media
} // namespace app
