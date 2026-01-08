#ifndef _BOARD_CONFIG_H_
#define _BOARD_CONFIG_H_

#include <driver/gpio.h>

#define I2C_SDA_PIN  GPIO_NUM_17
#define I2C_SCL_PIN  GPIO_NUM_18

#define AUDIO_INPUT_SAMPLE_RATE  16000
#define AUDIO_OUTPUT_SAMPLE_RATE 16000

#define AUDIO_INPUT_REFERENCE    true

#define AUDIO_I2S_GPIO_MCLK GPIO_NUM_16
#define AUDIO_I2S_GPIO_WS   GPIO_NUM_45
#define AUDIO_I2S_GPIO_BCLK GPIO_NUM_9
#define AUDIO_I2S_GPIO_DIN  GPIO_NUM_10
#define AUDIO_I2S_GPIO_DOUT GPIO_NUM_8

#define AUDIO_CODEC_PA_PIN       GPIO_NUM_48
#define AUDIO_CODEC_ES8311_ADDR  ES8311_CODEC_DEFAULT_ADDR
#define AUDIO_CODEC_ES7210_ADDR  ES7210_CODEC_DEFAULT_ADDR

#define BUILTIN_LED_GPIO        GPIO_NUM_NC
#define BOOT_BUTTON_GPIO        GPIO_NUM_5
#define VOLUME_UP_BUTTON_GPIO   GPIO_NUM_NC
#define VOLUME_DOWN_BUTTON_GPIO GPIO_NUM_NC

#ifdef CONFIG_LCD_ST7789
#define DISPLAY_SDA_PIN GPIO_NUM_NC
#define DISPLAY_SCL_PIN GPIO_NUM_NC
#define DISPLAY_WIDTH   280
#define DISPLAY_HEIGHT  240
#define DISPLAY_SWAP_XY true
#define DISPLAY_MIRROR_X false
#define DISPLAY_MIRROR_Y true
#define BACKLIGHT_INVERT false

#define DISPLAY_OFFSET_X  20
#define DISPLAY_OFFSET_Y  0
#endif

#ifdef CONFIG_LCD_ILI9341
#define LCD_TYPE_ILI9341_SERIAL
#define DISPLAY_SDA_PIN GPIO_NUM_NC
#define DISPLAY_SCL_PIN GPIO_NUM_NC
#define DISPLAY_WIDTH   320
#define DISPLAY_HEIGHT  240

#define DISPLAY_SWAP_XY false
#define DISPLAY_MIRROR_X true
#define DISPLAY_MIRROR_Y true
#define BACKLIGHT_INVERT false

#define DISPLAY_OFFSET_X  0
#define DISPLAY_OFFSET_Y  0
#endif

#define DISPLAY_BACKLIGHT_PIN GPIO_NUM_NC
#define DISPLAY_BACKLIGHT_OUTPUT_INVERT false

// 摄像头引脚定义
#define CAMERA_PIN_PWDN -1      // 电源控制引脚，-1表示不使用
#define CAMERA_PIN_RESET -1     // 复位引脚，-1表示不使用
#define CAMERA_PIN_XCLK 40      // 外部时钟输入引脚
#define CAMERA_PIN_SIOD 17      // I2C数据线
#define CAMERA_PIN_SIOC 18      // I2C时钟线

// 数据引脚定义
#define CAMERA_PIN_D7 39        // 数据位7
#define CAMERA_PIN_D6 41        // 数据位6
#define CAMERA_PIN_D5 42        // 数据位5
#define CAMERA_PIN_D4 12        // 数据位4
#define CAMERA_PIN_D3 3         // 数据位3
#define CAMERA_PIN_D2 14        // 数据位2
#define CAMERA_PIN_D1 47        // 数据位1
#define CAMERA_PIN_D0 13        // 数据位0

// 控制信号引脚
#define CAMERA_PIN_VSYNC 21     // 垂直同步信号
#define CAMERA_PIN_HREF 38      // 水平参考信号
#define CAMERA_PIN_PCLK 11      // 像素时钟

#define XCLK_FREQ_HZ 20000000   // 外部时钟频率：20MHz

#define BSP_SD_CLK          (GPIO_NUM_15)
#define BSP_SD_CMD          (GPIO_NUM_7)
#define BSP_SD_D0           (GPIO_NUM_4)
#define MOUNT_POINT              "/sdcard"

#endif // _BOARD_CONFIG_H_
