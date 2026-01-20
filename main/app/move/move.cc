#include "move.hpp"

#include <cmath>
#include <cstring>
#include <algorithm>
#include <esp_log.h>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include <driver/ledc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "cJSON.h"

// 寄存器地址定义，参考 Adafruit_PWMServoDriver
static constexpr uint8_t PCA9685_MODE1    = 0x00;
static constexpr uint8_t PCA9685_PRESCALE = 0xFE;

static constexpr uint8_t LED0_ON_L  = 0x06;
static constexpr uint8_t LED0_ON_H  = 0x07;
static constexpr uint8_t LED0_OFF_L = 0x08;
static constexpr uint8_t LED0_OFF_H = 0x09;

static const char* const TAG = "PCA9685";

// b2 电机控制相关常量
static constexpr gpio_num_t MOTOR_B2_PWM_GPIO = GPIO_NUM_6;   // PWM 速度控制引脚
static constexpr gpio_num_t MOTOR_B2_DIR_GPIO = GPIO_NUM_46;  // 方向控制引脚
static constexpr ledc_channel_t MOTOR_B2_LEDC_CHANNEL = LEDC_CHANNEL_0;
static constexpr ledc_timer_t MOTOR_B2_LEDC_TIMER = LEDC_TIMER_0;
static constexpr uint32_t MOTOR_B2_PWM_FREQ_HZ = 5000;  // 5kHz PWM 频率（适合电机控制）
static constexpr ledc_timer_bit_t MOTOR_B2_PWM_RESOLUTION = LEDC_TIMER_10_BIT;  // 10位分辨率 (0-1023)
static bool motor_b2_initialized_ = false;

namespace app
{
    namespace move
    {
        // 静态成员定义
        bool                    PCA9685::initialized_    = false;
        i2c_master_bus_handle_t PCA9685::bus_handle_     = nullptr;
        i2c_master_dev_handle_t PCA9685::dev_handle_     = nullptr;
        uint8_t                 PCA9685::i2c_addr_       = PCA9685::DEFAULT_I2C_ADDR;
        float                   PCA9685::current_freq_hz_ = 0.0f;

        bool PCA9685::init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr)
        {
            if (bus_handle == nullptr)
            {
                ESP_LOGE(TAG, "I2C 总线句柄为空");
                return false;
            }

            // 如果已经初始化，先反初始化
            if (initialized_ && dev_handle_ != nullptr)
            {
                ESP_LOGW(TAG, "PCA9685 已初始化，先反初始化");
                deinit();
            }

            bus_handle_ = bus_handle;
            i2c_addr_   = i2c_addr;

            // 配置 I2C 设备
            i2c_device_config_t dev_cfg = {};
            dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
            dev_cfg.device_address      = i2c_addr_;
            dev_cfg.scl_speed_hz        = 400000; // 400kHz

            // 添加设备到 I2C 总线
            esp_err_t ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "添加 PCA9685 I2C 设备失败: %s", esp_err_to_name(ret));
                dev_handle_  = nullptr;
                initialized_ = false;
                return false;
            }

            initialized_     = true;
            current_freq_hz_ = 0.0f;

            ESP_LOGI(TAG, "PCA9685 初始化成功 (I2C 地址: 0x%02X)", i2c_addr_);

            // 复位芯片
            if (!reset())
            {
                ESP_LOGE(TAG, "PCA9685 复位失败");
                return false;
            }

            // 默认设置为 50Hz，适合舵机
            if (!setPWMFreq(50.0f))
            {
                ESP_LOGE(TAG, "PCA9685 设置默认频率 50Hz 失败");
                return false;
            }

            return true;
        }

        void PCA9685::deinit()
        {
            if (dev_handle_ != nullptr)
            {
                i2c_master_bus_rm_device(dev_handle_);
                dev_handle_ = nullptr;
            }
            bus_handle_      = nullptr;
            initialized_     = false;
            current_freq_hz_ = 0.0f;
            ESP_LOGI(TAG, "PCA9685 已反初始化");
        }

        bool PCA9685::reset()
        {
            if (!initialized_ || dev_handle_ == nullptr)
            {
                ESP_LOGE(TAG, "PCA9685 未初始化，无法复位");
                return false;
            }

            // MODE1 写 0x00 即可复位
            if (!write8(PCA9685_MODE1, 0x00))
            {
                ESP_LOGE(TAG, "写入 MODE1 失败，无法复位");
                return false;
            }

            // 等待一小段时间让芯片稳定
            vTaskDelay(pdMS_TO_TICKS(10));
            return true;
        }

        bool PCA9685::setPWMFreq(float freq_hz)
        {
            if (!initialized_ || dev_handle_ == nullptr)
            {
                ESP_LOGE(TAG, "PCA9685 未初始化，无法设置频率");
                return false;
            }

            if (freq_hz <= 0.0f)
            {
                ESP_LOGE(TAG, "无效的频率: %f", static_cast<double>(freq_hz));
                return false;
            }

            // 参考 Adafruit 公式：prescale = round(25MHz / (4096 * freq)) - 1
            float prescaleval = 25000000.0f; // 25MHz
            prescaleval /= 4096.0f;
            prescaleval /= freq_hz;
            prescaleval -= 1.0f;

            uint8_t prescale = static_cast<uint8_t>(std::floor(prescaleval + 0.5f));

            uint8_t oldmode = 0;
            if (!read8(PCA9685_MODE1, oldmode))
            {
                ESP_LOGE(TAG, "读取 MODE1 失败，无法设置频率");
                return false;
            }

            uint8_t newmode = (oldmode & 0x7F) | 0x10; // 进入 sleep
            if (!write8(PCA9685_MODE1, newmode))
            {
                ESP_LOGE(TAG, "写入 MODE1 失败（进入 sleep）");
                return false;
            }

            if (!write8(PCA9685_PRESCALE, prescale))
            {
                ESP_LOGE(TAG, "写入 PRESCALE 失败");
                return false;
            }

            // 退出 sleep
            if (!write8(PCA9685_MODE1, oldmode))
            {
                ESP_LOGE(TAG, "恢复 MODE1 失败");
                return false;
            }

            vTaskDelay(pdMS_TO_TICKS(5));

            // 开启自动递增
            if (!write8(PCA9685_MODE1, oldmode | 0xA1))
            {
                ESP_LOGE(TAG, "设置 MODE1 自动递增失败");
                return false;
            }

            current_freq_hz_ = freq_hz;
            ESP_LOGI(TAG, "PCA9685 PWM 频率设置为: %.2f Hz", static_cast<double>(freq_hz));
            return true;
        }

        bool PCA9685::setPWM(uint8_t channel, uint16_t on, uint16_t off)
        {
            if (!initialized_ || dev_handle_ == nullptr)
            {
                ESP_LOGE(TAG, "PCA9685 未初始化，无法设置 PWM");
                return false;
            }

            if (channel >= 16)
            {
                ESP_LOGE(TAG, "通道号超出范围: %u (有效范围 0~15)", static_cast<unsigned>(channel));
                return false;
            }

            uint8_t buffer[5];
            buffer[0] = LED0_ON_L + 4 * channel;
            buffer[1] = static_cast<uint8_t>(on & 0xFF);
            buffer[2] = static_cast<uint8_t>((on >> 8) & 0x0F);
            buffer[3] = static_cast<uint8_t>(off & 0xFF);
            buffer[4] = static_cast<uint8_t>((off >> 8) & 0x0F);

            esp_err_t ret = i2c_master_transmit(dev_handle_, buffer, sizeof(buffer), 1000);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "I2C 发送 PWM 数据失败: %s", esp_err_to_name(ret));
                return false;
            }

            return true;
        }

        bool PCA9685::setPin(uint8_t channel, uint16_t val, bool invert)
        {
            // 限制在 0~4095
            if (val > 4095)
            {
                val = 4095;
            }

            if (invert)
            {
                if (val == 0)
                {
                    // 全亮
                    return setPWM(channel, 4096, 0);
                }
                else if (val == 4095)
                {
                    // 全灭
                    return setPWM(channel, 0, 4096);
                }
                else
                {
                    return setPWM(channel, 0, 4095 - val);
                }
            }
            else
            {
                if (val == 4095)
                {
                    // 全亮
                    return setPWM(channel, 4096, 0);
                }
                else if (val == 0)
                {
                    // 全灭
                    return setPWM(channel, 0, 4096);
                }
                else
                {
                    return setPWM(channel, 0, val);
                }
            }
        }

        bool PCA9685::setServoAngle(uint8_t channel,
                                    float   angle_deg,
                                    float   min_pulse_us,
                                    float   max_pulse_us,
                                    float   freq_hz)
        {
            if (!initialized_ || dev_handle_ == nullptr)
            {
                ESP_LOGE(TAG, "PCA9685 未初始化，无法设置舵机角度");
                return false;
            }

            if (angle_deg < 0.0f)
            {
                angle_deg = 0.0f;
            }
            if (angle_deg > 180.0f)
            {
                angle_deg = 180.0f;
            }

            if (freq_hz <= 0.0f)
            {
                freq_hz = 50.0f;
            }

            // 如果当前频率和期望不一致，则重新设置
            if (std::fabs(current_freq_hz_ - freq_hz) > 0.1f)
            {
                if (!setPWMFreq(freq_hz))
                {
                    return false;
                }
            }

            // 一个周期的总时间（us）
            float period_us = 1000000.0f / freq_hz;
            // 每个计数对应的时间
            float us_per_count = period_us / 4096.0f;

            // 线性插值角度对应的脉宽
            float pulse_us = min_pulse_us +
                             (max_pulse_us - min_pulse_us) * (angle_deg / 180.0f);

            // 转换为计数值
            uint16_t pulse_counts =
                static_cast<uint16_t>(std::round(pulse_us / us_per_count));

            // 使用从 0 开始的 on=0/off=pulse_counts
            return setPWM(channel, 0, pulse_counts);
        }

        /**
         * @brief 缓动函数：将线性进度 t (0.0~1.0) 转换为缓动后的进度值
         * @param t 线性进度 (0.0~1.0)
         * @param type 缓动类型
         * @return 缓动后的进度值 (0.0~1.0)
         */
        static float applyEasing(float t, PCA9685::EasingType type)
        {
            // 限制 t 在 0.0~1.0 范围内
            t = std::clamp(t, 0.0f, 1.0f);

            switch (type)
            {
                case PCA9685::EasingType::LINEAR:
                    return t;

                case PCA9685::EasingType::EASE_IN:
                    // 二次缓入：t^2
                    return t * t;

                case PCA9685::EasingType::EASE_OUT:
                    // 二次缓出：1 - (1-t)^2
                    {
                        float t1 = 1.0f - t;
                        return 1.0f - (t1 * t1);
                    }

                case PCA9685::EasingType::EASE_IN_OUT:
                    // 二次缓入缓出
                    if (t < 0.5f)
                    {
                        return 2.0f * t * t;
                    }
                    else
                    {
                        float t1 = (2.0f * t) - 1.0f;
                        float t2 = 1.0f - t1;
                        return 1.0f - (0.5f * t2 * t2);
                    }

                case PCA9685::EasingType::EASE_IN_QUAD:
                    return t * t;

                case PCA9685::EasingType::EASE_OUT_QUAD:
                    {
                        float t1 = 1.0f - t;
                        return 1.0f - (t1 * t1);
                    }

                case PCA9685::EasingType::EASE_IN_OUT_QUAD:
                    if (t < 0.5f)
                    {
                        return 2.0f * t * t;
                    }
                    else
                    {
                        float t1 = (2.0f * t) - 1.0f;
                        float t2 = 1.0f - t1;
                        return 1.0f - (0.5f * t2 * t2);
                    }

                case PCA9685::EasingType::EASE_IN_CUBIC:
                    // 三次缓入：t^3
                    return t * t * t;

                case PCA9685::EasingType::EASE_OUT_CUBIC:
                    // 三次缓出：1 - (1-t)^3
                    {
                        float t1 = 1.0f - t;
                        return 1.0f - (t1 * t1 * t1);
                    }

                case PCA9685::EasingType::EASE_IN_OUT_CUBIC:
                    // 三次缓入缓出
                    if (t < 0.5f)
                    {
                        return 4.0f * t * t * t;
                    }
                    else
                    {
                        float t1 = (2.0f * t) - 1.0f;
                        float t2 = 1.0f - t1;
                        return 1.0f - (0.5f * t2 * t2 * t2);
                    }

                default:
                    return t; // 默认线性
            }
        }

        bool PCA9685::setServoAngleWithEasing(uint8_t    channel,
                                              float      start_angle,
                                              float      end_angle,
                                              uint32_t   duration_ms,
                                              EasingType easing_type,
                                              uint32_t   update_interval_ms,
                                              float      min_pulse_us,
                                              float      max_pulse_us,
                                              float      freq_hz)
        {
            if (!initialized_ || dev_handle_ == nullptr)
            {
                ESP_LOGE(TAG, "PCA9685 未初始化，无法设置舵机角度");
                return false;
            }

            // 参数验证
            start_angle = std::clamp(start_angle, 0.0f, 180.0f);
            end_angle   = std::clamp(end_angle, 0.0f, 180.0f);

            if (duration_ms == 0)
            {
                // 如果持续时间为0，直接设置目标角度
                return setServoAngle(channel, end_angle, min_pulse_us, max_pulse_us, freq_hz);
            }

            if (update_interval_ms == 0)
            {
                update_interval_ms = 20; // 默认20ms
            }

            // 如果起始角度和目标角度相同，直接返回
            if (std::fabs(start_angle - end_angle) < 0.1f)
            {
                return setServoAngle(channel, end_angle, min_pulse_us, max_pulse_us, freq_hz);
            }

            // 计算总步数
            uint32_t total_steps = (duration_ms + update_interval_ms - 1) / update_interval_ms;
            if (total_steps == 0)
            {
                total_steps = 1;
            }

            // 执行变速转动
            uint32_t elapsed_ms = 0;
            while (elapsed_ms < duration_ms)
            {
                // 计算线性进度 (0.0 ~ 1.0)
                float linear_progress = static_cast<float>(elapsed_ms) / static_cast<float>(duration_ms);

                // 应用缓动函数
                float eased_progress = applyEasing(linear_progress, easing_type);

                // 计算当前角度（线性插值）
                float angle_diff = end_angle - start_angle;
                float current_angle = start_angle + (angle_diff * eased_progress);

                // 设置舵机角度
                if (!setServoAngle(channel, current_angle, min_pulse_us, max_pulse_us, freq_hz))
                {
                    ESP_LOGE(TAG, "设置舵机角度失败 (通道 %d, 角度 %.1f)", channel, current_angle);
                    return false;
                }

                // 延时
                vTaskDelay(pdMS_TO_TICKS(update_interval_ms));
                elapsed_ms += update_interval_ms;
            }

            // 确保到达最终角度
            setServoAngle(channel, end_angle, min_pulse_us, max_pulse_us, freq_hz);

            return true;
        }

        bool PCA9685::read8(uint8_t reg_addr, uint8_t& value)
        {
            if (dev_handle_ == nullptr)
            {
                ESP_LOGE(TAG, "设备未初始化，无法读取寄存器");
                return false;
            }

            esp_err_t ret =
                i2c_master_transmit_receive(dev_handle_, &reg_addr, 1, &value, 1, 1000);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "I2C 读取寄存器 0x%02X 失败: %s", reg_addr,
                         esp_err_to_name(ret));
                return false;
            }

            return true;
        }

        bool PCA9685::write8(uint8_t reg_addr, uint8_t value)
        {
            if (dev_handle_ == nullptr)
            {
                ESP_LOGE(TAG, "设备未初始化，无法写入寄存器");
                return false;
            }

            uint8_t buf[2] = {reg_addr, value};
            esp_err_t ret  = i2c_master_transmit(dev_handle_, buf, sizeof(buf), 1000);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "I2C 写入寄存器 0x%02X 失败: %s", reg_addr,
                         esp_err_to_name(ret));
                return false;
            }

            return true;
        }

        // ========== 运动控制函数实现 ==========

        bool initMotorB2()
        {
            if (motor_b2_initialized_)
            {
                ESP_LOGW(TAG, "b2 电机已初始化");
                return true;
            }

            // 配置 GPIO6 为 PWM 输出
            gpio_config_t gpio_cfg = {};
            gpio_cfg.pin_bit_mask = (1ULL << MOTOR_B2_PWM_GPIO) | (1ULL << MOTOR_B2_DIR_GPIO);
            gpio_cfg.mode = GPIO_MODE_OUTPUT;
            gpio_cfg.pull_up_en = GPIO_PULLUP_DISABLE;
            gpio_cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
            gpio_cfg.intr_type = GPIO_INTR_DISABLE;
            esp_err_t ret = gpio_config(&gpio_cfg);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "配置 b2 电机 GPIO 失败: %s", esp_err_to_name(ret));
                return false;
            }

            // 初始化 LEDC 定时器
            ledc_timer_config_t timer_cfg = {};
            timer_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
            timer_cfg.timer_num = MOTOR_B2_LEDC_TIMER;
            timer_cfg.duty_resolution = MOTOR_B2_PWM_RESOLUTION;
            timer_cfg.freq_hz = MOTOR_B2_PWM_FREQ_HZ;
            timer_cfg.clk_cfg = LEDC_AUTO_CLK;
            ret = ledc_timer_config(&timer_cfg);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "配置 b2 电机 LEDC 定时器失败: %s", esp_err_to_name(ret));
                return false;
            }

            // 初始化 LEDC 通道
            ledc_channel_config_t channel_cfg = {};
            channel_cfg.speed_mode = LEDC_LOW_SPEED_MODE;
            channel_cfg.channel = MOTOR_B2_LEDC_CHANNEL;
            channel_cfg.timer_sel = MOTOR_B2_LEDC_TIMER;
            channel_cfg.intr_type = LEDC_INTR_DISABLE;
            channel_cfg.gpio_num = MOTOR_B2_PWM_GPIO;
            channel_cfg.duty = 0;
            channel_cfg.hpoint = 0;
            ret = ledc_channel_config(&channel_cfg);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "配置 b2 电机 LEDC 通道失败: %s", esp_err_to_name(ret));
                return false;
            }

            // 设置方向引脚初始状态（停止）
            gpio_set_level(MOTOR_B2_DIR_GPIO, 0);

            motor_b2_initialized_ = true;
            ESP_LOGI(TAG, "b2 电机初始化成功 (PWM: GPIO%d, DIR: GPIO%d)", MOTOR_B2_PWM_GPIO, MOTOR_B2_DIR_GPIO);
            return true;
        }

        bool setMotorB2Speed(float speed_percent)
        {
            if (!motor_b2_initialized_)
            {
                ESP_LOGE(TAG, "b2 电机未初始化");
                return false;
            }

            // 限制速度范围 -100% ~ 100%
            speed_percent = std::clamp(speed_percent, -100.0f, 100.0f);

            // 计算 PWM 占空比（绝对值）
            float abs_speed = std::fabs(speed_percent);
            uint32_t max_duty = (1U << 10) - 1;  // 1023 for 10-bit resolution
            uint32_t duty = static_cast<uint32_t>((abs_speed / 100.0f) * max_duty);

            // 如果速度为 0，确保完全停止
            if (abs_speed < 0.1f)
            {
                duty = 0;
                // 停止时，将方向引脚设置为低电平，确保电机完全停止
                gpio_set_level(MOTOR_B2_DIR_GPIO, 0);
            }
            else
            {
                // 设置方向：正数为正转，负数为反转
                bool direction = (speed_percent >= 0.0f);
                gpio_set_level(MOTOR_B2_DIR_GPIO, direction ? 1 : 0);
            }

            // 设置 PWM 占空比
            esp_err_t ret = ledc_set_duty(LEDC_LOW_SPEED_MODE, MOTOR_B2_LEDC_CHANNEL, duty);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "设置 b2 电机 PWM 占空比失败: %s", esp_err_to_name(ret));
                return false;
            }

            ret = ledc_update_duty(LEDC_LOW_SPEED_MODE, MOTOR_B2_LEDC_CHANNEL);
            if (ret != ESP_OK)
            {
                ESP_LOGE(TAG, "更新 b2 电机 PWM 占空比失败: %s", esp_err_to_name(ret));
                return false;
            }

            //ESP_LOGI(TAG, "b2 电机速度: %.1f%%, PWM duty: %lu/%lu, DIR: %d", 
                    // speed_percent, duty, max_duty, gpio_get_level(MOTOR_B2_DIR_GPIO));
            return true;
        }

        uint8_t mapMovePartToChannel(const std::string& move_part)
        {
            // 运动部位到通道号的映射
            // h1 -> 0 (头1), h2 -> 1 (头2), b1 -> 2 (身体1), h3 -> 3 (头3), b2 -> 254 (电机，特殊值)
            if (move_part == "h1")
                return 0;
            else if (move_part == "h2")
                return 1;
            else if (move_part == "b1")
                return 2;
            else if (move_part == "h3")
                return 3;
            else if (move_part == "b2")
                return 254; // 特殊值，表示 b2 是直流电机
            else
            {
                ESP_LOGW(TAG, "未知的运动部位: %s", move_part.c_str());
                return 255; // 无效通道号
            }
        }

        bool parseMovementJson(const std::string& json_str, std::vector<ServoMotion>& motions)
        {
            motions.clear();

            cJSON* root = cJSON_Parse(json_str.c_str());
            if (!root)
            {
                ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                return false;
            }

            // 验证类型
            cJSON* type_item = cJSON_GetObjectItem(root, "type");
            if (!type_item || !cJSON_IsString(type_item) ||
                std::string(cJSON_GetStringValue(type_item)) != "mov_info")
            {
                ESP_LOGE(TAG, "JSON 类型不是 mov_info");
                cJSON_Delete(root);
                return false;
            }

            // 获取 data 对象
            cJSON* data_item = cJSON_GetObjectItem(root, "data");
            if (!data_item || !cJSON_IsObject(data_item))
            {
                ESP_LOGE(TAG, "缺少 data 字段或类型错误");
                cJSON_Delete(root);
                return false;
            }

            // 遍历所有 servo_XX 键（servo_01, servo_02, ... 代表第1步、第2步等，支持任意数量的步数）
            cJSON* servo_item = nullptr;
            int step_count = 0;
            cJSON_ArrayForEach(servo_item, data_item)
            {
                if (!cJSON_IsObject(servo_item))
                    continue;

                step_count++;
                ServoMotion motion;

                // 解析 move_part
                cJSON* move_part_item = cJSON_GetObjectItem(servo_item, "move_part");
                if (!move_part_item || !cJSON_IsString(move_part_item))
                {
                    ESP_LOGW(TAG, "跳过缺少 move_part 的舵机配置");
                    continue;
                }
                motion.move_part = cJSON_GetStringValue(move_part_item);
                motion.channel   = mapMovePartToChannel(motion.move_part);
                if (motion.channel == 255)
                {
                    ESP_LOGW(TAG, "跳过无效的运动部位: %s", motion.move_part.c_str());
                    continue;
                }

                // 解析 start_time
                cJSON* start_time_item = cJSON_GetObjectItem(servo_item, "start_time");
                if (start_time_item && cJSON_IsString(start_time_item))
                {
                    motion.start_time = static_cast<uint32_t>(std::stoul(cJSON_GetStringValue(start_time_item)));
                }
                else if (start_time_item && cJSON_IsNumber(start_time_item))
                {
                    motion.start_time = static_cast<uint32_t>(cJSON_GetNumberValue(start_time_item));
                }
                else
                {
                    motion.start_time = 0;
                }

                // 解析 angle
                cJSON* angle_item = cJSON_GetObjectItem(servo_item, "angle");
                if (angle_item && cJSON_IsNumber(angle_item))
                {
                    int angle_val = static_cast<int>(cJSON_GetNumberValue(angle_item));
                    motion.angle  = static_cast<uint8_t>(std::clamp(angle_val, 0, 180));
                }
                else
                {
                    ESP_LOGW(TAG, "缺少 angle 字段，使用默认值 90");
                    motion.angle = 90;
                }

                // 解析 duration
                cJSON* duration_item = cJSON_GetObjectItem(servo_item, "duration");
                if (duration_item && cJSON_IsNumber(duration_item))
                {
                    motion.duration = static_cast<uint32_t>(cJSON_GetNumberValue(duration_item));
                }
                else if (duration_item && cJSON_IsString(duration_item))
                {
                    motion.duration = static_cast<uint32_t>(std::stoul(cJSON_GetStringValue(duration_item)));
                }
                else
                {
                    ESP_LOGW(TAG, "缺少 duration 字段，使用默认值 1000ms");
                    motion.duration = 1000;
                }

                motions.push_back(motion);
                ESP_LOGI(TAG, "解析第%d步: %s -> ch%d, start=%lums, angle=%d°, duration=%lums",
                         step_count, motion.move_part.c_str(), motion.channel, motion.start_time, motion.angle,
                         motion.duration);
            }

            cJSON_Delete(root);

            if (motions.empty())
            {
                ESP_LOGE(TAG, "未找到有效的舵机运动配置");
                return false;
            }

            // 按 start_time 排序
            std::sort(motions.begin(), motions.end(),
                      [](const ServoMotion& a, const ServoMotion& b) { return a.start_time < b.start_time; });

            ESP_LOGI(TAG, "成功解析 %zu 个舵机运动配置", motions.size());
            return true;
        }

        bool executeMovements(const std::vector<ServoMotion>& motions, uint32_t update_interval_ms)
        {
            if (motions.empty())
            {
                ESP_LOGE(TAG, "运动序列为空");
                return false;
            }

            if (!PCA9685::isInitialized())
            {
                ESP_LOGE(TAG, "PCA9685 未初始化，无法执行运动");
                return false;
            }

            // 检查是否需要初始化 b2 电机
            bool need_motor_b2 = false;
            for (const auto& motion : motions)
            {
                if (motion.move_part == "b2")
                {
                    need_motor_b2 = true;
                    break;
                }
            }

            if (need_motor_b2 && !motor_b2_initialized_)
            {
                if (!initMotorB2())
                {
                    ESP_LOGE(TAG, "b2 电机初始化失败");
                    return false;
                }
            }

            // 计算总时长（最后一个动作的开始时间 + 持续时间）
            uint32_t total_duration = 0;
            for (const auto& motion : motions)
            {
                uint32_t end_time = motion.start_time + motion.duration;
                if (end_time > total_duration)
                {
                    total_duration = end_time;
                }
            }

            ESP_LOGI(TAG, "开始执行运动序列，总时长: %lu ms，更新间隔: %lu ms", total_duration,
                     update_interval_ms);

            // 记录每个通道的起始角度（假设当前角度为90度）
            uint8_t start_angles[16] = {90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90, 90};

            // 记录 b2 电机的起始速度（0%）
            float start_speed_b2 = 0.0f;

            // 记录每个通道当前正在执行的运动
            struct ChannelState
            {
                bool     is_active;
                uint32_t start_time;
                uint32_t duration;
                uint8_t  start_angle;
                uint8_t  target_angle;
            };
            ChannelState channel_states[16] = {};

            // b2 电机状态（速度控制）
            struct MotorB2State
            {
                bool     is_active;
                uint32_t start_time;
                uint32_t duration;
                float    start_speed;  // 起始速度百分比
                float    target_speed; // 目标速度百分比
            };
            MotorB2State motor_b2_state = {};

            uint32_t current_time = 0;

            while (current_time <= total_duration)
            {
                // 检查是否有新的运动需要开始
                for (const auto& motion : motions)
                {
                    // 处理 b2 电机（特殊处理）
                    if (motion.move_part == "b2")
                    {
                        if (motion.start_time <= current_time &&
                            current_time < motion.start_time + motion.duration)
                        {
                            // 运动正在进行中，更新电机状态
                            if (!motor_b2_state.is_active ||
                                motor_b2_state.start_time != motion.start_time)
                            {
                                // 新运动开始
                                motor_b2_state.is_active = true;
                                motor_b2_state.start_time = motion.start_time;
                                motor_b2_state.duration = motion.duration;
                                motor_b2_state.start_speed = start_speed_b2;
                                // angle (0-180) 映射到速度 (-100% ~ 100%)
                                // 0 -> -100%, 90 -> 0%, 180 -> 100%
                                float speed_percent = ((motion.angle / 180.0f) - 0.5f) * 200.0f;
                                motor_b2_state.target_speed = speed_percent;
                            }
                        }
                        else if (current_time >= motion.start_time + motion.duration)
                        {
                            // 运动已完成，更新起始速度为最终速度
                            if (motor_b2_state.start_time == motion.start_time)
                            {
                                float speed_percent = ((motion.angle / 180.0f) - 0.5f) * 200.0f;
                                start_speed_b2 = speed_percent;
                                motor_b2_state.is_active = false;
                            }
                        }
                    }
                    // 处理舵机通道（h1, h2, b1）
                    else if (motion.channel < 16)
                    {
                        if (motion.start_time <= current_time &&
                            current_time < motion.start_time + motion.duration)
                        {
                            // 运动正在进行中，更新通道状态
                            if (!channel_states[motion.channel].is_active ||
                                channel_states[motion.channel].start_time != motion.start_time)
                            {
                                // 新运动开始
                                channel_states[motion.channel].is_active   = true;
                                channel_states[motion.channel].start_time   = motion.start_time;
                                channel_states[motion.channel].duration    = motion.duration;
                                channel_states[motion.channel].start_angle = start_angles[motion.channel];
                                channel_states[motion.channel].target_angle = motion.angle;
                            }
                        }
                        else if (current_time >= motion.start_time + motion.duration)
                        {
                            // 运动已完成，更新起始角度为最终角度
                            if (channel_states[motion.channel].start_time == motion.start_time)
                            {
                                start_angles[motion.channel] = motion.angle;
                                channel_states[motion.channel].is_active = false;
                            }
                        }
                    }
                }

                // 更新所有活跃通道的角度（线性插值）
                for (int ch = 0; ch < 16; ch++)
                {
                    if (channel_states[ch].is_active)
                    {
                        uint32_t elapsed = current_time - channel_states[ch].start_time;
                        if (elapsed >= channel_states[ch].duration)
                        {
                            // 运动完成，设置为目标角度
                            PCA9685::setServoAngle(ch, channel_states[ch].target_angle);
                            channel_states[ch].is_active = false;
                            start_angles[ch]             = channel_states[ch].target_angle;
                        }
                        else
                        {
                            // 线性插值计算当前角度
                            float progress = static_cast<float>(elapsed) / static_cast<float>(channel_states[ch].duration);
                            float current_angle = channel_states[ch].start_angle +
                                                  ((channel_states[ch].target_angle - channel_states[ch].start_angle) *
                                                      progress);
                            PCA9685::setServoAngle(ch, static_cast<uint8_t>(std::round(current_angle)));
                        }
                    }
                }

                // 更新 b2 电机的速度（线性插值）
                if (motor_b2_state.is_active)
                {
                    uint32_t elapsed = current_time - motor_b2_state.start_time;
                    if (elapsed >= motor_b2_state.duration)
                    {
                        // 运动完成，设置为目标速度
                        setMotorB2Speed(motor_b2_state.target_speed);
                        motor_b2_state.is_active = false;
                        start_speed_b2 = motor_b2_state.target_speed;
                    }
                    else
                    {
                        // 线性插值计算当前速度
                        float progress = static_cast<float>(elapsed) / static_cast<float>(motor_b2_state.duration);
                        float current_speed = motor_b2_state.start_speed +
                                              ((motor_b2_state.target_speed - motor_b2_state.start_speed) * progress);
                        setMotorB2Speed(current_speed);
                    }
                }

                vTaskDelay(pdMS_TO_TICKS(update_interval_ms));
                current_time += update_interval_ms;
            }

            // 确保所有运动都到达最终位置
            for (const auto& motion : motions)
            {
                if (motion.move_part == "b2")
                {
                    // b2 电机：angle (0-180) 映射到速度 (-100% ~ 100%)
                    float speed_percent = ((motion.angle / 180.0f) - 0.5f) * 200.0f;
                    setMotorB2Speed(speed_percent);
                }
                else if (motion.channel < 16)
                {
                    // 舵机通道
                    PCA9685::setServoAngle(motion.channel, motion.angle);
                }
            }

            ESP_LOGI(TAG, "运动序列执行完成");
            return true;
        }

        bool executeMovementFromJson(const std::string& json_str, uint32_t update_interval_ms)
        {
            std::vector<ServoMotion> motions;

            if (!parseMovementJson(json_str, motions))
            {
                ESP_LOGE(TAG, "解析运动JSON失败");
                return false;
            }

            ESP_LOGI(TAG, "开始执行运动序列，共 %zu 个动作", motions.size());
            return executeMovements(motions, update_interval_ms);
        }

    } // namespace move
} // namespace app
