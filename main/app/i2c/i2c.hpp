#pragma once

#include <cstdint>
#include <driver/i2c_master.h>
#include <driver/gpio.h>

#include "config/config.hpp"

namespace app
{
    namespace i2c
    {
        /**
         * @brief I2C 配置结构体
         */
        struct Config
        {
            i2c_port_t port;                   // I2C 端口号 (I2C_NUM_0 或 I2C_NUM_1)
            gpio_num_t sda_pin;                // SDA GPIO 引脚
            gpio_num_t scl_pin;                // SCL GPIO 引脚
            bool       enable_internal_pullup; // 是否启用内部上拉，默认 true

            Config()
                : port(I2C_NUM_1), sda_pin(config::I2C_SDA), scl_pin(config::I2C_SCL),
                  enable_internal_pullup(true)
            {
            }
        };

        /**
         * @brief I2C 主设备类
         *
         * 封装 ESP-IDF I2C master bus API，提供初始化和设备扫描功能
         */
        class I2c
        {
        public:
            I2c();
            ~I2c();

            /**
             * @brief 初始化 I2C 总线
             * @param config I2C 配置参数，如果为 nullptr 则使用默认配置
             * @return true 成功，false 失败
             */
            bool init(const Config* config = nullptr);

            /**
             * @brief 扫描 I2C 总线上的设备
             * @param timeout_ms 扫描超时时间（毫秒），默认 200ms
             * @return 扫描到的设备数量
             */
            int scan(uint32_t timeout_ms = 200);

            /**
             * @brief 检查 I2C 总线是否已初始化
             * @return true 已初始化，false 未初始化
             */
            bool isInitialized() const
            {
                return bus_handle_ != nullptr;
            }

            /**
             * @brief 获取 I2C 总线句柄
             * @return I2C master bus 句柄，如果未初始化则返回 nullptr
             */
            i2c_master_bus_handle_t getBusHandle() const
            {
                return bus_handle_;
            }

            /**
             * @brief 反初始化 I2C 总线
             */
            void deinit();

        private:
            i2c_master_bus_handle_t bus_handle_;  // I2C master bus 句柄
            Config                  config_;      // I2C 配置
            bool                    initialized_; // 是否已初始化
        };
    } // namespace i2c
} // namespace app
