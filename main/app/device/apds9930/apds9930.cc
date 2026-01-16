#include "apds9930.hpp"

#include <cmath>
#include <cstring>
#include <esp_log.h>
#include "system/task/task.hpp"

static const char* const TAG = "APDS9930";

namespace app
{
    namespace device
    {
        namespace apds9930
        {
            APDS9930::~APDS9930()
            {
                stopDataCollection();
                deinit();
            }

            bool APDS9930::init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr)
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

                // 读取 ID 寄存器并检查已知的 APDS-9930 值
                uint8_t id = 0;
                if (!wireReadDataByte(Reg::ID, id))
                {
                    ESP_LOGE(TAG, "读取 ID 寄存器失败");
                    deinit();
                    return false;
                }

                if (!(id == APDS9930_ID_1 || id == APDS9930_ID_2))
                {
                    ESP_LOGW(TAG, "ID 检查失败，ID: 0x%02X (期望 0x%02X 或 0x%02X)", id,
                             APDS9930_ID_1, APDS9930_ID_2);
                    // 继续初始化，某些设备可能 ID 不同
                }

                // 将 ENABLE 寄存器设置为 0（禁用所有功能）
                if (!setMode(Mode::ALL, OFF))
                {
                    ESP_LOGE(TAG, "禁用所有寄存器失败");
                    deinit();
                    return false;
                }

                // 设置环境光和接近寄存器的默认值
                if (!wireWriteDataByte(Reg::ATIME, DEFAULT_ATIME))
                {
                    ESP_LOGE(TAG, "设置 ATIME 失败");
                    deinit();
                    return false;
                }
                if (!wireWriteDataByte(Reg::WTIME, DEFAULT_WTIME))
                {
                    ESP_LOGE(TAG, "设置 WTIME 失败");
                    deinit();
                    return false;
                }
                if (!wireWriteDataByte(Reg::PPULSE, DEFAULT_PPULSE))
                {
                    ESP_LOGE(TAG, "设置 PPULSE 失败");
                    deinit();
                    return false;
                }
                if (!wireWriteDataByte(Reg::POFFSET, DEFAULT_POFFSET))
                {
                    ESP_LOGE(TAG, "设置 POFFSET 失败");
                    deinit();
                    return false;
                }
                if (!wireWriteDataByte(Reg::CONFIG, DEFAULT_CONFIG))
                {
                    ESP_LOGE(TAG, "设置 CONFIG 失败");
                    deinit();
                    return false;
                }
                if (!setLEDDrive(DEFAULT_PDRIVE))
                {
                    ESP_LOGE(TAG, "设置 LED 驱动失败");
                    deinit();
                    return false;
                }
                if (!setProximityGain(DEFAULT_PGAIN))
                {
                    ESP_LOGE(TAG, "设置接近增益失败");
                    deinit();
                    return false;
                }
                if (!setAmbientLightGain(DEFAULT_AGAIN))
                {
                    ESP_LOGE(TAG, "设置环境光增益失败");
                    deinit();
                    return false;
                }
                if (!setProximityDiode(DEFAULT_PDIODE))
                {
                    ESP_LOGE(TAG, "设置接近二极管失败");
                    deinit();
                    return false;
                }
                if (!setProximityIntLowThreshold(DEFAULT_PILT))
                {
                    ESP_LOGE(TAG, "设置接近低阈值失败");
                    deinit();
                    return false;
                }
                if (!setProximityIntHighThreshold(DEFAULT_PIHT))
                {
                    ESP_LOGE(TAG, "设置接近高阈值失败");
                    deinit();
                    return false;
                }
                if (!setLightIntLowThreshold(DEFAULT_AILT))
                {
                    ESP_LOGE(TAG, "设置光低阈值失败");
                    deinit();
                    return false;
                }
                if (!setLightIntHighThreshold(DEFAULT_AIHT))
                {
                    ESP_LOGE(TAG, "设置光高阈值失败");
                    deinit();
                    return false;
                }
                if (!wireWriteDataByte(Reg::PERS, DEFAULT_PERS))
                {
                    ESP_LOGE(TAG, "设置 PERS 失败");
                    deinit();
                    return false;
                }

                initialized_ = true;
                ESP_LOGI(TAG, "APDS-9930 初始化成功 (地址: 0x%02X)", i2c_addr_);
                return true;
            }

            void APDS9930::deinit()
            {
                if (dev_handle_ != nullptr)
                {
                    i2c_master_bus_rm_device(dev_handle_);
                    dev_handle_ = nullptr;
                }
                bus_handle_  = nullptr;
                initialized_ = false;
            }

            uint8_t APDS9930::getMode()
            {
                uint8_t enable_value = 0;

                // 读取当前 ENABLE 寄存器
                if (!wireReadDataByte(Reg::ENABLE, enable_value))
                {
                    return ERROR;
                }

                return enable_value;
            }

            bool APDS9930::setMode(uint8_t mode, uint8_t enable)
            {
                uint8_t reg_val;

                // 读取当前 ENABLE 寄存器
                reg_val = getMode();
                if (reg_val == ERROR)
                {
                    return false;
                }

                // 更改 ENABLE 寄存器中的位
                enable = enable & 0x01;
                if (mode <= 6)
                {
                    if (enable)
                    {
                        reg_val |= (1 << mode);
                    }
                    else
                    {
                        reg_val &= ~(1 << mode);
                    }
                }
                else if (mode == Mode::ALL)
                {
                    if (enable)
                    {
                        reg_val = 0x7F;
                    }
                    else
                    {
                        reg_val = 0x00;
                    }
                }

                // 将值写回 ENABLE 寄存器
                if (!wireWriteDataByte(Reg::ENABLE, reg_val))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::enableLightSensor(bool interrupts)
            {
                // 设置默认增益、中断、启用电源并启用传感器
                if (!setAmbientLightGain(DEFAULT_AGAIN))
                {
                    return false;
                }
                if (interrupts)
                {
                    if (!setAmbientLightIntEnable(1))
                    {
                        return false;
                    }
                }
                else
                {
                    if (!setAmbientLightIntEnable(0))
                    {
                        return false;
                    }
                }
                if (!enablePower())
                {
                    return false;
                }
                if (!setMode(Mode::AMBIENT_LIGHT, 1))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::disableLightSensor()
            {
                if (!setAmbientLightIntEnable(0))
                {
                    return false;
                }
                if (!setMode(Mode::AMBIENT_LIGHT, 0))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::enableProximitySensor(bool interrupts)
            {
                // 设置默认增益、LED、中断、启用电源并启用传感器
                if (!setProximityGain(DEFAULT_PGAIN))
                {
                    return false;
                }
                if (!setLEDDrive(DEFAULT_PDRIVE))
                {
                    return false;
                }
                if (interrupts)
                {
                    if (!setProximityIntEnable(1))
                    {
                        return false;
                    }
                }
                else
                {
                    if (!setProximityIntEnable(0))
                    {
                        return false;
                    }
                }
                if (!enablePower())
                {
                    return false;
                }
                if (!setMode(Mode::PROXIMITY, 1))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::disableProximitySensor()
            {
                if (!setProximityIntEnable(0))
                {
                    return false;
                }
                if (!setMode(Mode::PROXIMITY, 0))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::enablePower()
            {
                if (!setMode(Mode::POWER, 1))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::disablePower()
            {
                if (!setMode(Mode::POWER, 0))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::start(bool light_interrupts, bool proximity_interrupts)
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化");
                    return false;
                }

                // 启用环境光传感器
                if (!enableLightSensor(light_interrupts))
                {
                    ESP_LOGE(TAG, "启用环境光传感器失败");
                    return false;
                }

                // 启用接近传感器
                if (!enableProximitySensor(proximity_interrupts))
                {
                    ESP_LOGE(TAG, "启用接近传感器失败");
                    // 如果接近传感器启用失败，尝试禁用已启用的环境光传感器
                    disableLightSensor();
                    return false;
                }

                ESP_LOGI(TAG, "传感器数据获取已开启");
                return true;
            }

            bool APDS9930::stop()
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化");
                    return false;
                }

                bool success = true;

                // 禁用环境光传感器
                if (!disableLightSensor())
                {
                    ESP_LOGW(TAG, "禁用环境光传感器失败");
                    success = false;
                }

                // 禁用接近传感器
                if (!disableProximitySensor())
                {
                    ESP_LOGW(TAG, "禁用接近传感器失败");
                    success = false;
                }

                if (success)
                {
                    ESP_LOGI(TAG, "传感器数据获取已关闭");
                }

                return success;
            }

            bool APDS9930::readAmbientLightLux(float& val)
            {
                uint16_t Ch0 = 0;
                uint16_t Ch1 = 0;

                // 从通道 0 读取值
                if (!readCh0Light(Ch0))
                {
                    return false;
                }

                // 从通道 1 读取值
                if (!readCh1Light(Ch1))
                {
                    return false;
                }

                val = floatAmbientToLux(Ch0, Ch1);
                return true;
            }

            bool APDS9930::readAmbientLightLux(unsigned long& val)
            {
                uint16_t Ch0 = 0;
                uint16_t Ch1 = 0;

                // 从通道 0 读取值
                if (!readCh0Light(Ch0))
                {
                    return false;
                }

                // 从通道 1 读取值
                if (!readCh1Light(Ch1))
                {
                    return false;
                }

                val = ulongAmbientToLux(Ch0, Ch1);
                return true;
            }

            float APDS9930::floatAmbientToLux(uint16_t Ch0, uint16_t Ch1)
            {
                float ALSIT = 2.73f * (256.0f - DEFAULT_ATIME);
                float iac   = fmaxf(Ch0 - B * Ch1, C * Ch0 - D * Ch1);
                float lpc   = GA * DF / (ALSIT * getAmbientLightGain());
                return iac * lpc;
            }

            unsigned long APDS9930::ulongAmbientToLux(uint16_t Ch0, uint16_t Ch1)
            {
                unsigned long ALSIT = (unsigned long)(2.73f * (256.0f - DEFAULT_ATIME));
                unsigned long iac   = (unsigned long)fmaxf(Ch0 - B * Ch1, C * Ch0 - D * Ch1);
                unsigned long lpc   = (unsigned long)(GA * DF / (ALSIT * getAmbientLightGain()));
                return iac * lpc;
            }

            bool APDS9930::readCh0Light(uint16_t& val)
            {
                uint8_t val_byte = 0;
                val              = 0;

                // 从通道 0 读取值
                if (!wireReadDataByte(Reg::Ch0DATAL, val_byte))
                {
                    return false;
                }
                val = val_byte;
                if (!wireReadDataByte(Reg::Ch0DATAH, val_byte))
                {
                    return false;
                }
                val += ((uint16_t)val_byte << 8);
                return true;
            }

            bool APDS9930::readCh1Light(uint16_t& val)
            {
                uint8_t val_byte = 0;
                val              = 0;

                // 从通道 1 读取值
                if (!wireReadDataByte(Reg::Ch1DATAL, val_byte))
                {
                    return false;
                }
                val = val_byte;
                if (!wireReadDataByte(Reg::Ch1DATAH, val_byte))
                {
                    return false;
                }
                val += ((uint16_t)val_byte << 8);
                return true;
            }

            bool APDS9930::readProximity(uint16_t& val)
            {
                val              = 0;
                uint8_t val_byte = 0;

                // 从接近数据寄存器读取值
                if (!wireReadDataByte(Reg::PDATAL, val_byte))
                {
                    return false;
                }
                val = val_byte;
                if (!wireReadDataByte(Reg::PDATAH, val_byte))
                {
                    return false;
                }
                val += ((uint16_t)val_byte << 8);

                return true;
            }

            uint8_t APDS9930::getProximityIntLowThreshold()
            {
                uint16_t val      = 0;
                uint8_t  val_byte = 0;

                // 从 PILT 寄存器读取值
                if (!wireReadDataByte(Reg::PILTL, val_byte))
                {
                    val = 0;
                }
                val = val_byte;
                if (!wireReadDataByte(Reg::PILTH, val_byte))
                {
                    val = 0;
                }
                val += ((uint16_t)val_byte << 8);

                return (uint8_t)val;
            }

            bool APDS9930::setProximityIntLowThreshold(uint16_t threshold)
            {
                uint8_t lo = threshold & 0x00FF;
                uint8_t hi = threshold >> 8;

                if (!wireWriteDataByte(Reg::PILTL, lo))
                {
                    return false;
                }
                if (!wireWriteDataByte(Reg::PILTH, hi))
                {
                    return false;
                }

                return true;
            }

            uint8_t APDS9930::getProximityIntHighThreshold()
            {
                uint16_t val      = 0;
                uint8_t  val_byte = 0;

                // 从 PIHT 寄存器读取值
                if (!wireReadDataByte(Reg::PIHTL, val_byte))
                {
                    val = 0;
                }
                val = val_byte;
                if (!wireReadDataByte(Reg::PIHTH, val_byte))
                {
                    val = 0;
                }
                val += ((uint16_t)val_byte << 8);

                return (uint8_t)val;
            }

            bool APDS9930::setProximityIntHighThreshold(uint16_t threshold)
            {
                uint8_t lo = threshold & 0x00FF;
                uint8_t hi = threshold >> 8;

                if (!wireWriteDataByte(Reg::PIHTL, lo))
                {
                    return false;
                }
                if (!wireWriteDataByte(Reg::PIHTH, hi))
                {
                    return false;
                }

                return true;
            }

            uint8_t APDS9930::getLEDDrive()
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return ERROR;
                }

                // 移位并屏蔽 LED 驱动位
                val = (val >> 6) & 0b00000011;

                return val;
            }

            bool APDS9930::setLEDDrive(uint8_t drive)
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                // 将寄存器中的位设置为给定值
                drive &= 0b00000011;
                drive = drive << 6;
                val &= 0b00111111;
                val |= drive;

                // 将寄存器值写回 CONTROL 寄存器
                if (!wireWriteDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                return true;
            }

            uint8_t APDS9930::getProximityGain()
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return ERROR;
                }

                // 移位并屏蔽 PDRIVE 位
                val = (val >> 2) & 0b00000011;

                return val;
            }

            bool APDS9930::setProximityGain(uint8_t gain)
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                // 将寄存器中的位设置为给定值
                gain &= 0b00000011;
                gain = gain << 2;
                val &= 0b11110011;
                val |= gain;

                // 将寄存器值写回 CONTROL 寄存器
                if (!wireWriteDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                return true;
            }

            uint8_t APDS9930::getProximityDiode()
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return ERROR;
                }

                // 移位并屏蔽 PDRIVE 位
                val = (val >> 4) & 0b00000011;

                return val;
            }

            bool APDS9930::setProximityDiode(uint8_t diode)
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                // 将寄存器中的位设置为给定值
                diode &= 0b00000011;
                diode = diode << 4;
                val &= 0b11001111;
                val |= diode;

                // 将寄存器值写回 CONTROL 寄存器
                if (!wireWriteDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                return true;
            }

            uint8_t APDS9930::getAmbientLightGain()
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return ERROR;
                }

                // 移位并屏蔽 ADRIVE 位
                val &= 0b00000011;

                return val;
            }

            bool APDS9930::setAmbientLightGain(uint8_t gain)
            {
                uint8_t val = 0;

                // 从 CONTROL 寄存器读取值
                if (!wireReadDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                // 将寄存器中的位设置为给定值
                gain &= 0b00000011;
                val &= 0b11111100;
                val |= gain;

                // 将寄存器值写回 CONTROL 寄存器
                if (!wireWriteDataByte(Reg::CONTROL, val))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::getLightIntLowThreshold(uint16_t& threshold)
            {
                uint8_t val_byte = 0;
                threshold        = 0;

                // 从环境光低阈值低字节寄存器读取值
                if (!wireReadDataByte(Reg::AILTL, val_byte))
                {
                    return false;
                }
                threshold = val_byte;

                // 从环境光低阈值高字节寄存器读取值
                if (!wireReadDataByte(Reg::AILTH, val_byte))
                {
                    return false;
                }
                threshold = threshold + ((uint16_t)val_byte << 8);

                return true;
            }

            bool APDS9930::setLightIntLowThreshold(uint16_t threshold)
            {
                uint8_t val_low  = threshold & 0x00FF;
                uint8_t val_high = (threshold & 0xFF00) >> 8;

                // 写入低字节
                if (!wireWriteDataByte(Reg::AILTL, val_low))
                {
                    return false;
                }

                // 写入高字节
                if (!wireWriteDataByte(Reg::AILTH, val_high))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::getLightIntHighThreshold(uint16_t& threshold)
            {
                uint8_t val_byte = 0;
                threshold        = 0;

                // 从环境光高阈值低字节寄存器读取值
                if (!wireReadDataByte(Reg::AIHTL, val_byte))
                {
                    return false;
                }
                threshold = val_byte;

                // 从环境光高阈值高字节寄存器读取值
                if (!wireReadDataByte(Reg::AIHTH, val_byte))
                {
                    return false;
                }
                threshold = threshold + ((uint16_t)val_byte << 8);

                return true;
            }

            bool APDS9930::setLightIntHighThreshold(uint16_t threshold)
            {
                uint8_t val_low  = threshold & 0x00FF;
                uint8_t val_high = (threshold & 0xFF00) >> 8;

                // 写入低字节
                if (!wireWriteDataByte(Reg::AIHTL, val_low))
                {
                    return false;
                }

                // 写入高字节
                if (!wireWriteDataByte(Reg::AIHTH, val_high))
                {
                    return false;
                }

                return true;
            }

            uint8_t APDS9930::getAmbientLightIntEnable()
            {
                uint8_t val = 0;

                // 从 ENABLE 寄存器读取值
                if (!wireReadDataByte(Reg::ENABLE, val))
                {
                    return ERROR;
                }

                // 移位并屏蔽 AIEN 位
                val = (val >> 4) & 0b00000001;

                return val;
            }

            bool APDS9930::setAmbientLightIntEnable(uint8_t enable)
            {
                uint8_t val = 0;

                // 从 ENABLE 寄存器读取值
                if (!wireReadDataByte(Reg::ENABLE, val))
                {
                    return false;
                }

                // 将寄存器中的位设置为给定值
                enable &= 0b00000001;
                enable = enable << 4;
                val &= 0b11101111;
                val |= enable;

                // 将寄存器值写回 ENABLE 寄存器
                if (!wireWriteDataByte(Reg::ENABLE, val))
                {
                    return false;
                }

                return true;
            }

            uint8_t APDS9930::getProximityIntEnable()
            {
                uint8_t val = 0;

                // 从 ENABLE 寄存器读取值
                if (!wireReadDataByte(Reg::ENABLE, val))
                {
                    return ERROR;
                }

                // 移位并屏蔽 PIEN 位
                val = (val >> 5) & 0b00000001;

                return val;
            }

            bool APDS9930::setProximityIntEnable(uint8_t enable)
            {
                uint8_t val = 0;

                // 从 ENABLE 寄存器读取值
                if (!wireReadDataByte(Reg::ENABLE, val))
                {
                    return false;
                }

                // 将寄存器中的位设置为给定值
                enable &= 0b00000001;
                enable = enable << 5;
                val &= 0b11011111;
                val |= enable;

                // 将寄存器值写回 ENABLE 寄存器
                if (!wireWriteDataByte(Reg::ENABLE, val))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::clearAmbientLightInt()
            {
                if (!wireWriteByte(CLEAR_ALS_INT))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::clearProximityInt()
            {
                if (!wireWriteByte(CLEAR_PROX_INT))
                {
                    return false;
                }

                return true;
            }

            bool APDS9930::clearAllInts()
            {
                if (!wireWriteByte(CLEAR_ALL_INTS))
                {
                    return false;
                }

                return true;
            }

            // 原始 I2C 读写函数
            bool APDS9930::wireWriteByte(uint8_t val)
            {
                if (dev_handle_ == nullptr)
                {
                    return false;
                }

                // 使用 1000ms 超时
                esp_err_t ret = i2c_master_transmit(dev_handle_, &val, 1, 1000);
                return ret == ESP_OK;
            }

            bool APDS9930::wireWriteDataByte(uint8_t reg, uint8_t val)
            {
                if (dev_handle_ == nullptr)
                {
                    return false;
                }

                uint8_t data[2];
                data[0] = static_cast<uint8_t>(reg | AUTO_INCREMENT);
                data[1] = val;
                // 使用 1000ms 超时
                esp_err_t ret = i2c_master_transmit(dev_handle_, data, 2, 1000);
                return ret == ESP_OK;
            }

            bool APDS9930::wireWriteDataBlock(uint8_t reg, uint8_t* val, unsigned int len)
            {
                if (dev_handle_ == nullptr || val == nullptr)
                {
                    return false;
                }

                // 分配缓冲区：寄存器地址 + 数据
                uint8_t* buffer = new uint8_t[len + 1];
                if (buffer == nullptr)
                {
                    return false;
                }

                buffer[0] = reg | AUTO_INCREMENT;
                memcpy(&buffer[1], val, len);

                // 使用 1000ms 超时
                esp_err_t ret = i2c_master_transmit(dev_handle_, buffer, len + 1, 1000);

                delete[] buffer;
                return ret == ESP_OK;
            }

            bool APDS9930::wireReadDataByte(uint8_t reg, uint8_t& val)
            {
                if (dev_handle_ == nullptr)
                {
                    return false;
                }

                uint8_t reg_addr = reg | AUTO_INCREMENT;
                // 使用 1000ms 超时
                esp_err_t ret =
                    i2c_master_transmit_receive(dev_handle_, &reg_addr, 1, &val, 1, 1000);
                return ret == ESP_OK;
            }

            int APDS9930::wireReadDataBlock(uint8_t reg, uint8_t* val, unsigned int len)
            {
                if (dev_handle_ == nullptr || val == nullptr)
                {
                    return -1;
                }

                uint8_t reg_addr = reg | AUTO_INCREMENT;
                // 使用 1000ms 超时
                esp_err_t ret =
                    i2c_master_transmit_receive(dev_handle_, &reg_addr, 1, val, len, 1000);
                if (ret != ESP_OK)
                {
                    return -1;
                }

                return len;
            }

            bool APDS9930::startDataCollection(uint32_t interval_ms)
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
                task_config.name       = "apds9930_collect";
                task_config.stack_size = 4096;
                task_config.priority   = app::sys::task::Priority::NORMAL;
                task_config.core_id    = -1;
                task_config.delay_ms   = 0;

                data_collection_task_ =
                    std::unique_ptr<app::sys::task::Task>(new app::sys::task::Task(
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

            bool APDS9930::stopDataCollection()
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

            void APDS9930::dataCollectionTaskFunction(void* param)
            {
                ESP_LOGI(TAG, "数据采集任务开始运行");

                uint32_t count             = 0;
                int      last_light_status = -1; // 上次的光状态，-1表示未初始化

                while (collection_running_)
                {
                    count++;

                    // 读取环境光数据
                    float lux      = 0.0f;
                    bool  light_ok = readAmbientLightLux(lux);

                    // 读取接近数据
                    uint16_t proximity = 0;
                    bool     prox_ok   = readProximity(proximity);

                    // 判断环境光状态
                    int current_status = -1;
                    if (light_ok)
                    {
                        current_status = (lux >= LIGHT_THRESHOLD_LUX) ? 1 : 0;
                        // 更新当前状态
                        current_light_status_ = current_status;
                    }

                    // // 输出数据
                    // ESP_LOGI(TAG,
                    //          "========== APDS-9930 数据 [%lu] ==========", (unsigned long)count);
                    // if (light_ok)
                    // {
                    //     // 显示环境光数据和状态
                    //     const char* status_str = (current_status == 1) ? "亮" : "灭";
                    //     ESP_LOGI(TAG, "  环境光: %.2f lux (%s)", lux, status_str);
                    // }
                    // else
                    // {
                    //     ESP_LOGW(TAG, "  环境光: 读取失败");
                    // }

                    // if (prox_ok)
                    // {
                    //     ESP_LOGI(TAG, "  接近值: %u", proximity);
                    // }
                    // else
                    // {
                    //     ESP_LOGW(TAG, "  接近值: 读取失败");
                    // }
                    // ESP_LOGI(TAG, "==========================================");

                    // 触发回调（只在状态变化时）
                    if (light_ok)
                    {
                        // 只在状态变化时触发回调，或者首次读取时也触发
                        if (last_light_status != current_status || last_light_status == -1)
                        {
                            last_light_status = current_status;

                            // 显示状态变化信息
                            const char* status_str = (current_status == 1) ? "亮" : "灭";
                            ESP_LOGI(TAG, "环境光状态变化: %d (%s), lux=%.2f", current_status,
                                     status_str, lux);

                            // 调用回调函数
                            if (light_status_callback_ != nullptr)
                            {
                                light_status_callback_(current_status);
                            }
                        }
                    }

                    // 等待指定间隔
                    app::sys::task::TaskManager::delayMs(collection_interval_ms_);
                }

                ESP_LOGI(TAG, "数据采集任务结束");
            }

        } // namespace apds9930
    } // namespace device
} // namespace app
