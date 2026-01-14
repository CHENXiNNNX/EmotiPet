#include "qmi8658a.hpp"

#include <cmath>
#include <cstring>
#include <esp_log.h>
#include "system/task/task.hpp"

static const char* const TAG = "QMI8658A";

// 常量
constexpr float M_PI_F  = 3.14159265358979323846f;
constexpr float RAD2DEG = 180.0f / M_PI_F;

namespace app
{
    namespace device
    {
        namespace qmi8658a
        {
            Qmi8658a::Qmi8658a() = default;

            Qmi8658a::~Qmi8658a()
            {
                stopDataCollection();
                deinit();
            }

            bool Qmi8658a::init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr)
            {
                if (bus_handle == nullptr)
                {
                    ESP_LOGE(TAG, "I2C 总线句柄为空");
                    return false;
                }

                bus_handle_ = bus_handle;
                i2c_addr_   = i2c_addr;

                // 添加 I2C 设备
                i2c_device_config_t dev_cfg = {};
                dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
                dev_cfg.device_address      = i2c_addr_;
                dev_cfg.scl_speed_hz        = 400000;

                esp_err_t ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(ret));
                    return false;
                }

                // 检查设备 ID，带重试机制
                uint8_t who_am_i    = 0;
                int     retry_count = 0;

                readRegister(Reg::WHO_AM_I, &who_am_i, 1);
                while (who_am_i != 0x05 && retry_count < 3)
                {
                    app::sys::task::TaskManager::delayMs(100); // 延时 100ms
                    readRegister(Reg::WHO_AM_I, &who_am_i, 1);
                    retry_count++;
                }

                if (who_am_i != 0x05)
                {
                    ESP_LOGE(TAG, "设备 ID 错误: 0x%02X (期望 0x05)", who_am_i);
                    deinit();
                    return false;
                }

                ESP_LOGI(TAG, "QMI8658A OK!");

                // 软复位
                writeRegister(Reg::RESET, 0xB0);
                app::sys::task::TaskManager::delayMs(10);

                // 配置运动检测
                // 第一步：配置运动阈值
                writeRegister(Reg::CATL1_L, 1);    // AnyMotionXThr (0~32)
                writeRegister(Reg::CATL1_H, 1);    // AnyMotionYThr (0~32)
                writeRegister(Reg::CATL2_L, 1);    // AnyMotionZThr (0~32)
                writeRegister(Reg::CATL2_H, 1);    // NoMotionXThr (0~32)
                writeRegister(Reg::CATL3_L, 1);    // NoMotionYThr (0~32)
                writeRegister(Reg::CATL3_H, 1);    // NoMotionZThr (0~32)
                writeRegister(Reg::CATL4_L, 0x77); // MOTION_MODE_CTRL (0111 0111)
                writeRegister(Reg::CATL4_H, 0x01); // 第1条命令
                writeRegister(Reg::CTRL9, 0x0E);   // 配置运动检测命令

                // 第二步：配置时间窗口
                writeRegister(Reg::CATL1_L, 1);      // AnyMotionWindow
                writeRegister(Reg::CATL1_H, 1);      // NoMotionWindow
                writeRegister(Reg::CATL2_L, 0xE8);   // SigMotionWaitWindow[7:0]
                writeRegister(Reg::CATL2_H, 0x03);   // SigMotionWaitWindow[15:8]
                writeRegister(Reg::CATL3_L, 0xE8);   // SigMotionConfirmWindow[7:0]
                writeRegister(Reg::CATL3_H, 0x03);   // SigMotionConfirmWindow[15:8]
                writeRegister(Reg::CATL4_H, 0x02);   // 第2条命令
                writeRegister(Reg::CTRL9, 0x0E);     // 配置运动检测命令

                // 配置传感器
                writeRegister(Reg::CTRL1, 0x40); // 地址自动递增
                writeRegister(Reg::CTRL7, 0x03); // 启用加速度计和陀螺仪
                writeRegister(Reg::CTRL2, 0x95); // ACC: 4G, 250Hz
                writeRegister(Reg::CTRL3, 0xD5); // GYR: 512DPS, 250Hz
                writeRegister(Reg::CTRL8, 0x0E); // 启用 Any-Motion, No-Motion, Significant-Motion

                initialized_ = true;
                ESP_LOGI(TAG, "初始化成功 (地址: 0x%02X, 运动检测已启用)", i2c_addr_);

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
                calibrated_  = false;
            }

            bool Qmi8658a::read(SensorData& data, uint8_t options)
            {
                if (!initialized_)
                {
                    return false;
                }

                // 读取传感器数据
                if (options & READ_SENSOR)
                {
                    // 读取 STATUS0 寄存器
                    uint8_t status      = 0;
                    bool    data_ready  = false;

                    readRegister(Reg::STATUS0, &status, 1);

                    // 检查数据是否就绪（bit[1:0]: 0x01=加速度, 0x02=陀螺仪, 0x03=两者）
                    if (status & 0x03)
                    {
                        data_ready = true;
                    }

                    if (!data_ready)
                    {
                        // 数据未就绪，静默返回
                        return false;
                    }

                    // 读取 12 字节数据（AX_L 到 GZ_H）
                    uint8_t buffer[12];
                    if (!readRegister(Reg::AX_L, buffer, 12))
                    {
                        return false;
                    }

                    // 解析原始数据（小端序）
                    data.acc_x_raw = static_cast<int16_t>((buffer[1] << 8) | buffer[0]);
                    data.acc_y_raw = static_cast<int16_t>((buffer[3] << 8) | buffer[2]);
                    data.acc_z_raw = static_cast<int16_t>((buffer[5] << 8) | buffer[4]);
                    data.gyr_x_raw = static_cast<int16_t>((buffer[7] << 8) | buffer[6]);
                    data.gyr_y_raw = static_cast<int16_t>((buffer[9] << 8) | buffer[8]);
                    data.gyr_z_raw = static_cast<int16_t>((buffer[11] << 8) | buffer[10]);

                    // 转换为物理单位
                    data.accel_x = static_cast<float>(data.acc_x_raw) * ACCEL_SCALE;
                    data.accel_y = static_cast<float>(data.acc_y_raw) * ACCEL_SCALE;
                    data.accel_z = static_cast<float>(data.acc_z_raw) * ACCEL_SCALE;

                    data.gyro_x = static_cast<float>(data.gyr_x_raw) * GYRO_SCALE;
                    data.gyro_y = static_cast<float>(data.gyr_y_raw) * GYRO_SCALE;
                    data.gyro_z = static_cast<float>(data.gyr_z_raw) * GYRO_SCALE;
                }

                // 计算姿态角
                if (options & READ_ATTITUDE)
                {
                    calculateAttitude(data);
                }

                return true;
            }

            void Qmi8658a::calculateAttitude(SensorData& data)
            {
                // 使用加速度计计算倾角
                float acc_x = static_cast<float>(data.acc_x_raw);
                float acc_y = static_cast<float>(data.acc_y_raw);
                float acc_z = static_cast<float>(data.acc_z_raw);

                // AngleX (Roll)
                float temp   = acc_x / sqrtf(acc_y * acc_y + acc_z * acc_z);
                data.angle_x = atanf(temp) * RAD2DEG;

                // AngleY (Pitch)
                temp         = acc_y / sqrtf(acc_x * acc_x + acc_z * acc_z);
                data.angle_y = atanf(temp) * RAD2DEG;

                // AngleZ (Yaw)
                temp         = sqrtf(acc_x * acc_x + acc_y * acc_y) / acc_z;
                data.angle_z = atanf(temp) * RAD2DEG;
            }

            uint8_t Qmi8658a::getMotionStatus()
            {
                if (!initialized_)
                {
                    return 0;
                }

                uint8_t status = 0;
                readRegister(Reg::STATUS1, &status, 1);
                return status;
            }

            void Qmi8658a::close()
            {
                if (!initialized_)
                {
                    return;
                }

                // 关闭芯片运行
                writeRegister(Reg::CTRL1, 0x01);
            }

            bool Qmi8658a::writeRegister(uint8_t reg, uint8_t value)
            {
                if (dev_handle_ == nullptr)
                {
                    return false;
                }

                uint8_t data[2] = {reg, value};
                // 使用 1000ms 超时
                esp_err_t ret = i2c_master_transmit(dev_handle_, data, 2, 1000);

                return ret == ESP_OK;
            }

            bool Qmi8658a::readRegister(uint8_t reg, uint8_t* buffer, size_t length)
            {
                if (dev_handle_ == nullptr || buffer == nullptr)
                {
                    return false;
                }

                // 使用 1000ms 超时
                esp_err_t ret = i2c_master_transmit_receive(dev_handle_, &reg, 1, buffer, length, 1000);

                return ret == ESP_OK;
            }

            bool Qmi8658a::calibrate()
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化，无法标定");
                    return false;
                }

                SensorData data;
                if (!read(data, READ_ALL))
                {
                    ESP_LOGE(TAG, "读取传感器数据失败，无法标定");
                    return false;
                }

                // 保存当前角度作为参考位置
                reference_angle_.roll  = data.angle_x;
                reference_angle_.pitch = data.angle_y;
                reference_angle_.yaw   = data.angle_z;
                calibrated_            = true;

                ESP_LOGI(TAG, "标定完成 - 参考角度: Roll=%.2f° Pitch=%.2f° Yaw=%.2f°",
                         reference_angle_.roll, reference_angle_.pitch, reference_angle_.yaw);

                return true;
            }

            void Qmi8658a::resetCalibration()
            {
                calibrated_ = false;
                reference_angle_.roll  = 0.0f;
                reference_angle_.pitch = 0.0f;
                reference_angle_.yaw   = 0.0f;
                ESP_LOGI(TAG, "标定已清除");
            }

            bool Qmi8658a::getCurrentAngle(AngleData& angle)
            {
                if (!initialized_)
                {
                    return false;
                }

                SensorData data;
                if (!read(data, READ_ALL))
                {
                    return false;
                }

                angle.roll  = data.angle_x;
                angle.pitch = data.angle_y;
                angle.yaw   = data.angle_z;

                return true;
            }

            bool Qmi8658a::getRelativeAngle(AngleData& angle)
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化");
                    return false;
                }

                if (!calibrated_)
                {
                    ESP_LOGW(TAG, "未标定，无法获取相对角度。请先调用 calibrate()");
                    return false;
                }

                SensorData data;
                if (!read(data, READ_ALL))
                {
                    return false;
                }

                // 计算相对于参考位置的角度
                angle.roll  = data.angle_x - reference_angle_.roll;
                angle.pitch = data.angle_y - reference_angle_.pitch;
                angle.yaw   = data.angle_z - reference_angle_.yaw;

                // 将角度归一化到 [-180, 180] 范围
                auto normalizeAngle = [](float& angle) {
                    while (angle > 180.0f)
                        angle -= 360.0f;
                    while (angle < -180.0f)
                        angle += 360.0f;
                };

                normalizeAngle(angle.roll);
                normalizeAngle(angle.pitch);
                normalizeAngle(angle.yaw);

                return true;
            }

            bool Qmi8658a::startDataCollection(uint32_t interval_ms)
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化，无法启动数据采集");
                    return false;
                }

                // 如果已经在运行，先停止
                if (collection_running_)
                {
                    stopDataCollection();
                }

                collection_interval_ms_ = interval_ms;

                // 创建数据采集任务
                app::sys::task::Config task_config;
                task_config.name       = "qmi8658a_collect";
                task_config.stack_size = 4096;
                task_config.priority   = app::sys::task::Priority::NORMAL;
                task_config.core_id    = -1;
                task_config.delay_ms   = 0;

                data_collection_task_ = std::unique_ptr<app::sys::task::Task>(
                    new app::sys::task::Task(
                        [this](void* param) { this->dataCollectionTaskFunction(param); },
                        task_config, this));

                if (!data_collection_task_)
                {
                    ESP_LOGE(TAG, "创建数据采集任务对象失败");
                    return false;
                }

                collection_running_ = true;
                if (!data_collection_task_->start())
                {
                    ESP_LOGE(TAG, "启动数据采集任务失败");
                    collection_running_ = false;
                    data_collection_task_.reset();
                    return false;
                }

                ESP_LOGI(TAG, "数据采集任务已启动，采集间隔: %lu ms", (unsigned long)interval_ms);
                return true;
            }

            bool Qmi8658a::stopDataCollection()
            {
                if (!collection_running_)
                {
                    return true;
                }

                // 停止任务
                collection_running_ = false;

                // 删除任务
                if (data_collection_task_ != nullptr)
                {
                    data_collection_task_->destroy();
                    data_collection_task_.reset();
                }

                ESP_LOGI(TAG, "数据采集任务已停止");
                return true;
            }

            void Qmi8658a::dataCollectionTaskFunction(void* param)
            {
                ESP_LOGI(TAG, "数据采集任务开始运行");

                int last_motion_status = -1; // 上次的运动状态，-1表示未初始化

                while (collection_running_)
                {
                    // 读取传感器数据
                    SensorData data;
                    if (read(data, READ_SENSOR))
                    {
                        // 判断是否有加速度变化
                        bool has_motion = false;
                        int  current_status = -1;

                        if (has_last_accel_)
                        {
                            // 计算加速度变化量（三个轴的向量差）
                            float delta_x = fabsf(data.accel_x - last_accel_x_);
                            float delta_y = fabsf(data.accel_y - last_accel_y_);
                            float delta_z = fabsf(data.accel_z - last_accel_z_);
                            
                            // 计算总变化量（欧几里得距离）
                            float total_change = sqrtf(delta_x * delta_x + delta_y * delta_y + delta_z * delta_z);
                            
                            // 如果变化超过阈值，认为"动了"
                            if (total_change > ACCEL_CHANGE_THRESHOLD)
                            {
                                has_motion = true;
                            }
                        }
                        else
                        {
                            // 第一次读取，初始化上一次的值
                            has_last_accel_ = true;
                        }

                        // 更新上一次的加速度值
                        last_accel_x_ = data.accel_x;
                        last_accel_y_ = data.accel_y;
                        last_accel_z_ = data.accel_z;

                        current_status = has_motion ? 1 : 0;
                        // 更新当前状态
                        current_motion_status_ = current_status;

                        // 只在"动了"时输出日志
                        //if (has_motion)
                        //{
                        //    ESP_LOGI(TAG, "========== QMI8658A 运动数据 ==========");
                        //    ESP_LOGI(TAG, "  运动状态: 动了");
                        //    ESP_LOGI(TAG, "  加速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f m/s²", 
                        //             data.accel_x, data.accel_y, data.accel_z);
                        //    ESP_LOGI(TAG, "==========================================");
                        //}

                        // 触发回调（只在状态变化时，且只在"动了"时输出）
                        if (last_motion_status != current_status || last_motion_status == -1)
                        {
                            last_motion_status = current_status;
                            
                            // 只在"动了"时才显示状态变化信息和调用回调
                            if (current_status == 1)
                            {
                                // ESP_LOGI(TAG, "运动状态变化: %d (动了)", current_status);
                                
                                // 调用回调函数
                                if (motion_status_callback_ != nullptr)
                                {
                                    motion_status_callback_(current_status);
                                }
                            }
                            else
                            {
                                // "没动"时只调用回调，不输出日志
                                if (motion_status_callback_ != nullptr)
                                {
                                    motion_status_callback_(current_status);
                                }
                            }
                        }
                    }

                    // 等待指定间隔
                    app::sys::task::TaskManager::delayMs(collection_interval_ms_);
                }

                ESP_LOGI(TAG, "数据采集任务结束");
            }

        } // namespace qmi8658a
    }     // namespace device
} // namespace app







