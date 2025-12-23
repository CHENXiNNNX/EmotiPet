#pragma once

#include <cstdint>
#include <driver/gpio.h>
#include <driver/i2c_master.h>
#include <driver/i2s_std.h>
#include <esp_codec_dev.h>
#include <esp_codec_dev_defaults.h>
#include <mutex>

#include "config/config.hpp"

namespace app
{
    namespace media
    {
        namespace audio
        {
            struct Config
            {
                void*      i2c_master_handle;
                int        input_sample_rate;
                int        output_sample_rate;
                gpio_num_t mclk;
                gpio_num_t bclk;
                gpio_num_t ws;
                gpio_num_t dout;
                gpio_num_t din;
                gpio_num_t pa_pin;
                uint8_t    es8389_addr;
                uint8_t    es7210_addr;
                bool       input_reference;

                Config()
                    : i2c_master_handle(nullptr), input_sample_rate(16000),
                      output_sample_rate(16000), mclk(config::I2S_MCLK), bclk(config::I2S_BCLK),
                      ws(config::I2S_WS), dout(config::I2S_DOUT), din(config::I2S_DIN),
                      pa_pin(config::PA_PIN), es8389_addr(ES8389_CODEC_DEFAULT_ADDR),
                      es7210_addr(ES7210_CODEC_DEFAULT_ADDR), input_reference(false)
                {
                }
            };

            class Audio
            {
            public:
                Audio();
                ~Audio();

                bool init(const Config* config = nullptr);

                // input_reference=false: 返回单通道 PCM (dest 长度 >= samples)
                // input_reference=true: 返回双通道交错 [mic0,ref0,mic1,ref1,...] (dest 长度 >=
                // samples*2)
                int  read(int16_t* dest, int samples);
                int  write(const int16_t* data, int samples);
                void setOutputVolume(int volume);
                void enableInput(bool enable);
                void enableOutput(bool enable);
                bool isInitialized() const
                {
                    return initialized_;
                }

            private:
                void CreateDuplexChannels(gpio_num_t mclk, gpio_num_t bclk, gpio_num_t ws,
                                          gpio_num_t dout, gpio_num_t din);

                const audio_codec_data_if_t* data_if_      = nullptr;
                const audio_codec_ctrl_if_t* out_ctrl_if_  = nullptr;
                const audio_codec_if_t*      out_codec_if_ = nullptr;
                const audio_codec_ctrl_if_t* in_ctrl_if_   = nullptr;
                const audio_codec_if_t*      in_codec_if_  = nullptr;
                const audio_codec_gpio_if_t* gpio_if_      = nullptr;

                esp_codec_dev_handle_t output_dev_ = nullptr;
                esp_codec_dev_handle_t input_dev_  = nullptr;

                i2s_chan_handle_t tx_handle_ = nullptr;
                i2s_chan_handle_t rx_handle_ = nullptr;

                Config     config_;
                bool       initialized_    = false;
                bool       input_enabled_  = false;
                bool       output_enabled_ = false;
                int        output_volume_  = 70;
                int        input_gain_     = 30;
                std::mutex data_if_mutex_;
            };
        } // namespace audio
    }     // namespace media
} // namespace app
