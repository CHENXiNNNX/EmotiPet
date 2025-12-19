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

    } // namespace config
} // namespace app
