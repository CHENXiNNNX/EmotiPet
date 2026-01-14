#pragma once

#include <cstdint>
#include <memory>
#include "driver/rmt_tx.h"
#include "driver/gpio.h"
#include "system/task/task.hpp"

namespace app::device::led
{
    // RGB颜色结构体
    struct Color
    {
        uint8_t r; // 红色分量 (0-255)
        uint8_t g; // 绿色分量 (0-255)
        uint8_t b; // 蓝色分量 (0-255)

        Color() : r(0), g(0), b(0) {}
        Color(uint8_t red, uint8_t green, uint8_t blue) : r(red), g(green), b(blue) {}
    };

    // LED控制类
    class WS2812
    {
    public:
        WS2812();
        ~WS2812();

        /**
         * @brief 设置闪烁参数
         * @param gpio_num GPIO引脚号
         * @param interval_ms 闪烁间隔时间（毫秒），-1表示常亮
         * @param count 闪烁次数，-1表示无限闪烁
         * @return true 成功，false 失败
         */
        bool setBlinkConfig(gpio_num_t gpio_num, int32_t interval_ms, int32_t count);

        /**
         * @brief 设置LED颜色
         * @param gpio_num GPIO引脚号
         * @param color 颜色值（RGB）
         * @return true 成功，false 失败
         */
        bool setColor(gpio_num_t gpio_num, const Color& color);

        /**
         * @brief 设置多个LED颜色（用于级联的WS2812）
         * @param gpio_num GPIO引脚号
         * @param colors 颜色数组
         * @param count LED数量
         * @return true 成功，false 失败
         */
        bool setColors(gpio_num_t gpio_num, const Color* colors, size_t count);

        /**
         * @brief 开始闪烁功能
         * @param gpio_num GPIO引脚号
         * @return true 成功，false 失败
         */
        bool startBlink(gpio_num_t gpio_num);

        /**
         * @brief 停止闪烁功能
         * @param gpio_num GPIO引脚号
         * @return true 成功，false 失败
         */
        bool stopBlink(gpio_num_t gpio_num);

        /**
         * @brief 设置LED亮度
         * @param brightness 亮度值（0-100），0为最暗（关闭），100为最亮
         * @return true 成功，false 失败（参数无效）
         * @note 亮度设置会影响后续所有颜色设置，包括闪烁功能
         */
        bool setBrightness(uint8_t brightness);

        /**
         * @brief 获取当前亮度
         * @return 当前亮度值（0-100）
         */
        uint8_t getBrightness() const
        {
            return brightness_;
        }

        /**
         * @brief 开始呼吸灯效果
         * @param gpio_num GPIO引脚号
         * @param color 呼吸灯颜色
         * @param cycle_ms 一个完整呼吸周期的时间（毫秒），默认2000ms
         * @param led_count LED数量（用于级联），默认1
         * @return true 成功，false 失败
         */
        bool startBreathing(gpio_num_t gpio_num, const Color& color, uint32_t cycle_ms = 2000, size_t led_count = 1);

        /**
         * @brief 停止呼吸灯效果
         * @param gpio_num GPIO引脚号
         * @return true 成功，false 失败
         */
        bool stopBreathing(gpio_num_t gpio_num);

        /**
         * @brief 更新呼吸灯颜色（运行时动态更新）
         * @param color 新的呼吸灯颜色
         * @return true 成功，false 失败（呼吸灯未运行）
         */
        bool updateBreathingColor(const Color& color);

    private:
        // RMT编码器句柄
        rmt_encoder_handle_t encoder_handle_;

        // RMT通道句柄
        rmt_channel_handle_t channel_handle_;

        // 当前GPIO引脚
        gpio_num_t current_gpio_;

        // 亮度值（0-100），默认100（最亮）
        uint8_t brightness_;

        // 闪烁参数
        struct BlinkConfig
        {
            int32_t interval_ms; // 闪烁间隔（毫秒），-1表示常亮
            int32_t count;       // 闪烁次数，-1表示无限
            Color   color;       // 闪烁颜色
            bool    is_running;  // 是否正在运行
        } blink_config_;

        // 闪烁任务对象
        std::unique_ptr<app::sys::task::Task> blink_task_;

        // 呼吸灯参数
        struct BreathingConfig
        {
            Color      color;      // 呼吸灯颜色
            uint32_t   cycle_ms;   // 一个完整呼吸周期的时间（毫秒）
            size_t     led_count;  // LED数量
            bool       is_running; // 是否正在运行
        } breathing_config_;

        // 呼吸灯任务对象
        std::unique_ptr<app::sys::task::Task> breathing_task_;

        // 初始化RMT通道
        bool initRMTChannel(gpio_num_t gpio_num);

        // 释放RMT通道
        void deinitRMTChannel();

        // 发送颜色数据到WS2812
        bool sendColor(const Color& color);

        // 发送多个LED颜色数据到WS2812
        bool sendColors(const Color* colors, size_t count);

        // 闪烁任务函数
        void blinkTaskFunction(void* param);

        // 呼吸灯任务函数
        void breathingTaskFunction(void* param);
    };

} // namespace app::device::led
