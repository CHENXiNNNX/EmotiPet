#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <array>
#include <driver/uart.h>
#include <driver/gpio.h>
#include "system/task/task.hpp"

namespace app
{
    namespace device
    {
        namespace pressure
        {
            // 压力传感器数据包格式
            constexpr uint8_t PACKET_HEADER_1 = 0xAA;
            constexpr uint8_t PACKET_HEADER_2 = 0x01;
            constexpr uint8_t PRESSURE_COUNT  = 16;  // 16个压力值
            constexpr uint8_t PACKET_SIZE     = 35;  // 总包大小：2(头) + 32(数据) + 1(校验)

            /**
             * @brief M0404 压力传感器数据结构
             */
            struct PressureData
            {
                std::array<uint16_t, PRESSURE_COUNT> pressures; // 16个压力值
                bool                                 valid;     // 数据是否有效
                uint32_t                             timestamp; // 时间戳（可选）

                PressureData() : valid(false), timestamp(0)
                {
                    pressures.fill(0);
                }
            };

            /**
             * @brief M0404 压力传感器驱动类（单例模式）
             */
            class M0404
            {
            public:
                /**
                 * @brief 获取单例实例
                 * @return M0404 实例的引用
                 */
                static M0404& getInstance()
                {
                    static M0404 instance;
                    return instance;
                }

                // 禁止拷贝和赋值
                M0404(const M0404&)            = delete;
                M0404& operator=(const M0404&) = delete;

                /**
                 * @brief 初始化传感器
                 * @param uart_num UART 端口号（UART_NUM_0, UART_NUM_1, UART_NUM_2等）
                 * @param tx_pin TX 引脚
                 * @param rx_pin RX 引脚
                 * @param baud_rate 波特率，默认 115200
                 * @return true 成功, false 失败
                 */
                bool init(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin,
                          int baud_rate = 115200);

                /**
                 * @brief 反初始化
                 */
                void deinit();

                /**
                 * @brief 读取压力数据
                 * @param data 输出数据结构
                 * @return true 成功, false 失败或数据未就绪
                 */
                bool read(PressureData& data);

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 启动后台数据采集任务
                 * @param interval_ms 采集间隔（毫秒），默认 5000ms
                 * @return true 成功, false 失败
                 */
                bool startDataCollection(uint32_t interval_ms = 5000);

                /**
                 * @brief 停止后台数据采集任务
                 * @return true 成功, false 失败
                 */
                bool stopDataCollection();

                /**
                 * @brief 静态方法：初始化传感器（便捷接口）
                 * @param uart_num UART 端口号
                 * @param tx_pin TX 引脚
                 * @param rx_pin RX 引脚
                 * @param baud_rate 波特率，默认 115200
                 * @return true 成功, false 失败
                 */
                static bool Init(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin,
                                 int baud_rate = 115200)
                {
                    return getInstance().init(uart_num, tx_pin, rx_pin, baud_rate);
                }

                /**
                 * @brief 静态方法：启动后台数据采集任务（便捷接口）
                 * @param interval_ms 采集间隔（毫秒），默认 5000ms
                 * @return true 成功, false 失败
                 */
                static bool StartDataCollection(uint32_t interval_ms = 5000)
                {
                    return getInstance().startDataCollection(interval_ms);
                }

                /**
                 * @brief 静态方法：停止后台数据采集任务（便捷接口）
                 * @return true 成功, false 失败
                 */
                static bool StopDataCollection()
                {
                    return getInstance().stopDataCollection();
                }

                /**
                 * @brief 压力状态回调函数类型
                 * @param pressure_status 0表示无压力，1表示有压力
                 */
                using PressureStatusCallback = std::function<void(int pressure_status)>;

                /**
                 * @brief 设置压力状态回调函数
                 * @param callback 回调函数，当压力状态变化时调用
                 * @note 回调函数参数：0表示无压力，1表示有压力
                 */
                void setPressureStatusCallback(PressureStatusCallback callback)
                {
                    pressure_status_callback_ = callback;
                }

                /**
                 * @brief 静态方法：设置压力状态回调函数（便捷接口）
                 * @param callback 回调函数
                 */
                static void SetPressureStatusCallback(PressureStatusCallback callback)
                {
                    getInstance().setPressureStatusCallback(callback);
                }

                /**
                 * @brief 获取当前压力状态
                 * @return 0表示无压力，1表示有压力，-1表示未读取或读取失败
                 */
                int getCurrentPressureStatus() const
                {
                    return current_pressure_status_;
                }

                /**
                 * @brief 静态方法：获取当前压力状态（便捷接口）
                 * @return 0表示无压力，1表示有压力，-1表示未读取或读取失败
                 */
                static int GetCurrentPressureStatus()
                {
                    return getInstance().getCurrentPressureStatus();
                }

            private:
                M0404() = default;
                ~M0404();

                // 数据采集任务函数
                void dataCollectionTaskFunction(void* param);

                // 解析接收到的数据包
                bool parsePacket(const uint8_t* buffer, size_t length, PressureData& data);

                // 计算校验和
                uint8_t calculateChecksum(const uint8_t* data, size_t length);

                uart_port_t uart_num_ = UART_NUM_MAX;
                bool        initialized_ = false;
                int         baud_rate_   = 115200;

                // 数据采集任务相关
                std::unique_ptr<app::sys::task::Task> data_collection_task_;
                uint32_t                              collection_interval_ms_ = 5000;
                bool                                  collection_running_     = false;

                // 压力状态回调函数
                PressureStatusCallback pressure_status_callback_ = nullptr;
                
                // 当前压力状态：0=无压力，1=有压力，-1=未初始化
                int current_pressure_status_ = -1;

                // 接收缓冲区
                uint8_t rx_buffer_[PACKET_SIZE * 2]; // 足够大的缓冲区
            };

        } // namespace pressure
    }     // namespace device
} // namespace app

