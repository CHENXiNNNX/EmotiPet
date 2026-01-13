#include "i2c/i2c.hpp"
#include "media/audio/audio.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace app::i2c;
using namespace app::media::audio;

static const char* const TAG = "Main";

extern "C" void app_main(void)
{
    I2c              i2c;
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

    i2c.scan(200);

    app::media::audio::Config audio_cfg;
    audio_cfg.i2c_master_handle  = i2c_bus;
    audio_cfg.input_sample_rate  = 16000;
    audio_cfg.output_sample_rate = 16000;
    audio_cfg.input_reference    = false;

    audio_cfg.mclk = GPIO_NUM_16;
    audio_cfg.ws   = GPIO_NUM_45;
    audio_cfg.bclk = GPIO_NUM_9;
    audio_cfg.din  = GPIO_NUM_10;
    audio_cfg.dout = GPIO_NUM_8;

    audio_cfg.pa_pin      = GPIO_NUM_48;
    audio_cfg.es8311_addr = ES8311_CODEC_DEFAULT_ADDR;
    audio_cfg.es7210_addr = ES7210_CODEC_DEFAULT_ADDR;

    Audio audio;
    if (!audio.init(&audio_cfg))
    {
        ESP_LOGE(TAG, "Audio 初始化失败");
        return;
    }

    audio.setOutputVolume(100);
    audio.enableOutput(true);
    audio.enableInput(true);

    if (audio_cfg.input_reference)
    {
        // 参考信号模式：8 通道 (4 麦克风 + 4 参考信号)
        const int frame_samples = 160;
        int16_t   buffer[frame_samples * 8];
        int16_t   mic[frame_samples * 4];
        int16_t   ref[frame_samples * 4];

        while (true)
        {
            int frames = audio.read(buffer, frame_samples);
            if (frames > 0)
            {
                // 分离麦克风和参考信号
                for (int i = 0; i < frames; ++i)
                {
                    mic[i * 4 + 0] = buffer[i * 8 + 0]; // mic1
                    ref[i * 4 + 0] = buffer[i * 8 + 1]; // ref1
                    mic[i * 4 + 1] = buffer[i * 8 + 2]; // mic2
                    ref[i * 4 + 1] = buffer[i * 8 + 3]; // ref2
                    mic[i * 4 + 2] = buffer[i * 8 + 4]; // mic3
                    ref[i * 4 + 2] = buffer[i * 8 + 5]; // ref3
                    mic[i * 4 + 3] = buffer[i * 8 + 6]; // mic4
                    ref[i * 4 + 3] = buffer[i * 8 + 7]; // ref4
                }

                // 简单波束成形：4 个麦克风平均
                int16_t output[frame_samples];
                for (int i = 0; i < frames; ++i)
                {
                    int32_t sum =
                        (int32_t)mic[i * 4 + 0] + mic[i * 4 + 1] + mic[i * 4 + 2] + mic[i * 4 + 3];
                    output[i] = sum / 4;
                }
                audio.write(output, frames);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    else
    {
        // 普通模式：4 个麦克风
        const int frame_samples = 160;
        int16_t   buffer[frame_samples * 4]; // 4 通道交错数据
        int16_t   mic1[frame_samples];
        int16_t   mic2[frame_samples];
        int16_t   mic3[frame_samples];
        int16_t   mic4[frame_samples];
        int16_t   output[frame_samples];

        ESP_LOGI(TAG, "开始 4 麦克风采集和播放...");

        while (true)
        {
            int n = audio.read(buffer, frame_samples);
            if (n > 0)
            {
                // 分离 4 个麦克风的数据
                for (int i = 0; i < n; i++)
                {
                    mic1[i] = buffer[i * 4 + 0];
                    mic2[i] = buffer[i * 4 + 1];
                    mic3[i] = buffer[i * 4 + 2];
                    mic4[i] = buffer[i * 4 + 3];
                }

                // 简单的波束成形：4 个麦克风平均（降噪效果）
                for (int i = 0; i < n; i++)
                {
                    int32_t sum = (int32_t)mic1[i] + mic2[i] + mic3[i] + mic4[i];
                    output[i]   = sum / 4;
                }

                // 播放融合后的音频
                audio.write(mic1, n);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
