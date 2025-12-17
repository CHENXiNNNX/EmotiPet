#include "common/i2c/i2c.hpp"
#include "media/audio/audio.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

using namespace app::common::i2c;
using namespace app::media::audio;

static const char* const TAG = "Main";

extern "C" void app_main(void)
{
    I2c                      i2c;
    app::common::i2c::Config i2c_cfg;
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
    audio_cfg.es8389_addr = ES8389_CODEC_DEFAULT_ADDR;
    audio_cfg.es7210_addr = ES7210_CODEC_DEFAULT_ADDR;

    Audio audio;
    if (!audio.init(&audio_cfg))
    {
        ESP_LOGE(TAG, "Audio 初始化失败");
        return;
    }

    audio.setOutputVolume(70);
    audio.enableOutput(true);
    audio.enableInput(true);

    if (audio_cfg.input_reference)
    {
        const int frame_samples = 160;
        int16_t   buffer[frame_samples * 2];
        int16_t   mic[frame_samples];
        int16_t   ref[frame_samples];

        while (true)
        {
            int frames = audio.read(buffer, frame_samples);
            if (frames > 0)
            {
                for (int i = 0; i < frames; ++i)
                {
                    mic[i] = buffer[(i * 2) + 0];
                    ref[i] = buffer[(i * 2) + 1];
                }
                audio.write(mic, frames);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
    else
    {
        const int frame_samples = 160;
        int16_t   buffer[frame_samples];

        while (true)
        {
            int n = audio.read(buffer, frame_samples);
            if (n > 0)
            {
                audio.write(buffer, n);
            }
            vTaskDelay(pdMS_TO_TICKS(10));
        }
    }
}
