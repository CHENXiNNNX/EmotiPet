#include <stddef.h>

#include "app/i2c/i2c.hpp"
#include "app/media/audio/audio.hpp"
#include "app/media/audio/process/opus/encode/opus_enc.hpp"
#include "app/media/audio/process/opus/decode/opus_dec.hpp"
#include "esp_log.h"
#include "freertos/task.h"

static const char* const TAG = "OpusTest";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Opus 编解码测试开始");

    // 初始化 I2C
    app::i2c::I2c    i2c;
    app::i2c::Config i2c_cfg;
    i2c_cfg.port    = I2C_NUM_1;
    i2c_cfg.sda_pin = GPIO_NUM_17;
    i2c_cfg.scl_pin = GPIO_NUM_18;

    if (!i2c.init(&i2c_cfg))
    {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }

    i2c_master_bus_handle_t i2c_bus = i2c.getBusHandle();
    if (i2c_bus == nullptr)
    {
        ESP_LOGE(TAG, "I2C bus 句柄为空");
        return;
    }

    // 初始化音频
    app::media::audio::Config audio_cfg;
    audio_cfg.i2c_master_handle  = i2c_bus;
    audio_cfg.input_sample_rate  = 16000;
    audio_cfg.output_sample_rate = 16000;
    audio_cfg.input_reference    = false;
    audio_cfg.mclk               = GPIO_NUM_16;
    audio_cfg.ws                 = GPIO_NUM_45;
    audio_cfg.bclk               = GPIO_NUM_9;
    audio_cfg.din                = GPIO_NUM_10;
    audio_cfg.dout               = GPIO_NUM_8;
    audio_cfg.pa_pin             = GPIO_NUM_48;
    audio_cfg.es8311_addr        = ES8311_CODEC_DEFAULT_ADDR;
    audio_cfg.es7210_addr        = ES7210_CODEC_DEFAULT_ADDR;

    app::media::audio::Audio audio;
    if (!audio.init(&audio_cfg))
    {
        ESP_LOGE(TAG, "Audio 初始化失败");
        return;
    }

    audio.setOutputVolume(70);
    audio.enableOutput(true);
    audio.enableInput(true);

    // 配置 Opus 编码器
    app::media::audio::process::opus::encode::EncoderConfig enc_cfg;
    enc_cfg.sample_rate      = 16000;
    enc_cfg.channel          = ESP_AUDIO_MONO;
    enc_cfg.bits_per_sample  = ESP_AUDIO_BIT16;
    enc_cfg.bitrate          = 64000;
    enc_cfg.frame_duration   = ESP_OPUS_ENC_FRAME_DURATION_20_MS;
    enc_cfg.application_mode = ESP_OPUS_ENC_APPLICATION_VOIP;
    enc_cfg.complexity       = 0;
    enc_cfg.enable_fec       = false;
    enc_cfg.enable_dtx       = false;
    enc_cfg.enable_vbr       = false;

    app::media::audio::process::opus::encode::OpusEncoder encoder(enc_cfg);
    if (!encoder.isValid())
    {
        ESP_LOGE(TAG, "编码器初始化失败");
        return;
    }

    // 配置 Opus 解码器
    app::media::audio::process::opus::decode::DecoderConfig dec_cfg;
    dec_cfg.sample_rate    = 16000;
    dec_cfg.channel        = ESP_AUDIO_MONO;
    dec_cfg.frame_duration = ESP_OPUS_DEC_FRAME_DURATION_20_MS;
    dec_cfg.self_delimited = false;

    app::media::audio::process::opus::decode::OpusDecoder decoder(dec_cfg);
    if (!decoder.isValid())
    {
        ESP_LOGE(TAG, "解码器初始化失败");
        return;
    }

    ESP_LOGI(TAG, "开始编解码循环");

    const int frame_samples = 320;
    int16_t   pcm_input[frame_samples * 4];
    int16_t   pcm_mono[frame_samples];

    int encode_count = 0;
    int decode_count = 0;

    while (true)
    {
        // 读取音频输入（4 通道）
        int samples = audio.read(pcm_input, frame_samples);
        if (samples <= 0)
        {
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }

        // 转换为单声道（取第一个麦克风）
        for (int i = 0; i < samples; i++)
        {
            pcm_mono[i] = pcm_input[i * 4];
        }

        // 编码为 Opus
        const uint8_t* encoded_data  = nullptr;
        size_t         encoded_bytes = 0;
        bool           enc_ok        = encoder.encode(reinterpret_cast<const uint8_t*>(pcm_mono),
                                                      samples * sizeof(int16_t), &encoded_data, &encoded_bytes);

        if (enc_ok && encoded_data != nullptr && encoded_bytes > 0)
        {
            encode_count++;
            if (encode_count % 50 == 0)
            {
                ESP_LOGI(TAG, "编码: %d 帧, 大小: %u 字节", encode_count,
                         (unsigned int)encoded_bytes);
            }

            // 解码 Opus 数据
            const int16_t* decoded_pcm    = nullptr;
            size_t         decoded_bytes  = 0;
            size_t         consumed_bytes = 0;

            bool dec_ok = decoder.decode(encoded_data, encoded_bytes, &decoded_pcm, &decoded_bytes,
                                         &consumed_bytes);

            if (dec_ok && decoded_pcm != nullptr && decoded_bytes > 0)
            {
                decode_count++;
                if (decode_count % 50 == 0)
                {
                    ESP_LOGI(TAG, "解码: %d 帧, 大小: %u 字节", decode_count,
                             (unsigned int)decoded_bytes);
                }

                // 输出解码后的音频
                int output_samples = decoded_bytes / sizeof(int16_t);
                audio.write(decoded_pcm, output_samples);
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}
