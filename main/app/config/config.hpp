#pragma once

#include <driver/gpio.h>

namespace app
{
    namespace config
    {
        // i2c pins
        const gpio_num_t I2C_SDA = GPIO_NUM_17;
        const gpio_num_t I2C_SCL = GPIO_NUM_18;

        // i2s pins
        const gpio_num_t I2S_MCLK = GPIO_NUM_16;
        const gpio_num_t I2S_WS   = GPIO_NUM_45;
        const gpio_num_t I2S_BCLK = GPIO_NUM_9;
        const gpio_num_t I2S_DIN  = GPIO_NUM_10;
        const gpio_num_t I2S_DOUT = GPIO_NUM_8;

        // pa pin
        const gpio_num_t PA_PIN = GPIO_NUM_48;

        // camera pins
        const gpio_num_t CAM_DVP_D0    = GPIO_NUM_13; // CAMERA_PIN_D0
        const gpio_num_t CAM_DVP_D1    = GPIO_NUM_47; // CAMERA_PIN_D1
        const gpio_num_t CAM_DVP_D2    = GPIO_NUM_14; // CAMERA_PIN_D2
        const gpio_num_t CAM_DVP_D3    = GPIO_NUM_3;  // CAMERA_PIN_D3
        const gpio_num_t CAM_DVP_D4    = GPIO_NUM_12; // CAMERA_PIN_D4
        const gpio_num_t CAM_DVP_D5    = GPIO_NUM_42; // CAMERA_PIN_D5
        const gpio_num_t CAM_DVP_D6    = GPIO_NUM_41; // CAMERA_PIN_D6
        const gpio_num_t CAM_DVP_D7    = GPIO_NUM_39; // CAMERA_PIN_D7
        const gpio_num_t CAM_DVP_VSYNC = GPIO_NUM_21; // CAMERA_PIN_VSYNC
        const gpio_num_t CAM_DVP_HREF  = GPIO_NUM_38; // CAMERA_PIN_HREF
        const gpio_num_t CAM_DVP_PCLK  = GPIO_NUM_11; // CAMERA_PIN_PCLK
        const gpio_num_t CAM_DVP_XCLK  = GPIO_NUM_40; // CAMERA_PIN_XCLK (外部时钟)
        const gpio_num_t CAM_RESET_PIN = GPIO_NUM_NC; // CAMERA_PIN_RESET (-1, 不使用)
        const gpio_num_t CAM_PWDN_PIN  = GPIO_NUM_NC; // CAMERA_PIN_PWDN (-1, 不使用)

        // camera clock
        const uint32_t CAM_XCLK_FREQ = 20000000;

    } // namespace config
} // namespace app
