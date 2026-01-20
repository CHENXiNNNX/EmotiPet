#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <driver/i2c_master.h>
#include "cJSON.h"

namespace app
{
    namespace move
    {
        /**
         * @brief PCA9685 16 通道 PWM/舵机驱动芯片（Adafruit_PWMServoDriver 的 ESP-IDF 版本）
         *
         * - 使用 app::i2c::I2c 管理的 I2C 总线（GPIO17/18）
         * - 接口参考 Adafruit_PWMServoDriver，但底层实现使用 ESP-IDF I2C 驱动
         * - 提供基础 PWM 控制接口和按角度控制舵机的接口
         */
        class PCA9685
        {
        public:
            /// PCA9685 默认 I2C 地址（Adafruit 板卡通常为 0x40，可通过焊盘修改）
            static constexpr uint8_t DEFAULT_I2C_ADDR = 0x44;

            /**
             * @brief 初始化 PCA9685 设备
             * @param bus_handle 已经初始化好的 I2C master bus 句柄（从 app::i2c::I2c::getBusHandle() 获取）
             * @param i2c_addr   7bit I2C 地址，默认 0x40
             * @return true 成功，false 失败
             */
            static bool init(i2c_master_bus_handle_t bus_handle,
                             uint8_t                 i2c_addr = DEFAULT_I2C_ADDR);

            /**
             * @brief 反初始化（从 I2C 总线上移除设备）
             */
            static void deinit();

            /**
             * @brief 复位芯片（清空寄存器，恢复默认状态）
             * @return true 成功，false 失败
             */
            static bool reset();

            /**
             * @brief 设置 PWM 频率（典型舵机为 50Hz）
             * @param freq_hz 频率（Hz），建议范围 24 ~ 1526
             * @return true 成功，false 失败
             */
            static bool setPWMFreq(float freq_hz);

            /**
             * @brief 设置指定通道的 PWM 输出（原始 on/off 计数）
             * @param channel 通道号 0~15
             * @param on      0~4095，开始计数位置
             * @param off     0~4095，结束计数位置
             * @return true 成功，false 失败
             */
            static bool setPWM(uint8_t channel, uint16_t on, uint16_t off);

            /**
             * @brief 设置指定通道的 PWM 占空比（0~4095），可选反相
             * @param channel 通道号 0~15
             * @param val     0~4095，占空比值
             * @param invert  是否反相输出
             * @return true 成功，false 失败
             */
            static bool setPin(uint8_t channel, uint16_t val, bool invert = false);

            /**
             * @brief 按角度控制舵机（内部转换为 PWM 脉宽）
             * @param channel       通道号 0~15
             * @param angle_deg     目标角度（0~180）
             * @param min_pulse_us  最小脉宽（典型 500us）
             * @param max_pulse_us  最大脉宽（典型 2500us）
             * @param freq_hz       舵机 PWM 频率（典型 50Hz）
             * @return true 成功，false 失败
             */
            static bool setServoAngle(uint8_t channel,
                                      float   angle_deg,
                                      float   min_pulse_us = 500.0f,
                                      float   max_pulse_us = 2500.0f,
                                      float   freq_hz      = 50.0f);

            /**
             * @brief 缓动函数类型枚举
             */
            enum class EasingType
            {
                LINEAR,      // 线性（匀速）
                EASE_IN,     // 缓入（开始慢，逐渐加速）
                EASE_OUT,    // 缓出（开始快，逐渐减速）
                EASE_IN_OUT, // 缓入缓出（开始慢，中间快，结束慢）
                EASE_IN_QUAD,  // 二次缓入
                EASE_OUT_QUAD,  // 二次缓出
                EASE_IN_OUT_QUAD, // 二次缓入缓出
                EASE_IN_CUBIC,   // 三次缓入
                EASE_OUT_CUBIC,  // 三次缓出
                EASE_IN_OUT_CUBIC // 三次缓入缓出
            };

            /**
             * @brief 舵机变速转动（支持缓动函数）
             * @param channel       通道号 0~15
             * @param start_angle   起始角度（0~180）
             * @param end_angle     目标角度（0~180）
             * @param duration_ms   转动持续时间（毫秒）
             * @param easing_type   缓动函数类型（默认线性）
             * @param update_interval_ms 更新间隔（毫秒，默认20ms）
             * @param min_pulse_us  最小脉宽（典型 500us）
             * @param max_pulse_us  最大脉宽（典型 2500us）
             * @param freq_hz       舵机 PWM 频率（典型 50Hz）
             * @return true 成功，false 失败
             */
            static bool setServoAngleWithEasing(uint8_t    channel,
                                                float      start_angle,
                                                float      end_angle,
                                                uint32_t   duration_ms,
                                                EasingType easing_type         = EasingType::LINEAR,
                                                uint32_t   update_interval_ms  = 20,
                                                float      min_pulse_us        = 500.0f,
                                                float      max_pulse_us        = 2500.0f,
                                                float      freq_hz             = 50.0f);

            /**
             * @brief 检查是否已初始化
             * @return true 已初始化，false 未初始化
             */
            static bool isInitialized()
            {
                return initialized_ && (dev_handle_ != nullptr);
            }

        private:
            static bool                    initialized_;
            static i2c_master_bus_handle_t bus_handle_;
            static i2c_master_dev_handle_t dev_handle_;
            static uint8_t                 i2c_addr_;

            // 内部缓存当前 PWM 频率，用于角度到脉宽的转换
            static float current_freq_hz_;

            /**
             * @brief 读取一个 8 位寄存器
             */
            static bool read8(uint8_t reg_addr, uint8_t& value);

            /**
             * @brief 写入一个 8 位寄存器
             */
            static bool write8(uint8_t reg_addr, uint8_t value);
        };

        /**
         * @brief 单个舵机运动信息
         */
        struct ServoMotion
        {
            std::string move_part;  // 运动部位标识 (h1, h2, b1, b2等)
            uint8_t     channel;    // 对应的PCA9685通道号 (0-15)
            uint32_t    start_time; // 起始时间（毫秒，相对于第一个动作）
            uint8_t     angle;      // 目标角度 (0-180)
            uint32_t    duration;   // 持续时间（毫秒）
        };

        /**
         * @brief 运动部位到通道号的映射
         * @param move_part 运动部位标识 (h1, h2, b1, h3, b2等)
         * @return 对应的PCA9685通道号，如果未找到返回255 (b2返回254表示电机)
         */
        uint8_t mapMovePartToChannel(const std::string& move_part);

        /**
         * @brief 从JSON字符串解析运动数据
         * @param json_str JSON字符串
         * @param motions 输出：解析后的运动信息列表
         * @return true 解析成功，false 解析失败
         */
        bool parseMovementJson(const std::string& json_str, std::vector<ServoMotion>& motions);

        /**
         * @brief 执行运动序列（按照时间轴协调多个舵机运动）
         * @param motions 运动信息列表
         * @param update_interval_ms 更新间隔（毫秒），越小越平滑但CPU占用越高
         * @return true 执行成功，false 执行失败
         */
        bool executeMovements(const std::vector<ServoMotion>& motions, uint32_t update_interval_ms = 20);

        /**
         * @brief 从JSON字符串解析并执行运动序列（便于上层直接调用）
         * @param json_str JSON格式的运动数据字符串（type=mov_info）
         * @param update_interval_ms 更新间隔（毫秒），默认20ms
         * @return true 执行成功，false 解析或执行失败
         */
        bool executeMovementFromJson(const std::string& json_str, uint32_t update_interval_ms = 20);

    } // namespace move
} // namespace app

// ============================================================================
// 函数总结（按类别整理）
// ============================================================================
//
// 【PCA9685 类 - 设备初始化与管理】
//   - init()                    : 初始化 PCA9685 设备（I2C 总线连接）
//   - deinit()                  : 反初始化，从 I2C 总线上移除设备
//   - reset()                   : 复位芯片，清空寄存器，恢复默认状态
//   - isInitialized()           : 检查是否已初始化
//
// 【PCA9685 类 - PWM 基础控制】
//   - setPWMFreq()             : 设置 PWM 频率（典型舵机为 50Hz，范围 24~1526Hz）
//   - setPWM()                  : 设置指定通道的 PWM 输出（原始 on/off 计数，0~4095）
//   - setPin()                  : 设置指定通道的 PWM 占空比（0~4095），可选反相输出
//
// 【PCA9685 类 - 舵机角度控制】
//   - setServoAngle()           : 按角度控制舵机（内部转换为 PWM 脉宽，0~180度）
//   - setServoAngleWithEasing() : 舵机变速转动（支持10种缓动函数，实现平滑运动）
//
// 【PCA9685 类 - 内部寄存器操作（私有）】
//   - read8()                   : 读取一个 8 位寄存器
//   - write8()                  : 写入一个 8 位寄存器
//
// 【全局函数 - 运动数据解析与执行】
//   - mapMovePartToChannel()    : 运动部位到通道号的映射（h1->0, h2->1, b1->2, h3->3, b2->254）
//   - parseMovementJson()       : 从JSON字符串解析运动数据（支持任意数量的servo_XX配置）
//   - executeMovements()        : 执行运动序列（按照时间轴协调多个舵机/电机运动）
//
// 【数据结构】
//   - ServoMotion               : 单个舵机运动信息结构体（move_part, channel, start_time, angle, duration）
//   - EasingType                : 缓动函数类型枚举（LINEAR, EASE_IN, EASE_OUT, EASE_IN_OUT等10种）
//
// ============================================================================
// EasingType 相关函数详细说明
// ============================================================================
//
// 【枚举类型 - EasingType】
//   定义在 PCA9685 类内部，包含10种缓动函数类型：
//   - LINEAR              : 线性（匀速运动，t）
//   - EASE_IN             : 缓入（开始慢，逐渐加速，t²）
//   - EASE_OUT            : 缓出（开始快，逐渐减速，1-(1-t)²）
//   - EASE_IN_OUT         : 缓入缓出（开始慢，中间快，结束慢，二次曲线）
//   - EASE_IN_QUAD        : 二次缓入（t²，与EASE_IN相同）
//   - EASE_OUT_QUAD       : 二次缓出（1-(1-t)²，与EASE_OUT相同）
//   - EASE_IN_OUT_QUAD    : 二次缓入缓出（与EASE_IN_OUT相同）
//   - EASE_IN_CUBIC       : 三次缓入（t³，更平滑的启动）
//   - EASE_OUT_CUBIC      : 三次缓出（1-(1-t)³，更平滑的停止）
//   - EASE_IN_OUT_CUBIC   : 三次缓入缓出（最平滑的运动曲线）
//
// 【公共函数 - setServoAngleWithEasing()】
//   功能：舵机变速转动，支持10种缓动函数类型
//   参数：
//     - channel            : 通道号 0~15
//     - start_angle        : 起始角度（0~180度）
//     - end_angle         : 目标角度（0~180度）
//     - duration_ms        : 转动持续时间（毫秒）
//     - easing_type        : 缓动函数类型（默认 LINEAR）
//     - update_interval_ms : 更新间隔（毫秒，默认20ms，越小越平滑但CPU占用越高）
//     - min_pulse_us       : 最小脉宽（典型 500us）
//     - max_pulse_us       : 最大脉宽（典型 2500us）
//     - freq_hz            : 舵机 PWM 频率（典型 50Hz）
//   返回值：true 成功，false 失败
//   说明：
//     - 函数会阻塞直到转动完成
//     - 内部调用 applyEasing() 计算缓动进度
//     - 通过线性插值计算每个时间点的角度
//     - 如果 duration_ms=0，直接设置到目标角度
//
// 【内部辅助函数 - applyEasing()】（在 move.cc 中实现，静态私有函数）
//   功能：将线性进度 t (0.0~1.0) 转换为缓动后的进度值
//   参数：
//     - t    : 线性进度 (0.0~1.0)
//     - type : 缓动类型（EasingType 枚举）
//   返回值：缓动后的进度值 (0.0~1.0)
//   说明：
//     - 根据不同的 EasingType 应用不同的数学公式
//     - 输入 t 会被限制在 0.0~1.0 范围内
//     - 输出值也在 0.0~1.0 范围内
//
// 【使用示例】
//   // 线性转动：45度 -> 135度，持续2秒
//   PCA9685::setServoAngleWithEasing(0, 45.0f, 135.0f, 2000, 
//                                   PCA9685::EasingType::LINEAR);
//
//   // 缓入缓出转动：135度 -> 45度，持续2秒，更新间隔20ms
//   PCA9685::setServoAngleWithEasing(0, 135.0f, 45.0f, 2000,
//                                   PCA9685::EasingType::EASE_IN_OUT, 20);
//
//   // 三次缓入缓出（最平滑）：0度 -> 180度，持续3秒
//   PCA9685::setServoAngleWithEasing(0, 0.0f, 180.0f, 3000,
//                                   PCA9685::EasingType::EASE_IN_OUT_CUBIC);
//
// ============================================================================

