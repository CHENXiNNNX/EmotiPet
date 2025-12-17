#include "qmi8658a.hpp"

#include <cmath>
#include <cstring>
#include <esp_log.h>
#include "system/task/task.hpp"
#include "tool/time/time.hpp"

static const char* const TAG = "QMI8658A";

// 常量
constexpr float M_PI_F  = 3.14159265358979323846f;
constexpr float ONE_G   = 9.807f;
constexpr float RAD2DEG = 180.0f / M_PI_F;
constexpr float DEG2RAD = M_PI_F / 180.0f;

// 寄存器地址
namespace Reg
{
    constexpr uint8_t WHO_AM_I    = 0x00;
    constexpr uint8_t REVISION    = 0x01;
    constexpr uint8_t CTRL1       = 0x02;
    constexpr uint8_t CTRL2       = 0x03;
    constexpr uint8_t CTRL3       = 0x04;
    constexpr uint8_t CTRL5       = 0x06;
    constexpr uint8_t CTRL7       = 0x08;
    constexpr uint8_t CTRL8       = 0x09;
    constexpr uint8_t CTRL9       = 0x0A;
    constexpr uint8_t STATUS0     = 0x2E;
    constexpr uint8_t TIMESTAMP_L = 0x30;
    constexpr uint8_t TEMP_L      = 0x33;
    constexpr uint8_t AX_L        = 0x35;
    constexpr uint8_t GX_L        = 0x3B;
} // namespace Reg

namespace app
{
    namespace common
    {
        namespace i2c
        {
            namespace qmi8658a
            {
                Qmi8658a::Qmi8658a() = default;

                Qmi8658a::~Qmi8658a()
                {
                    deinit();
                }

                bool Qmi8658a::init(i2c_master_bus_handle_t bus_handle, const Config* config)
                {
                    if (bus_handle == nullptr)
                    {
                        ESP_LOGE(TAG, "I2C 总线句柄为空");
                        return false;
                    }

                    bus_handle_ = bus_handle;

                    if (config != nullptr)
                    {
                        config_ = *config;
                    }

                    // 添加 I2C 设备
                    i2c_device_config_t dev_cfg = {};
                    dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
                    dev_cfg.device_address      = config_.i2c_addr;
                    dev_cfg.scl_speed_hz        = 400000;

                    esp_err_t ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(ret));
                        return false;
                    }

                    // 检查设备 ID
                    uint8_t who_am_i = getDeviceId();
                    if (who_am_i != 0x05)
                    {
                        ESP_LOGE(TAG, "设备 ID 错误: 0x%02X (期望 0x05)", who_am_i);
                        deinit();
                        return false;
                    }

                    // 软复位
                    if (!reset())
                    {
                        ESP_LOGE(TAG, "复位失败");
                        deinit();
                        return false;
                    }

                    // 配置 CTRL1: 地址自动递增, SPI 4线模式
                    writeRegister(Reg::CTRL1, 0x40);

                    // 配置加速度计
                    setAccelRange(config_.accel_range);
                    setAccelOdr(config_.accel_odr);

                    // 配置陀螺仪
                    setGyroRange(config_.gyro_range);
                    setGyroOdr(config_.gyro_odr);

                    // 启用传感器
                    enableAccel(true);
                    enableGyro(true);

                    initialized_ = true;
                    ESP_LOGI(TAG, "初始化成功 (地址: 0x%02X)", config_.i2c_addr);

                    return true;
                }

                void Qmi8658a::deinit()
                {
                    if (dev_handle_ != nullptr)
                    {
                        i2c_master_bus_rm_device(dev_handle_);
                        dev_handle_ = nullptr;
                    }
                    bus_handle_  = nullptr;
                    initialized_ = false;
                }

                bool Qmi8658a::read(SensorData& data)
                {
                    if (!initialized_)
                    {
                        return false;
                    }

                    uint8_t buffer[14];

                    // 读取加速度和陀螺仪数据 (AX_L 到 GZ_H)
                    if (!readRegister(Reg::AX_L, buffer, 12))
                    {
                        return false;
                    }

                    // 解析原始数据
                    int16_t ax_raw = static_cast<int16_t>((buffer[1] << 8) | buffer[0]);
                    int16_t ay_raw = static_cast<int16_t>((buffer[3] << 8) | buffer[2]);
                    int16_t az_raw = static_cast<int16_t>((buffer[5] << 8) | buffer[4]);
                    int16_t gx_raw = static_cast<int16_t>((buffer[7] << 8) | buffer[6]);
                    int16_t gy_raw = static_cast<int16_t>((buffer[9] << 8) | buffer[8]);
                    int16_t gz_raw = static_cast<int16_t>((buffer[11] << 8) | buffer[10]);

                    // 转换加速度
                    float accel_scale = 1.0f / static_cast<float>(accel_lsb_div_);
                    if (config_.use_mps2)
                    {
                        accel_scale *= ONE_G;
                    }
                    data.accel_x = static_cast<float>(ax_raw) * accel_scale;
                    data.accel_y = static_cast<float>(ay_raw) * accel_scale;
                    data.accel_z = static_cast<float>(az_raw) * accel_scale;

                    // 转换陀螺仪
                    float gyro_scale = 1.0f / static_cast<float>(gyro_lsb_div_);
                    if (config_.use_rads)
                    {
                        gyro_scale *= M_PI_F / 180.0f;
                    }
                    data.gyro_x = static_cast<float>(gx_raw) * gyro_scale;
                    data.gyro_y = static_cast<float>(gy_raw) * gyro_scale;
                    data.gyro_z = static_cast<float>(gz_raw) * gyro_scale;

                    // 读取温度
                    if (readRegister(Reg::TEMP_L, buffer, 2))
                    {
                        int16_t temp_raw = static_cast<int16_t>((buffer[1] << 8) | buffer[0]);
                        data.temperature = static_cast<float>(temp_raw) / 256.0f;
                    }

                    // 读取时间戳
                    if (readRegister(Reg::TIMESTAMP_L, buffer, 3))
                    {
                        data.timestamp = buffer[0] | (buffer[1] << 8) | (buffer[2] << 16);
                    }

                    return true;
                }

                bool Qmi8658a::readAttitude(Attitude& attitude, float dt)
                {
                    SensorData data;
                    if (!read(data))
                    {
                        return false;
                    }

                    // 计算时间间隔
                    int64_t now_us = app::tool::time::uptimeUs();
                    if (dt <= 0)
                    {
                        if (last_time_us_ == 0)
                        {
                            last_time_us_ = static_cast<uint64_t>(now_us);
                            attitude      = attitude_;
                            return true;
                        }
                        dt = static_cast<float>(now_us - static_cast<int64_t>(last_time_us_)) /
                             1000000.0f;
                    }
                    last_time_us_ = static_cast<uint64_t>(now_us);

                    // 限制 dt 范围
                    if (dt > 0.5f)
                        dt = 0.5f;
                    if (dt < 0.001f)
                        dt = 0.001f;

                    // 从加速度计算姿态角 (静态)
                    float accel_roll  = atan2f(data.accel_y, data.accel_z) * RAD2DEG;
                    float accel_pitch = atan2f(-data.accel_x, sqrtf(data.accel_y * data.accel_y +
                                                                    data.accel_z * data.accel_z)) *
                                        RAD2DEG;

                    // 陀螺仪角速度 (转换为度/秒)
                    float gyro_roll_rate  = data.gyro_x * RAD2DEG;
                    float gyro_pitch_rate = data.gyro_y * RAD2DEG;
                    float gyro_yaw_rate   = data.gyro_z * RAD2DEG;

                    // 互补滤波
                    attitude_.roll = filter_alpha_ * (attitude_.roll + gyro_roll_rate * dt) +
                                     (1.0f - filter_alpha_) * accel_roll;
                    attitude_.pitch = filter_alpha_ * (attitude_.pitch + gyro_pitch_rate * dt) +
                                      (1.0f - filter_alpha_) * accel_pitch;
                    // Yaw 只能用陀螺仪积分 (会漂移)
                    attitude_.yaw += gyro_yaw_rate * dt;

                    // 限制 Yaw 范围 [-180, 180]
                    while (attitude_.yaw > 180.0f)
                        attitude_.yaw -= 360.0f;
                    while (attitude_.yaw < -180.0f)
                        attitude_.yaw += 360.0f;

                    attitude = attitude_;
                    return true;
                }

                void Qmi8658a::resetAttitude()
                {
                    attitude_     = {0, 0, 0};
                    last_time_us_ = 0;
                }

                bool Qmi8658a::isDataReady()
                {
                    uint8_t status = 0;
                    if (!readRegister(Reg::STATUS0, &status, 1))
                    {
                        return false;
                    }
                    return (status & 0x03) != 0;
                }

                bool Qmi8658a::reset()
                {
                    // 软复位
                    if (!writeRegister(Reg::CTRL9, 0xB0))
                    {
                        return false;
                    }

                    // 等待复位完成
                    app::sys::task::TaskManager::delayMs(10);

                    // 禁用所有传感器
                    writeRegister(Reg::CTRL7, 0x00);

                    return true;
                }

                bool Qmi8658a::setAccelRange(AccelRange range)
                {
                    uint8_t ctrl2 = 0;
                    readRegister(Reg::CTRL2, &ctrl2, 1);
                    ctrl2 = (ctrl2 & 0x8F) | (static_cast<uint8_t>(range) << 4);

                    if (!writeRegister(Reg::CTRL2, ctrl2))
                    {
                        return false;
                    }

                    config_.accel_range = range;
                    updateAccelLsbDiv();
                    return true;
                }

                bool Qmi8658a::setAccelOdr(AccelOdr odr)
                {
                    uint8_t ctrl2 = 0;
                    readRegister(Reg::CTRL2, &ctrl2, 1);
                    ctrl2 = (ctrl2 & 0xF0) | static_cast<uint8_t>(odr);

                    if (!writeRegister(Reg::CTRL2, ctrl2))
                    {
                        return false;
                    }

                    config_.accel_odr = odr;
                    return true;
                }

                bool Qmi8658a::setGyroRange(GyroRange range)
                {
                    uint8_t ctrl3 = 0;
                    readRegister(Reg::CTRL3, &ctrl3, 1);
                    ctrl3 = (ctrl3 & 0x8F) | (static_cast<uint8_t>(range) << 4);

                    if (!writeRegister(Reg::CTRL3, ctrl3))
                    {
                        return false;
                    }

                    config_.gyro_range = range;
                    updateGyroLsbDiv();
                    return true;
                }

                bool Qmi8658a::setGyroOdr(GyroOdr odr)
                {
                    uint8_t ctrl3 = 0;
                    readRegister(Reg::CTRL3, &ctrl3, 1);
                    ctrl3 = (ctrl3 & 0xF0) | static_cast<uint8_t>(odr);

                    if (!writeRegister(Reg::CTRL3, ctrl3))
                    {
                        return false;
                    }

                    config_.gyro_odr = odr;
                    return true;
                }

                bool Qmi8658a::enableAccel(bool enable)
                {
                    uint8_t ctrl7 = 0;
                    readRegister(Reg::CTRL7, &ctrl7, 1);

                    if (enable)
                    {
                        ctrl7 |= 0x01;
                    }
                    else
                    {
                        ctrl7 &= ~0x01;
                    }

                    return writeRegister(Reg::CTRL7, ctrl7);
                }

                bool Qmi8658a::enableGyro(bool enable)
                {
                    uint8_t ctrl7 = 0;
                    readRegister(Reg::CTRL7, &ctrl7, 1);

                    if (enable)
                    {
                        ctrl7 |= 0x02;
                    }
                    else
                    {
                        ctrl7 &= ~0x02;
                    }

                    return writeRegister(Reg::CTRL7, ctrl7);
                }

                bool Qmi8658a::enableWakeOnMotion(uint8_t threshold)
                {
                    // 配置 WoM 阈值
                    if (!writeRegister(Reg::CTRL8, threshold))
                    {
                        return false;
                    }

                    // 启用 WoM
                    uint8_t ctrl7 = 0;
                    readRegister(Reg::CTRL7, &ctrl7, 1);
                    ctrl7 |= 0x04;

                    return writeRegister(Reg::CTRL7, ctrl7);
                }

                bool Qmi8658a::disableWakeOnMotion()
                {
                    uint8_t ctrl7 = 0;
                    readRegister(Reg::CTRL7, &ctrl7, 1);
                    ctrl7 &= ~0x04;

                    return writeRegister(Reg::CTRL7, ctrl7);
                }

                uint8_t Qmi8658a::getDeviceId()
                {
                    uint8_t id = 0;
                    readRegister(Reg::WHO_AM_I, &id, 1);
                    return id;
                }

                bool Qmi8658a::writeRegister(uint8_t reg, uint8_t value)
                {
                    if (dev_handle_ == nullptr)
                    {
                        return false;
                    }

                    uint8_t   data[2] = {reg, value};
                    esp_err_t ret     = i2c_master_transmit(dev_handle_, data, 2, 100);

                    return ret == ESP_OK;
                }

                bool Qmi8658a::readRegister(uint8_t reg, uint8_t* buffer, size_t length)
                {
                    if (dev_handle_ == nullptr || buffer == nullptr)
                    {
                        return false;
                    }

                    esp_err_t ret =
                        i2c_master_transmit_receive(dev_handle_, &reg, 1, buffer, length, 100);

                    return ret == ESP_OK;
                }

                void Qmi8658a::updateAccelLsbDiv()
                {
                    switch (config_.accel_range)
                    {
                    case AccelRange::RANGE_2G:
                        accel_lsb_div_ = 16384;
                        break;
                    case AccelRange::RANGE_4G:
                        accel_lsb_div_ = 8192;
                        break;
                    case AccelRange::RANGE_8G:
                        accel_lsb_div_ = 4096;
                        break;
                    case AccelRange::RANGE_16G:
                        accel_lsb_div_ = 2048;
                        break;
                    }
                }

                void Qmi8658a::updateGyroLsbDiv()
                {
                    switch (config_.gyro_range)
                    {
                    case GyroRange::RANGE_32DPS:
                        gyro_lsb_div_ = 1024;
                        break;
                    case GyroRange::RANGE_64DPS:
                        gyro_lsb_div_ = 512;
                        break;
                    case GyroRange::RANGE_128DPS:
                        gyro_lsb_div_ = 256;
                        break;
                    case GyroRange::RANGE_256DPS:
                        gyro_lsb_div_ = 128;
                        break;
                    case GyroRange::RANGE_512DPS:
                        gyro_lsb_div_ = 64;
                        break;
                    case GyroRange::RANGE_1024DPS:
                        gyro_lsb_div_ = 32;
                        break;
                    case GyroRange::RANGE_2048DPS:
                        gyro_lsb_div_ = 16;
                        break;
                    case GyroRange::RANGE_4096DPS:
                        gyro_lsb_div_ = 8;
                        break;
                    }
                }

            } // namespace qmi8658a
        }     // namespace i2c
    }         // namespace common
} // namespace app
