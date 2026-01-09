#pragma once

#include <cstdint>
#include <driver/i2c_master.h>

namespace app
{
    namespace device
    {
        namespace qmi8658a
        {
            // I2C 地址
            constexpr uint8_t QMI8658A_ADDR_LOW  = 0x6A;
            constexpr uint8_t QMI8658A_ADDR_HIGH = 0x6B;

            // 读取选项位掩码
            enum ReadOption : uint8_t
            {
                READ_SENSOR   = 0x01,                       // 读取加速度和陀螺仪
                READ_ATTITUDE = 0x02,                       // 计算姿态角
                READ_ALL      = READ_SENSOR | READ_ATTITUDE // 读取全部
            };

            // 运动状态位掩码（STATUS1 寄存器）
            enum MotionStatus : uint8_t
            {
                ANY_MOTION         = 0x20, // bit 5: 检测到任何运动/震动
                NO_MOTION          = 0x40, // bit 6: 检测到静止状态
                SIGNIFICANT_MOTION = 0x80  // bit 7: 检测到显著运动
            };

            // 传感器数据结构
            struct SensorData
            {
                // 原始数据（16位整数）
                int16_t acc_x_raw;
                int16_t acc_y_raw;
                int16_t acc_z_raw;
                int16_t gyr_x_raw;
                int16_t gyr_y_raw;
                int16_t gyr_z_raw;

                // 转换后的数据
                float accel_x; // 加速度 X (m/s²)
                float accel_y; // 加速度 Y
                float accel_z; // 加速度 Z
                float gyro_x;  // 角速度 X (rad/s)
                float gyro_y;  // 角速度 Y
                float gyro_z;  // 角速度 Z

                // 姿态角（度）
                float angle_x; // Roll 横滚角
                float angle_y; // Pitch 俯仰角
                float angle_z; // Yaw 偏航角
            };

            /**
             * @brief QMI8658A 六轴 IMU 驱动类
             */
            class Qmi8658a
            {
            public:
                Qmi8658a();
                ~Qmi8658a();

                /**
                 * @brief 初始化传感器
                 * @param bus_handle I2C 总线句柄
                 * @param i2c_addr I2C 地址，默认 0x6A
                 * @return true 成功, false 失败
                 */
                bool init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr = QMI8658A_ADDR_LOW);

                /**
                 * @brief 反初始化
                 */
                void deinit();

                /**
                 * @brief 读取传感器数据
                 * @param data 输出数据结构
                 * @param options 读取选项（位掩码）
                 *                - READ_SENSOR: 读取加速度和陀螺仪（默认）
                 *                - READ_ATTITUDE: 计算姿态角
                 *                - READ_ALL: 读取全部数据
                 * @return true 成功, false 失败或数据未就绪
                 * @example
                 *     SensorData data;
                 *     // 只读传感器数据
                 *     read(data);
                 *     // 读取传感器数据并计算姿态
                 *     read(data, READ_ALL);
                 */
                bool read(SensorData& data, uint8_t options = READ_SENSOR);

                /**
                 * @brief 获取运动状态
                 * @return 运动状态寄存器值（STATUS1）
                 * @note 位定义：
                 *       - bit 5 (0x20): ANY_MOTION - 检测到任何运动/震动
                 *       - bit 6 (0x40): NO_MOTION - 检测到静止状态
                 *       - bit 7 (0x80): SIGNIFICANT_MOTION - 检测到显著运动
                 * @example
                 *       uint8_t status = getMotionStatus();
                 *       if (status & MotionStatus::ANY_MOTION) {
                 *           // 检测到运动
                 *       }
                 */
                uint8_t getMotionStatus();

                /**
                 * @brief 关闭传感器
                 */
                void close();

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

            private:
                // 寄存器地址
                enum Reg : uint8_t
                {
                    WHO_AM_I    = 0x00,
                    REVISION_ID = 0x01,
                    CTRL1       = 0x02,
                    CTRL2       = 0x03,
                    CTRL3       = 0x04,
                    CTRL7       = 0x08,
                    CTRL8       = 0x09,
                    CTRL9       = 0x0A,
                    CATL1_L     = 0x0B, // 运动检测配置
                    CATL1_H     = 0x0C,
                    CATL2_L     = 0x0D,
                    CATL2_H     = 0x0E,
                    CATL3_L     = 0x0F,
                    CATL3_H     = 0x10,
                    CATL4_L     = 0x11,
                    CATL4_H     = 0x12,
                    STATUS0     = 0x2E,
                    STATUS1     = 0x2F,
                    AX_L        = 0x35,
                    RESET       = 0x60
                };

                bool writeRegister(uint8_t reg, uint8_t value);
                bool readRegister(uint8_t reg, uint8_t* buffer, size_t length);
                void calculateAttitude(SensorData& data);

                i2c_master_bus_handle_t bus_handle_  = nullptr;
                i2c_master_dev_handle_t dev_handle_  = nullptr;
                bool                    initialized_ = false;
                uint8_t                 i2c_addr_    = QMI8658A_ADDR_LOW;

                // 量程缩放因子
                // 4G 加速度计: LSB/g = 8192
                // 512DPS 陀螺仪: LSB/(dps) = 64
                static constexpr float ACCEL_SCALE = 9.807f / 8192.0f;   // m/s² per LSB
                static constexpr float GYRO_SCALE  = 0.0174533f / 64.0f; // rad/s per LSB (π/180/64)
            };

        } // namespace qmi8658a
    } // namespace device
} // namespace app
