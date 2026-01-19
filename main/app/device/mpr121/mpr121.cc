#include "mpr121.hpp"

#include <cstring>
#include <esp_log.h>
#include "system/task/task.hpp"

static const char* const TAG = "MPR121";

namespace app
{
    namespace device
    {
        namespace mpr121
        {
            // MPR121 寄存器地址
            enum Reg : uint8_t
            {
                TOUCH_STATUS_L = 0x00, // 触摸状态低字节 (MPR121_TS1)
                TOUCH_STATUS_H = 0x01, // 触摸状态高字节 (MPR121_TS2)
                ELE0_T         = 0x41, // 电极0触摸阈值 (MPR121_E0TTH)
                ELE0_R         = 0x42, // 电极0释放阈值 (MPR121_E0RTH)
                ELE1_T         = 0x43, // 电极1触摸阈值 (MPR121_E1TTH)
                ELE1_R         = 0x44, // 电极1释放阈值 (MPR121_E1RTH)
                // ... 其他电极配置寄存器
                AFE2           = 0x5D, // AFE配置寄存器2 (MPR121_AFE2)
                ELE_CFG        = 0x5E, // 电极配置寄存器 (MPR121_ECR)
                SRST           = 0x80, // 软复位寄存器 (MPR121_SRST)
            };

            // 默认配置值（参考原始组件的默认值）
            constexpr uint8_t DEFAULT_TOUCH_THRESHOLD   = 40;  // 触摸阈值（原始组件默认值）
            constexpr uint8_t DEFAULT_RELEASE_THRESHOLD = 20;  // 释放阈值（原始组件默认值）
            constexpr uint8_t DEFAULT_ELECTRODE_CONFIG  = 0x03; // 电极配置（启用3个电极，根据实际需求调整）
            constexpr uint8_t SOFT_RESET_VALUE          = 0x63; // 软复位值
            constexpr uint8_t AFE2_DEFAULT_VALUE        = 0x24; // AFE2默认值（用于验证初始化）

            MPR121::~MPR121()
            {
                stopDataCollection();
                deinit();
            }

            bool MPR121::init(i2c_master_bus_handle_t bus_handle, uint8_t i2c_addr,
                              gpio_num_t irq_pin)
            {
                if (bus_handle == nullptr)
                {
                    ESP_LOGE(TAG, "I2C 总线句柄为空");
                    return false;
                }

                if (initialized_)
                {
                    ESP_LOGW(TAG, "传感器已初始化，先反初始化");
                    deinit();
                }

                bus_handle_ = bus_handle;
                i2c_addr_   = i2c_addr;
                irq_pin_    = irq_pin;

                // 添加 I2C 设备
                i2c_device_config_t dev_cfg = {};
                dev_cfg.dev_addr_length     = I2C_ADDR_BIT_LEN_7;
                dev_cfg.device_address      = i2c_addr_;
                dev_cfg.scl_speed_hz        = 400000; // MPR121 最大支持 400kHz

                esp_err_t ret = i2c_master_bus_add_device(bus_handle_, &dev_cfg, &dev_handle_);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "添加 I2C 设备失败: %s", esp_err_to_name(ret));
                    return false;
                }

                // 软复位：写入 0x63 到软复位寄存器（参考原始组件 MPR121_reset）
                if (!wireWriteDataByte(SRST, SOFT_RESET_VALUE))
                {
                    ESP_LOGE(TAG, "软复位失败");
                    deinit();
                    return false;
                }

                // 等待复位完成
                vTaskDelay(pdMS_TO_TICKS(10));

                // 验证初始化：检查 AFE2 寄存器是否为默认值 0x24
                uint8_t afe2_value = 0;
                if (!wireReadDataByte(AFE2, afe2_value))
                {
                    ESP_LOGE(TAG, "读取 AFE2 寄存器失败");
                    deinit();
                    return false;
                }

                if (afe2_value != AFE2_DEFAULT_VALUE)
                {
                    ESP_LOGW(TAG, "AFE2 寄存器值异常: 0x%02X (期望: 0x%02X)", afe2_value, AFE2_DEFAULT_VALUE);
                    // 不直接返回失败，继续初始化流程
                }

                // 配置电极阈值（根据实际使用的电极数量）
                for (uint8_t i = 0; i < MPR121_ELECTRODE_COUNT; i++)
                {
                    uint8_t touch_reg   = ELE0_T + i * 2;
                    uint8_t release_reg = ELE0_R + i * 2;

                    if (!wireWriteDataByte(touch_reg, DEFAULT_TOUCH_THRESHOLD))
                    {
                        ESP_LOGE(TAG, "设置电极 %d 触摸阈值失败", i);
                        deinit();
                        return false;
                    }

                    if (!wireWriteDataByte(release_reg, DEFAULT_RELEASE_THRESHOLD))
                    {
                        ESP_LOGE(TAG, "设置电极 %d 释放阈值失败", i);
                        deinit();
                        return false;
                    }
                }

                // 配置电极使能寄存器（启用指定数量的电极）
                // 注意：ECR 寄存器的低4位表示启用的电极数量（0-12）
                // 0x03 表示启用3个电极（0, 1, 2）
                if (!wireWriteDataByte(ELE_CFG, DEFAULT_ELECTRODE_CONFIG))
                {
                    ESP_LOGE(TAG, "设置电极配置失败");
                    deinit();
                    return false;
                }

                // 等待配置生效
                vTaskDelay(pdMS_TO_TICKS(10));

                initialized_ = true;
                ESP_LOGI(TAG, "MPR121 初始化成功 (地址: 0x%02X)", i2c_addr_);
                return true;
            }

            void MPR121::deinit()
            {
                if (dev_handle_ != nullptr)
                {
                    i2c_master_bus_rm_device(dev_handle_);
                    dev_handle_ = nullptr;
                }
                bus_handle_  = nullptr;
                initialized_ = false;
            }

            bool MPR121::readTouch(TouchData& data)
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化");
                    return false;
                }

                // 读取触摸状态寄存器（2字节）
                uint8_t touch_status[2] = {0};
                if (!wireReadDataBlock(TOUCH_STATUS_L, touch_status, 2))
                {
                    ESP_LOGE(TAG, "读取触摸状态失败");
                    data.valid = false;
                    return false;
                }

                // 组合低字节和高字节
                data.touched   = touch_status[0] | (touch_status[1] << 8);
                data.valid     = true;
                data.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;

                return true;
            }

            bool MPR121::isElectrodeTouched(uint8_t electrode)
            {
                if (electrode >= MPR121_ELECTRODE_COUNT)
                {
                    ESP_LOGE(TAG, "电极编号无效: %d (有效范围: 0-%d)", electrode,
                             MPR121_ELECTRODE_COUNT - 1);
                    return false;
                }

                uint16_t touched = getTouchedElectrodes();
                return (touched & (1 << electrode)) != 0;
            }

            uint16_t MPR121::getTouchedElectrodes()
            {
                TouchData data;
                if (readTouch(data))
                {
                    return data.touched;
                }
                return 0;
            }

            bool MPR121::startDataCollection(uint32_t interval_ms)
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
                task_config.name       = "mpr121_collect";
                task_config.stack_size = 2 * 1024;
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

            bool MPR121::stopDataCollection()
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

            void MPR121::dataCollectionTaskFunction(void* param)
            {
                ESP_LOGI(TAG, "数据采集任务开始运行");

                uint32_t count             = 0;
                int      last_touch_status = -1; // 上次的触摸状态，-1表示未初始化

                while (collection_running_)
                {
                    count++;

                    // 读取触摸数据
                    TouchData data;
                    bool      read_ok = readTouch(data);

                    // 判断触摸状态
                    int current_status = -1;
                    if (read_ok && data.valid)
                    {
                        uint16_t touched    = data.touched;
                        bool     is_touched = (touched != 0);
                        current_status      = is_touched ? 1 : 0;
                        // 更新当前状态
                        current_touch_status_ = current_status;
                    }

                    // 输出数据（只在有触摸时输出）
                    if (read_ok && data.valid)
                    {
                        // 检查哪些电极被触摸
                        uint16_t touched    = data.touched;
                        bool     is_touched = (touched != 0);

                        // 只在有触摸时才输出日志
                        // 暂时关闭日志输出
                        // if (is_touched)
                        // {
                        //     ESP_LOGI(TAG, "========== MPR121 触摸数据 [%lu] ==========",
                        //     (unsigned long)count); ESP_LOGI(TAG, "  触摸状态: 触摸 (位掩码:
                        //     0x%04X)", touched);
                        //
                        //     // 打印每个电极的状态
                        //     for (uint8_t i = 0; i < MPR121_ELECTRODE_COUNT; i++)
                        //     {
                        //         if (touched & (1 << i))
                        //         {
                        //             ESP_LOGI(TAG, "  电极 %d: 被触摸", i);
                        //         }
                        //     }
                        //     ESP_LOGI(TAG, "==========================================");
                        // }
                    }
                    else if (!read_ok)
                    {
                        // 只在读取失败时输出警告
                        ESP_LOGW(TAG, "========== MPR121 触摸数据 [%lu] ==========",
                                 (unsigned long)count);
                        ESP_LOGW(TAG, "  读取触摸数据失败");
                        ESP_LOGW(TAG, "==========================================");
                    }

                    // 触发回调（只在状态变化时，且只在触摸时输出）
                    if (read_ok && data.valid)
                    {
                        // 只在状态变化时触发回调，或者首次读取时也触发
                        if (last_touch_status != current_status || last_touch_status == -1)
                        {
                            last_touch_status = current_status;

                            // 只在触摸时才显示状态变化信息和调用回调
                            if (current_status == 1)
                            {
                                // ESP_LOGI(TAG, "触摸状态变化: %d (触摸), 位掩码: 0x%04X",
                                //          current_status, data.touched);

                                // 调用回调函数
                                if (touch_status_callback_ != nullptr)
                                {
                                    touch_status_callback_(current_status);
                                }
                            }
                            else
                            {
                                // 未触摸时只调用回调，不输出日志
                                if (touch_status_callback_ != nullptr)
                                {
                                    touch_status_callback_(current_status);
                                }
                            }
                        }
                    }

                    // 等待指定间隔
                    app::sys::task::TaskManager::delayMs(collection_interval_ms_);
                }

                ESP_LOGI(TAG, "数据采集任务结束");
            }

            // 原始 I2C 读写函数
            bool MPR121::wireWriteDataByte(uint8_t reg, uint8_t val)
            {
                if (dev_handle_ == nullptr)
                {
                    return false;
                }

                uint8_t data[2];
                data[0] = reg;
                data[1] = val;

                // 使用 1000ms 超时
                esp_err_t ret = i2c_master_transmit(dev_handle_, data, 2, 1000);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "写入寄存器 0x%02X 失败: %s", reg, esp_err_to_name(ret));
                    return false;
                }

                return true;
            }

            bool MPR121::wireReadDataByte(uint8_t reg, uint8_t& val)
            {
                if (dev_handle_ == nullptr)
                {
                    return false;
                }

                uint8_t reg_addr = reg;
                // 使用 1000ms 超时
                esp_err_t ret =
                    i2c_master_transmit_receive(dev_handle_, &reg_addr, 1, &val, 1, 1000);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "读取寄存器 0x%02X 失败: %s", reg, esp_err_to_name(ret));
                    return false;
                }

                return true;
            }

            bool MPR121::wireReadDataBlock(uint8_t reg, uint8_t* val, uint8_t len)
            {
                if (dev_handle_ == nullptr || val == nullptr)
                {
                    return false;
                }

                uint8_t reg_addr = reg;
                // 使用 1000ms 超时
                esp_err_t ret =
                    i2c_master_transmit_receive(dev_handle_, &reg_addr, 1, val, len, 1000);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "读取寄存器块 0x%02X 失败: %s", reg, esp_err_to_name(ret));
                    return false;
                }

                return true;
            }

        } // namespace mpr121
    } // namespace device
} // namespace app
