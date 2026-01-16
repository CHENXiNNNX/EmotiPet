#pragma once

#include <cstdint>
#include <memory>
#include <functional>
#include <driver/i2c_master.h>
#include "system/task/task.hpp"

namespace app
{
    namespace device
    {
        namespace apds9930
        {
            // I2C 地址
            constexpr uint8_t APDS9930_I2C_ADDR = 0x39;

            // 命令寄存器模式
            constexpr uint8_t REPEATED_BYTE  = 0x80;
            constexpr uint8_t AUTO_INCREMENT = 0xA0;
            constexpr uint8_t SPECIAL_FN     = 0xE0;

            // 错误代码
            constexpr uint8_t ERROR = 0xFF;

            // 可接受的设备 ID
            constexpr uint8_t APDS9930_ID_1 = 0x12;
            constexpr uint8_t APDS9930_ID_2 = 0x39;

            // 其他参数
            constexpr uint32_t FIFO_PAUSE_TIME = 30; // FIFO 读取之间的等待时间（ms）

            // 寄存器地址
            enum Reg : uint8_t
            {
                ENABLE   = 0x00,
                ATIME    = 0x01,
                WTIME    = 0x03,
                AILTL    = 0x04,
                AILTH    = 0x05,
                AIHTL    = 0x06,
                AIHTH    = 0x07,
                PILTL    = 0x08,
                PILTH    = 0x09,
                PIHTL    = 0x0A,
                PIHTH    = 0x0B,
                PERS     = 0x0C,
                CONFIG   = 0x0D,
                PPULSE   = 0x0E,
                CONTROL  = 0x0F,
                ID       = 0x12,
                STATUS   = 0x13,
                Ch0DATAL = 0x14,
                Ch0DATAH = 0x15,
                Ch1DATAL = 0x16,
                Ch1DATAH = 0x17,
                PDATAL   = 0x18,
                PDATAH   = 0x19,
                POFFSET  = 0x1E
            };

            // 位字段
            constexpr uint8_t PON  = 0b00000001;
            constexpr uint8_t AEN  = 0b00000010;
            constexpr uint8_t PEN  = 0b00000100;
            constexpr uint8_t WEN  = 0b00001000;
            constexpr uint8_t AIEN = 0b00010000;
            constexpr uint8_t PIEN = 0b00100000;
            constexpr uint8_t SAI  = 0b01000000;

            // 开/关定义
            constexpr uint8_t OFF = 0;
            constexpr uint8_t ON  = 1;

            // setMode 的可接受参数
            enum Mode : uint8_t
            {
                POWER             = 0,
                AMBIENT_LIGHT     = 1,
                PROXIMITY         = 2,
                WAIT              = 3,
                AMBIENT_LIGHT_INT = 4,
                PROXIMITY_INT     = 5,
                SLEEP_AFTER_INT   = 6,
                ALL               = 7
            };

            // LED 驱动值
            enum LEDDrive : uint8_t
            {
                LED_DRIVE_100MA  = 0,
                LED_DRIVE_50MA   = 1,
                LED_DRIVE_25MA   = 2,
                LED_DRIVE_12_5MA = 3
            };

            // 接近传感器增益 (PGAIN) 值
            enum ProximityGain : uint8_t
            {
                PGAIN_1X = 0,
                PGAIN_2X = 1,
                PGAIN_4X = 2,
                PGAIN_8X = 3
            };

            // ALS 增益 (AGAIN) 值
            enum AmbientLightGain : uint8_t
            {
                AGAIN_1X   = 0,
                AGAIN_8X   = 1,
                AGAIN_16X  = 2,
                AGAIN_120X = 3
            };

            // 中断清除值
            constexpr uint8_t CLEAR_PROX_INT = 0xE5;
            constexpr uint8_t CLEAR_ALS_INT  = 0xE6;
            constexpr uint8_t CLEAR_ALL_INTS = 0xE7;

            // 默认值
            constexpr uint8_t  DEFAULT_ATIME   = 0xFF;
            constexpr uint8_t  DEFAULT_WTIME   = 0xFF;
            constexpr uint8_t  DEFAULT_PTIME   = 0xFF;
            constexpr uint8_t  DEFAULT_PPULSE  = 0x08;
            constexpr uint8_t  DEFAULT_POFFSET = 0; // 0 偏移
            constexpr uint8_t  DEFAULT_CONFIG  = 0;
            constexpr uint8_t  DEFAULT_PDRIVE  = LED_DRIVE_100MA;
            constexpr uint8_t  DEFAULT_PDIODE  = 2;
            constexpr uint8_t  DEFAULT_PGAIN   = PGAIN_8X;
            constexpr uint8_t  DEFAULT_AGAIN   = AGAIN_16X;
            constexpr uint16_t DEFAULT_PILT    = 0;      // 低接近阈值
            constexpr uint16_t DEFAULT_PIHT    = 50;     // 高接近阈值
            constexpr uint16_t DEFAULT_AILT    = 0xFFFF; // 强制中断用于校准
            constexpr uint16_t DEFAULT_AIHT    = 0;
            constexpr uint8_t  DEFAULT_PERS    = 0x22; // 2 个连续的接近或 ALS 中断

            // ALS 系数
            constexpr float DF = 52.0f;
            constexpr float GA = 0.49f;
            constexpr float B  = 1.862f;
            constexpr float C  = 0.746f;
            constexpr float D  = 1.291f;

            /**
             * @brief APDS-9930 光环境和接近检测传感器驱动类（单例模式）
             */
            class APDS9930
            {
            public:
                /**
                 * @brief 获取单例实例
                 * @return APDS9930 实例的引用
                 */
                static APDS9930& getInstance()
                {
                    static APDS9930 instance;
                    return instance;
                }

                // 禁止拷贝和赋值
                APDS9930(const APDS9930&)            = delete;
                APDS9930& operator=(const APDS9930&) = delete;

                /**
                 * @brief 初始化传感器
                 * @param bus_handle I2C 总线句柄
                 * @param i2c_addr I2C 地址，默认 0x39
                 * @return true 成功, false 失败
                 */
                bool init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr = APDS9930_I2C_ADDR);

                /**
                 * @brief 反初始化
                 */
                void deinit();

                /**
                 * @brief 读取并返回 ENABLE 寄存器的内容
                 * @return ENABLE 寄存器的内容，错误时返回 0xFF
                 */
                uint8_t getMode();

                /**
                 * @brief 启用或禁用 APDS-9930 的功能
                 * @param mode 要启用的功能
                 * @param enable ON (1) 或 OFF (0)
                 * @return true 成功, false 失败
                 */
                bool setMode(uint8_t mode, uint8_t enable);

                /**
                 * @brief 打开 APDS-9930
                 * @return true 成功, false 失败
                 */
                bool enablePower();

                /**
                 * @brief 关闭 APDS-9930
                 * @return true 成功, false 失败
                 */
                bool disablePower();

                /**
                 * @brief 开启传感器数据获取（同时启用环境光和接近传感器）
                 * @param light_interrupts 是否启用环境光中断，默认 false
                 * @param proximity_interrupts 是否启用接近传感器中断，默认 false
                 * @return true 成功, false 失败
                 * @note 此方法会同时启用环境光传感器和接近传感器
                 */
                bool start(bool light_interrupts = false, bool proximity_interrupts = false);

                /**
                 * @brief 关闭传感器数据获取（同时禁用环境光和接近传感器）
                 * @return true 成功, false 失败
                 * @note 此方法会同时禁用环境光传感器和接近传感器
                 */
                bool stop();

                /**
                 * @brief 启动 APDS-9930 上的光（环境光/红外）传感器
                 * @param interrupts true 表示在高低光时启用硬件中断
                 * @return true 传感器启用正确, false 错误
                 */
                bool enableLightSensor(bool interrupts = false);

                /**
                 * @brief 结束 APDS-9930 上的光传感器
                 * @return true 传感器禁用正确, false 错误
                 */
                bool disableLightSensor();

                /**
                 * @brief 启动 APDS-9930 上的接近传感器
                 * @param interrupts true 表示在接近时启用硬件外部中断
                 * @return true 传感器启用正确, false 错误
                 */
                bool enableProximitySensor(bool interrupts = false);

                /**
                 * @brief 结束 APDS-9930 上的接近传感器
                 * @return true 传感器禁用正确, false 错误
                 */
                bool disableProximitySensor();

                /**
                 * @brief 返回接近和 ALS 的 LED 驱动强度
                 * @return LED 驱动强度值，失败时返回 0xFF
                 */
                uint8_t getLEDDrive();

                /**
                 * @brief 设置接近和 ALS 的 LED 驱动强度
                 * @param drive LED 驱动强度值 (0-3)
                 * @return true 成功, false 失败
                 */
                bool setLEDDrive(uint8_t drive);

                /**
                 * @brief 返回环境光传感器 (ALS) 的接收增益
                 * @return ALS 增益值，失败时返回 0xFF
                 */
                uint8_t getAmbientLightGain();

                /**
                 * @brief 设置环境光传感器 (ALS) 的接收增益
                 * @param gain 增益值 (0-3)
                 * @return true 成功, false 失败
                 */
                bool setAmbientLightGain(uint8_t gain);

                /**
                 * @brief 返回接近检测的接收增益
                 * @return 接近增益值，失败时返回 0xFF
                 */
                uint8_t getProximityGain();

                /**
                 * @brief 设置接近检测的接收增益
                 * @param gain 增益值 (0-3)
                 * @return true 成功, false 失败
                 */
                bool setProximityGain(uint8_t gain);

                /**
                 * @brief 选择接近二极管
                 * @param diode 二极管值 (0-3)
                 * @return true 成功, false 失败
                 */
                bool setProximityDiode(uint8_t diode);

                /**
                 * @brief 返回接近二极管
                 * @return 选中的二极管，失败时返回 0xFF
                 */
                uint8_t getProximityDiode();

                /**
                 * @brief 获取和设置光中断阈值
                 */
                bool getLightIntLowThreshold(uint16_t& threshold);
                bool setLightIntLowThreshold(uint16_t threshold);
                bool getLightIntHighThreshold(uint16_t& threshold);
                bool setLightIntHighThreshold(uint16_t threshold);

                /**
                 * @brief 获取和设置中断使能
                 */
                uint8_t getAmbientLightIntEnable();
                bool    setAmbientLightIntEnable(uint8_t enable);
                uint8_t getProximityIntEnable();
                bool    setProximityIntEnable(uint8_t enable);

                /**
                 * @brief 清除中断
                 */
                bool clearAmbientLightInt();
                bool clearProximityInt();
                bool clearAllInts();

                /**
                 * @brief 读取接近级别作为 16 位值
                 * @param val 接近传感器的值
                 * @return true 成功, false 失败
                 */
                bool readProximity(uint16_t& val);

                /**
                 * @brief 读取环境光级别作为浮点 lux 值
                 * @param val 光传感器的 lux 值
                 * @return true 成功, false 失败
                 */
                bool readAmbientLightLux(float& val);

                /**
                 * @brief 读取环境光级别作为无符号长整型 lux 值
                 * @param val 光传感器的 lux 值
                 * @return true 成功, false 失败
                 */
                bool readAmbientLightLux(unsigned long& val);

                /**
                 * @brief 从通道 0 读取光值
                 * @param val 通道 0 的值
                 * @return true 成功, false 失败
                 */
                bool readCh0Light(uint16_t& val);

                /**
                 * @brief 从通道 1 读取光值
                 * @param val 通道 1 的值
                 * @return true 成功, false 失败
                 */
                bool readCh1Light(uint16_t& val);

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 静态方法：初始化传感器（便捷接口）
                 * @param bus_handle I2C 总线句柄
                 * @param i2c_addr I2C 地址，默认 0x39
                 * @return true 成功, false 失败
                 */
                static bool Init(i2c_master_bus_handle_t bus_handle,
                                 uint8_t                 i2c_addr = APDS9930_I2C_ADDR)
                {
                    return getInstance().init(bus_handle, i2c_addr);
                }

                /**
                 * @brief 静态方法：开启传感器数据获取（便捷接口）
                 * @param light_interrupts 是否启用环境光中断，默认 false
                 * @param proximity_interrupts 是否启用接近传感器中断，默认 false
                 * @return true 成功, false 失败
                 */
                static bool Start(bool light_interrupts = false, bool proximity_interrupts = false)
                {
                    return getInstance().start(light_interrupts, proximity_interrupts);
                }

                /**
                 * @brief 静态方法：关闭传感器数据获取（便捷接口）
                 * @return true 成功, false 失败
                 */
                static bool Stop()
                {
                    return getInstance().stop();
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
                 * @brief 环境光状态回调函数类型
                 * @param light_status 0表示暗（<1500 lux），1表示亮（>=1500 lux）
                 */
                using LightStatusCallback = std::function<void(int light_status)>;

                /**
                 * @brief 设置环境光状态回调函数
                 * @param callback 回调函数，当环境光状态变化时调用
                 * @note 回调函数参数：0表示暗（<1500 lux），1表示亮（>=1500 lux）
                 */
                void setLightStatusCallback(LightStatusCallback callback)
                {
                    light_status_callback_ = callback;
                }

                /**
                 * @brief 静态方法：设置环境光状态回调函数（便捷接口）
                 * @param callback 回调函数
                 */
                static void SetLightStatusCallback(LightStatusCallback callback)
                {
                    getInstance().setLightStatusCallback(callback);
                }

                /**
                 * @brief 获取当前环境光状态
                 * @return 0表示暗（<1500 lux），1表示亮（>=1500 lux），-1表示未读取或读取失败
                 */
                int getCurrentLightStatus() const
                {
                    return current_light_status_;
                }

                /**
                 * @brief 静态方法：获取当前环境光状态（便捷接口）
                 * @return 0表示暗（<1500 lux），1表示亮（>=1500 lux），-1表示未读取或读取失败
                 */
                static int GetCurrentLightStatus()
                {
                    return getInstance().getCurrentLightStatus();
                }

            private:
                APDS9930() = default;
                ~APDS9930();
                // 接近中断阈值
                uint8_t getProximityIntLowThreshold();
                bool    setProximityIntLowThreshold(uint16_t threshold);
                uint8_t getProximityIntHighThreshold();
                bool    setProximityIntHighThreshold(uint16_t threshold);

                // 原始 I2C 命令
                bool wireWriteByte(uint8_t val);
                bool wireWriteDataByte(uint8_t reg, uint8_t val);
                bool wireWriteDataBlock(uint8_t reg, uint8_t* val, unsigned int len);
                bool wireReadDataByte(uint8_t reg, uint8_t& val);
                int  wireReadDataBlock(uint8_t reg, uint8_t* val, unsigned int len);

                // 辅助函数
                float         floatAmbientToLux(uint16_t Ch0, uint16_t Ch1);
                unsigned long ulongAmbientToLux(uint16_t Ch0, uint16_t Ch1);

                // 数据采集任务函数
                void dataCollectionTaskFunction(void* param);

                i2c_master_bus_handle_t bus_handle_  = nullptr;
                i2c_master_dev_handle_t dev_handle_  = nullptr;
                bool                    initialized_ = false;
                uint8_t                 i2c_addr_    = APDS9930_I2C_ADDR;

                // 数据采集任务相关
                std::unique_ptr<app::sys::task::Task> data_collection_task_;
                uint32_t                              collection_interval_ms_ = 5000;
                bool                                  collection_running_     = false;

                // 环境光状态回调函数
                LightStatusCallback light_status_callback_ = nullptr;

                // 当前环境光状态：0=暗，1=亮，-1=未初始化
                int current_light_status_ = -1;

                // 环境光阈值（lux）
                static constexpr float LIGHT_THRESHOLD_LUX = 1000.0f;
            };

        } // namespace apds9930
    } // namespace device
} // namespace app
