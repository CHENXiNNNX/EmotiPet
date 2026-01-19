#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <array>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include "system/task/task.hpp"

namespace app
{
    namespace device
    {
        namespace m0404
        {
            // 压力传感器数据包格式
            constexpr uint8_t PACKET_HEADER_1 = 0xAA;
            constexpr uint8_t PACKET_HEADER_2 = 0x01;
            constexpr uint8_t PRESSURE_COUNT  = 16;  // 16个压力值
            constexpr uint8_t PACKET_SIZE     = 35;  // 总包大小：2(头) + 32(数据) + 1(校验)
            constexpr uint16_t DEAD_ZONE_THRESHOLD = 30 ;  // 死区阈值，小于此值的压力变化将被忽略
            constexpr uint16_t HEAVY_TOUCH_THRESHOLD = 500;  // 重摸阈值，大于此值认为是重摸

            /**
             * @brief 触摸强度类型
             */
            enum class TouchIntensity
            {
                NONE = 0,    // 无触摸
                LIGHT = 1,   // 轻摸（< 500）
                HEAVY = 2    // 重摸（>= 500）
            };

            /**
             * @brief 触摸方向类型
             */
            enum class TouchDirection
            {
                NONE = 0,        // 无方向
                TOP_TO_BOTTOM = 1,  // 从上往下（12->8->4->0）
                BOTTOM_TO_TOP = 2   // 从下往上（0->4->8->12）
            };

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
                 * @brief 触摸状态回调函数类型
                 * @param intensity 触摸强度（轻摸/重摸）
                 * @param direction 触摸方向（从上往下/从下往上）
                 * @param max_pressure 最大压力值
                 */
                using TouchStateCallback = std::function<void(TouchIntensity intensity, TouchDirection direction, uint16_t max_pressure)>;

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
                 * @brief 设置触摸状态回调函数
                 * @param callback 回调函数，当检测到触摸时调用
                 */
                void setTouchStateCallback(TouchStateCallback callback)
                {
                    touch_state_callback_ = callback;
                }

                /**
                 * @brief 静态方法：设置触摸状态回调函数（便捷接口）
                 * @param callback 回调函数
                 */
                static void SetTouchStateCallback(TouchStateCallback callback)
                {
                    getInstance().setTouchStateCallback(callback);
                }

                /**
                 * @brief 启用默认触摸状态日志输出
                 * @note 会自动设置一个回调函数，输出触摸强度和方向的日志
                 */
                void enableDefaultTouchStateLogging()
                {
                    setTouchStateCallback(
                        [](TouchIntensity intensity, TouchDirection direction, uint16_t max_pressure)
                        {
                            const char* intensity_str = "";
                            switch (intensity)
                            {
                                case TouchIntensity::LIGHT:
                                    intensity_str = "轻摸";
                                    break;
                                case TouchIntensity::HEAVY:
                                    intensity_str = "重摸";
                                    break;
                                default:
                                    intensity_str = "无";
                                    break;
                            }

                            const char* direction_str = "";
                            switch (direction)
                            {
                                case TouchDirection::TOP_TO_BOTTOM:
                                    direction_str = "从上往下";
                                    break;
                                case TouchDirection::BOTTOM_TO_TOP:
                                    direction_str = "从下往上";
                                    break;
                                default:
                                    direction_str = "无方向";
                                    break;
                            }

                            ESP_LOGI("M0404", "触摸状态: %s, 方向: %s, 最大压力: %u", 
                                     intensity_str, direction_str, max_pressure);
                        });
                }

                /**
                 * @brief 静态方法：启用默认触摸状态日志输出（便捷接口）
                 */
                static void EnableDefaultTouchStateLogging()
                {
                    getInstance().enableDefaultTouchStateLogging();
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

                /**
                 * @brief 获取最新的压力数据（由后台数据采集任务更新）
                 * @param data 输出数据结构
                 * @return true 数据有效, false 数据无效或未采集
                 */
                bool getLatestPressureData(PressureData& data) const
                {
                    if (!latest_data_.valid)
                    {
                        return false;
                    }
                    data = latest_data_;
                    return true;
                }

                /**
                 * @brief 静态方法：获取最新的压力数据（便捷接口）
                 * @param data 输出数据结构
                 * @return true 数据有效, false 数据无效或未采集
                 */
                static bool GetLatestPressureData(PressureData& data)
                {
                    return getInstance().getLatestPressureData(data);
                }

                /**
                 * @brief 执行零点标定（采集多次数据取平均值作为零点）
                 * @param sample_count 采集次数，默认50次
                 * @param sample_interval_ms 每次采集间隔（毫秒），默认100ms
                 * @return true 成功, false 失败
                 */
                bool calibrateZeroPoint(uint32_t sample_count = 50, uint32_t sample_interval_ms = 100);

                /**
                 * @brief 静态方法：执行零点标定（便捷接口）
                 * @param sample_count 采集次数，默认50次
                 * @param sample_interval_ms 每次采集间隔（毫秒），默认100ms
                 * @return true 成功, false 失败
                 */
                static bool CalibrateZeroPoint(uint32_t sample_count = 50, uint32_t sample_interval_ms = 100)
                {
                    return getInstance().calibrateZeroPoint(sample_count, sample_interval_ms);
                }

                /**
                 * @brief 清除零点标定（恢复为0）
                 * @return true 成功, false 失败
                 */
                bool clearZeroPoint();

                /**
                 * @brief 静态方法：清除零点标定（便捷接口）
                 * @return true 成功, false 失败
                 */
                static bool ClearZeroPoint()
                {
                    return getInstance().clearZeroPoint();
                }

                /**
                 * @brief 获取当前零点标定值
                 * @param zero_points 输出零点值数组
                 * @return true 已标定, false 未标定
                 */
                bool getZeroPoint(std::array<uint16_t, PRESSURE_COUNT>& zero_points) const;

                /**
                 * @brief 静态方法：获取当前零点标定值（便捷接口）
                 * @param zero_points 输出零点值数组
                 * @return true 已标定, false 未标定
                 */
                static bool GetZeroPoint(std::array<uint16_t, PRESSURE_COUNT>& zero_points)
                {
                    return getInstance().getZeroPoint(zero_points);
                }

            private:
                M0404() = default;
                ~M0404();

                // 数据采集任务函数
                void dataCollectionTaskFunction(void* param);

                // 读取原始压力数据（不应用零点补偿）
                bool readRaw(PressureData& data);

                // 解析接收到的数据包
                bool parsePacket(const uint8_t* buffer, size_t length, PressureData& data);

                // 计算校验和
                uint8_t calculateChecksum(const uint8_t* data, size_t length);

                // 应用零点补偿
                void applyZeroPointCompensation(PressureData& data) const;

                // 从NVS加载零点值
                bool loadZeroPointFromNVS();

                // 保存零点值到NVS
                bool saveZeroPointToNVS() const;

                uart_port_t uart_num_ = UART_NUM_MAX;
                bool        initialized_ = false;
                int         baud_rate_   = 115200;

                // 数据采集任务相关
                std::unique_ptr<app::sys::task::Task> data_collection_task_;
                uint32_t                              collection_interval_ms_ = 5000;
                bool                                  collection_running_     = false;

                // 压力状态回调函数
                PressureStatusCallback pressure_status_callback_ = nullptr;
                
                // 触摸状态回调函数
                TouchStateCallback touch_state_callback_ = nullptr;
                
                // 当前压力状态：0=无压力，1=有压力，-1=未初始化
                int current_pressure_status_ = -1;

                // 触摸检测相关
                std::array<uint16_t, 4> last_row_pressures_; // 保存每行的最大压力值 [12行, 8行, 4行, 0行]
                std::array<bool, 4> row_active_history_;     // 每行的激活历史
                std::array<uint32_t, 4> row_activate_time_;  // 每行首次激活的时间戳 [12行, 8行, 4行, 0行]，0表示未激活
                uint32_t last_touch_detect_time_ = 0;       // 上次触摸检测时间
                static constexpr uint32_t TOUCH_DETECT_INTERVAL_MS = 200; // 触摸检测间隔
                
                // 最近两次激活的行记录（用于判断方向）
                int last_activated_row_ = -1;  // 上一次激活的行号，-1表示未激活
                int current_activated_row_ = -1; // 当前激活的行号，-1表示未激活

                // 检测触摸状态和方向
                void detectTouchState(const PressureData& data);

                // 最新的压力数据（由后台数据采集任务更新）
                PressureData latest_data_;

                // 接收缓冲区
                uint8_t rx_buffer_[PACKET_SIZE * 2]; // 足够大的缓冲区

                // 零点标定值（16个传感器的基准值）
                std::array<uint16_t, PRESSURE_COUNT> zero_points_;
                bool zero_point_calibrated_ = false; // 是否已标定
            };

        } // namespace m0404
    } // namespace device
} // namespace app
