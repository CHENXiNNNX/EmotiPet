#pragma once

#include <cstdint>
#include <driver/i2c_master.h>

namespace app
{
    namespace common
    {
        namespace i2c
        {
            namespace qmi8658a
            {
                // I2C 地址
                constexpr uint8_t QMI8658A_ADDR_LOW  = 0x6A;
                constexpr uint8_t QMI8658A_ADDR_HIGH = 0x6B;

                // 加速度计量程
                enum class AccelRange : uint8_t
                {
                    RANGE_2G  = 0x00,
                    RANGE_4G  = 0x01,
                    RANGE_8G  = 0x02,
                    RANGE_16G = 0x03
                };

                // 加速度计输出速率
                enum class AccelOdr : uint8_t
                {
                    ODR_8000HZ  = 0x00,
                    ODR_4000HZ  = 0x01,
                    ODR_2000HZ  = 0x02,
                    ODR_1000HZ  = 0x03,
                    ODR_500HZ   = 0x04,
                    ODR_250HZ   = 0x05,
                    ODR_125HZ   = 0x06,
                    ODR_62_5HZ  = 0x07,
                    ODR_31_25HZ = 0x08
                };

                // 陀螺仪量程
                enum class GyroRange : uint8_t
                {
                    RANGE_32DPS   = 0x00,
                    RANGE_64DPS   = 0x01,
                    RANGE_128DPS  = 0x02,
                    RANGE_256DPS  = 0x03,
                    RANGE_512DPS  = 0x04,
                    RANGE_1024DPS = 0x05,
                    RANGE_2048DPS = 0x06,
                    RANGE_4096DPS = 0x07
                };

                // 陀螺仪输出速率
                enum class GyroOdr : uint8_t
                {
                    ODR_8000HZ  = 0x00,
                    ODR_4000HZ  = 0x01,
                    ODR_2000HZ  = 0x02,
                    ODR_1000HZ  = 0x03,
                    ODR_500HZ   = 0x04,
                    ODR_250HZ   = 0x05,
                    ODR_125HZ   = 0x06,
                    ODR_62_5HZ  = 0x07,
                    ODR_31_25HZ = 0x08
                };

                // 传感器数据
                struct SensorData
                {
                    float    accel_x;     // 加速度 X (m/s² 或 mg)
                    float    accel_y;     // 加速度 Y
                    float    accel_z;     // 加速度 Z
                    float    gyro_x;      // 角速度 X (rad/s 或 dps)
                    float    gyro_y;      // 角速度 Y
                    float    gyro_z;      // 角速度 Z
                    float    temperature; // 温度 (°C)
                    uint32_t timestamp;   // 时间戳
                };

                // 姿态数据 (欧拉角)
                struct Attitude
                {
                    float roll;  // 横滚角 (绕 X 轴, 度)
                    float pitch; // 俯仰角 (绕 Y 轴, 度)
                    float yaw;   // 偏航角 (绕 Z 轴, 度)
                };

                // 配置
                struct Config
                {
                    uint8_t    i2c_addr    = QMI8658A_ADDR_HIGH;
                    AccelRange accel_range = AccelRange::RANGE_8G;
                    AccelOdr   accel_odr   = AccelOdr::ODR_1000HZ;
                    GyroRange  gyro_range  = GyroRange::RANGE_512DPS;
                    GyroOdr    gyro_odr    = GyroOdr::ODR_1000HZ;
                    bool       use_mps2    = true; // 加速度单位: true=m/s², false=mg
                    bool       use_rads    = true; // 角速度单位: true=rad/s, false=dps
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
                     * @param config 配置参数，nullptr 使用默认配置
                     * @return true 成功, false 失败
                     */
                    bool init(i2c_master_bus_handle_t bus_handle, const Config* config = nullptr);

                    /**
                     * @brief 反初始化
                     */
                    void deinit();

                    /**
                     * @brief 读取传感器数据
                     * @param data 输出数据
                     * @return true 成功, false 失败
                     */
                    bool read(SensorData& data);

                    /**
                     * @brief 读取姿态数据
                     * @param attitude 输出姿态
                     * @param dt 时间间隔 (秒), 0 则自动计算
                     * @return true 成功, false 失败
                     */
                    bool readAttitude(Attitude& attitude, float dt = 0);

                    /**
                     * @brief 重置姿态 (清零)
                     */
                    void resetAttitude();

                    /**
                     * @brief 设置互补滤波系数
                     * @param alpha 滤波系数 (0~1, 默认 0.98, 越大越信任陀螺仪)
                     */
                    void setFilterAlpha(float alpha)
                    {
                        filter_alpha_ = alpha;
                    }

                    /**
                     * @brief 检查数据是否就绪
                     * @return true 数据就绪, false 未就绪
                     */
                    bool isDataReady();

                    /**
                     * @brief 重置传感器
                     * @return true 成功, false 失败
                     */
                    bool reset();

                    /**
                     * @brief 设置加速度计量程
                     */
                    bool setAccelRange(AccelRange range);

                    /**
                     * @brief 设置加速度计输出速率
                     */
                    bool setAccelOdr(AccelOdr odr);

                    /**
                     * @brief 设置陀螺仪量程
                     */
                    bool setGyroRange(GyroRange range);

                    /**
                     * @brief 设置陀螺仪输出速率
                     */
                    bool setGyroOdr(GyroOdr odr);

                    /**
                     * @brief 启用/禁用加速度计
                     */
                    bool enableAccel(bool enable);

                    /**
                     * @brief 启用/禁用陀螺仪
                     */
                    bool enableGyro(bool enable);

                    /**
                     * @brief 启用运动唤醒
                     * @param threshold 唤醒阈值
                     */
                    bool enableWakeOnMotion(uint8_t threshold);

                    /**
                     * @brief 禁用运动唤醒
                     */
                    bool disableWakeOnMotion();

                    /**
                     * @brief 获取设备 ID
                     * @return 设备 ID (应为 0x05)
                     */
                    uint8_t getDeviceId();

                    /**
                     * @brief 检查是否已初始化
                     */
                    bool isInitialized() const
                    {
                        return initialized_;
                    }

                private:
                    bool writeRegister(uint8_t reg, uint8_t value);
                    bool readRegister(uint8_t reg, uint8_t* buffer, size_t length);
                    void updateAccelLsbDiv();
                    void updateGyroLsbDiv();

                    i2c_master_bus_handle_t bus_handle_  = nullptr;
                    i2c_master_dev_handle_t dev_handle_  = nullptr;
                    bool                    initialized_ = false;

                    Config   config_;
                    uint16_t accel_lsb_div_ = 4096;
                    uint16_t gyro_lsb_div_  = 64;

                    // 姿态解算
                    Attitude attitude_     = {0, 0, 0};
                    float    filter_alpha_ = 0.98f;
                    uint64_t last_time_us_ = 0;
                };

            } // namespace qmi8658a
        }     // namespace i2c
    }         // namespace common
} // namespace app
