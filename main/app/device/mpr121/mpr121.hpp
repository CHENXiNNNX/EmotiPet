#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <driver/i2c_master.h>
#include <driver/gpio.h>
#include "system/task/task.hpp"

namespace app
{
    namespace device
    {
        namespace mpr121
        {
            // MPR121 I2C 地址
            constexpr uint8_t MPR121_I2C_ADDR = 0x5A;

            // MPR121 电极数量
            constexpr uint8_t MPR121_ELECTRODE_COUNT = 12;

            /**
             * @brief MPR121 触摸传感器数据结构
             */
            struct TouchData
            {
                uint16_t touched;  // 触摸状态位掩码（12位，每位对应一个电极）
                bool     valid;     // 数据是否有效
                uint32_t timestamp; // 时间戳（可选）

                TouchData() : touched(0), valid(false), timestamp(0)
                {
                }
            };

            /**
             * @brief MPR121 触摸传感器驱动类（单例模式）
             */
            class MPR121
            {
            public:
                /**
                 * @brief 获取单例实例
                 * @return MPR121 实例的引用
                 */
                static MPR121& getInstance()
                {
                    static MPR121 instance;
                    return instance;
                }

                // 禁止拷贝和赋值
                MPR121(const MPR121&)            = delete;
                MPR121& operator=(const MPR121&) = delete;

                /**
                 * @brief 初始化传感器
                 * @param bus_handle I2C 总线句柄
                 * @param i2c_addr I2C 地址，默认 0x5A
                 * @param irq_pin IRQ 引脚，默认 GPIO_NUM_NC（不使用中断）
                 * @return true 成功, false 失败
                 */
                bool init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr = MPR121_I2C_ADDR,
                          gpio_num_t irq_pin = GPIO_NUM_NC);

                /**
                 * @brief 反初始化
                 */
                void deinit();

                /**
                 * @brief 读取触摸状态
                 * @param data 输出数据结构
                 * @return true 成功, false 失败
                 */
                bool readTouch(TouchData& data);

                /**
                 * @brief 检查指定电极是否被触摸
                 * @param electrode 电极编号（0-11）
                 * @return true 被触摸, false 未触摸或错误
                 */
                bool isElectrodeTouched(uint8_t electrode);

                /**
                 * @brief 获取所有电极的触摸状态
                 * @return 触摸状态位掩码（12位，每位对应一个电极）
                 */
                uint16_t getTouchedElectrodes();

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 启动后台数据采集任务
                 * @param interval_ms 采集间隔（毫秒），默认 100ms
                 * @return true 成功, false 失败
                 */
                bool startDataCollection(uint32_t interval_ms = 100);

                /**
                 * @brief 停止后台数据采集任务
                 * @return true 成功, false 失败
                 */
                bool stopDataCollection();

                /**
                 * @brief 静态方法：初始化传感器（便捷接口）
                 * @param bus_handle I2C 总线句柄
                 * @param i2c_addr I2C 地址，默认 0x5A
                 * @param irq_pin IRQ 引脚，默认 GPIO_NUM_NC
                 * @return true 成功, false 失败
                 */
                static bool Init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr = MPR121_I2C_ADDR,
                                 gpio_num_t irq_pin = GPIO_NUM_NC)
                {
                    return getInstance().init(bus_handle, i2c_addr, irq_pin);
                }

                /**
                 * @brief 静态方法：启动后台数据采集任务（便捷接口）
                 * @param interval_ms 采集间隔（毫秒），默认 100ms
                 * @return true 成功, false 失败
                 */
                static bool StartDataCollection(uint32_t interval_ms = 100)
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
                 * @brief 触摸状态回调函数类型
                 * @param touch_status 0表示未触摸，1表示触摸
                 */
                using TouchStatusCallback = std::function<void(int touch_status)>;

                /**
                 * @brief 设置触摸状态回调函数
                 * @param callback 回调函数，当触摸状态变化时调用
                 * @note 回调函数参数：0表示未触摸，1表示触摸
                 */
                void setTouchStatusCallback(TouchStatusCallback callback)
                {
                    touch_status_callback_ = callback;
                }

                /**
                 * @brief 静态方法：设置触摸状态回调函数（便捷接口）
                 * @param callback 回调函数
                 */
                static void SetTouchStatusCallback(TouchStatusCallback callback)
                {
                    getInstance().setTouchStatusCallback(callback);
                }

                /**
                 * @brief 获取当前触摸状态
                 * @return 0表示未触摸，1表示触摸，-1表示未读取或读取失败
                 */
                int getCurrentTouchStatus() const
                {
                    return current_touch_status_;
                }

                /**
                 * @brief 静态方法：获取当前触摸状态（便捷接口）
                 * @return 0表示未触摸，1表示触摸，-1表示未读取或读取失败
                 */
                static int GetCurrentTouchStatus()
                {
                    return getInstance().getCurrentTouchStatus();
                }

            private:
                MPR121() = default;
                ~MPR121();

                // 原始 I2C 读写函数
                bool wireWriteDataByte(uint8_t reg, uint8_t val);
                bool wireReadDataByte(uint8_t reg, uint8_t& val);
                bool wireReadDataBlock(uint8_t reg, uint8_t* val, uint8_t len);

                // 数据采集任务函数
                void dataCollectionTaskFunction(void* param);

                i2c_master_bus_handle_t bus_handle_  = nullptr;
                i2c_master_dev_handle_t dev_handle_  = nullptr;
                bool                    initialized_ = false;
                uint8_t                 i2c_addr_    = MPR121_I2C_ADDR;
                gpio_num_t              irq_pin_     = GPIO_NUM_NC;

                // 数据采集任务相关
                std::unique_ptr<app::sys::task::Task> data_collection_task_;
                uint32_t                              collection_interval_ms_ = 100;
                bool                                  collection_running_     = false;

                // 触摸状态回调函数
                TouchStatusCallback touch_status_callback_ = nullptr;
                
                // 当前触摸状态：0=未触摸，1=触摸，-1=未初始化
                int current_touch_status_ = -1;
            };

        } // namespace mpr121
    }     // namespace device
} // namespace app

