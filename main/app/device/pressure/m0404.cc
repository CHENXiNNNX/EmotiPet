#include "m0404.hpp"

#include <cstring>
#include <esp_log.h>
#include "system/task/task.hpp"
#include "driver/uart.h"

static const char* const TAG = "M0404";

namespace app
{
    namespace device
    {
        namespace pressure
        {
            M0404::~M0404()
            {
                stopDataCollection();
                deinit();
            }

            bool M0404::init(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate)
            {
                if (initialized_)
                {
                    ESP_LOGW(TAG, "传感器已初始化，先反初始化");
                    deinit();
                }

                uart_num_  = uart_num;
                baud_rate_ = baud_rate;

                // 配置 UART 参数
                uart_config_t uart_config = {};
                uart_config.baud_rate      = baud_rate;
                uart_config.data_bits       = UART_DATA_8_BITS;
                uart_config.parity          = UART_PARITY_DISABLE;
                uart_config.stop_bits       = UART_STOP_BITS_1;
                uart_config.flow_ctrl       = UART_HW_FLOWCTRL_DISABLE;
                uart_config.source_clk      = UART_SCLK_DEFAULT;

                // 配置 UART
                esp_err_t ret = uart_driver_install(uart_num_, 1024, 1024, 0, nullptr, 0);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "安装 UART 驱动失败: %s", esp_err_to_name(ret));
                    return false;
                }

                ret = uart_param_config(uart_num_, &uart_config);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "配置 UART 参数失败: %s", esp_err_to_name(ret));
                    uart_driver_delete(uart_num_);
                    return false;
                }

                ret = uart_set_pin(uart_num_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "设置 UART 引脚失败: %s", esp_err_to_name(ret));
                    uart_driver_delete(uart_num_);
                    return false;
                }

                initialized_ = true;
                ESP_LOGI(TAG, "M0404 压力传感器初始化成功 (UART%d, 波特率: %d)", uart_num_, baud_rate_);
                return true;
            }

            void M0404::deinit()
            {
                if (uart_num_ != UART_NUM_MAX)
                {
                    uart_driver_delete(uart_num_);
                    uart_num_ = UART_NUM_MAX;
                }
                initialized_ = false;
            }

            uint8_t M0404::calculateChecksum(const uint8_t* data, size_t length)
            {
                uint8_t sum = 0;
                for (size_t i = 0; i < length; i++)
                {
                    sum += data[i];
                }
                return sum;
            }

            bool M0404::parsePacket(const uint8_t* buffer, size_t length, PressureData& data)
            {
                if (length < PACKET_SIZE)
                {
                    ESP_LOGW(TAG, "[错误] 数据包长度不足: %lu (需要 %d)", (unsigned long)length, PACKET_SIZE);
                    return false;
                }

                // 检查包头
                if (buffer[0] != PACKET_HEADER_1 || buffer[1] != PACKET_HEADER_2)
                {
                    ESP_LOGW(TAG, "[错误] 包头错误: 0x%02X 0x%02X (期望 0x%02X 0x%02X)", buffer[0], buffer[1],
                             PACKET_HEADER_1, PACKET_HEADER_2);
                    return false;
                }

                ESP_LOGD(TAG, "[调试] 包头验证通过: 0x%02X 0x%02X", buffer[0], buffer[1]);

                // 计算校验和（前34字节）
                uint8_t calculated_checksum = calculateChecksum(buffer, PACKET_SIZE - 1);
                uint8_t received_checksum    = buffer[PACKET_SIZE - 1];

                ESP_LOGD(TAG, "[调试] 校验和: 计算值=0x%02X, 接收值=0x%02X", calculated_checksum,
                         received_checksum);

                if (calculated_checksum != received_checksum)
                {
                    ESP_LOGW(TAG, "[错误] 校验和错误: 计算值=0x%02X, 接收值=0x%02X", calculated_checksum,
                             received_checksum);
                    // 打印数据包内容用于调试
                    ESP_LOGW(TAG, "[错误] 数据包内容 (HEX):");
                    char hex_str[256] = {0};
                    for (int i = 0; i < PACKET_SIZE && i < 35; i++)
                    {
                        char temp[4];
                        snprintf(temp, sizeof(temp), "%02X ", buffer[i]);
                        strcat(hex_str, temp);
                        if ((i + 1) % 16 == 0)
                        {
                            ESP_LOGW(TAG, "[错误] %s", hex_str);
                            hex_str[0] = '\0';
                        }
                    }
                    if (strlen(hex_str) > 0)
                    {
                        ESP_LOGW(TAG, "[错误] %s", hex_str);
                    }
                    return false;
                }

                ESP_LOGD(TAG, "[调试] 校验和验证通过");

                // 解析16个压力值（每个2字节，大端序）
                for (size_t i = 0; i < PRESSURE_COUNT; i++)
                {
                    size_t offset = 2 + i * 2; // 跳过2字节包头
                    uint8_t high_byte = buffer[offset];
                    uint8_t low_byte = buffer[offset + 1];
                    data.pressures[i] = (high_byte << 8) | low_byte;
                    ESP_LOGD(TAG, "[调试] 压力[%lu]: 原始字节 [0x%02X 0x%02X] = %u", 
                             (unsigned long)i, high_byte, low_byte, data.pressures[i]);
                }

                data.valid     = true;
                data.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGD(TAG, "[调试] 数据包解析完成，时间戳: %lu ms", (unsigned long)data.timestamp);
                return true;
            }

            bool M0404::read(PressureData& data)
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化");
                    return false;
                }

                // 检查缓冲区中是否有数据
                size_t available = 0;
                uart_get_buffered_data_len(uart_num_, &available);
                ESP_LOGD(TAG, "[调试] 缓冲区可用数据: %lu 字节", (unsigned long)available);

                // 如果缓冲区数据不足一个完整包，等待数据到达
                int read_len = 0;
                if (available < PACKET_SIZE)
                {
                    ESP_LOGD(TAG, "[调试] 缓冲区数据不足 (%lu < %d)，等待数据到达（最多500ms）", (unsigned long)available,
                             PACKET_SIZE);
                    // 等待数据到达（最多等待500ms，因为传感器可能每100ms发送一次）
                    read_len = uart_read_bytes(uart_num_, rx_buffer_, sizeof(rx_buffer_),
                                              500 / portTICK_PERIOD_MS);
                    ESP_LOGD(TAG, "[调试] 等待后读取到 %d 字节数据", read_len);
                }
                else
                {
                    // 如果缓冲区有足够数据，立即读取
                    read_len = uart_read_bytes(uart_num_, rx_buffer_, sizeof(rx_buffer_), 0);
                    ESP_LOGD(TAG, "[调试] 立即读取到 %d 字节数据", read_len);
                }

                if (read_len <= 0)
                {
                    ESP_LOGD(TAG, "[调试] 未读取到任何数据，缓冲区: %lu 字节", (unsigned long)available);
                    return false;
                }

                if (read_len < PACKET_SIZE)
                {
                    ESP_LOGD(TAG, "[调试] 读取数据不足: %d 字节 (需要 %d 字节)", read_len, PACKET_SIZE);
                    // 打印前几个字节用于调试
                    if (read_len > 0)
                    {
                        ESP_LOGD(TAG, "[调试] 接收到的前 %d 字节 (HEX):", read_len > 16 ? 16 : read_len);
                        char hex_str[128] = {0};
                        for (int i = 0; i < (read_len > 16 ? 16 : read_len); i++)
                        {
                            char temp[4];
                            snprintf(temp, sizeof(temp), "%02X ", rx_buffer_[i]);
                            strcat(hex_str, temp);
                        }
                        ESP_LOGD(TAG, "[调试] %s", hex_str);
                    }
                    return false;
                }

                // 打印接收到的原始数据（前35字节）- 仅在DEBUG级别显示
                ESP_LOGD(TAG, "[调试] 接收到的原始数据 (HEX, 前 %d 字节):",
                         read_len > PACKET_SIZE ? PACKET_SIZE : read_len);
                char hex_str[256] = {0};
                int  print_len   = (read_len > PACKET_SIZE) ? PACKET_SIZE : read_len;
                for (int i = 0; i < print_len; i++)
                {
                    char temp[4];
                    snprintf(temp, sizeof(temp), "%02X ", rx_buffer_[i]);
                    strcat(hex_str, temp);
                    if ((i + 1) % 16 == 0)
                    {
                        ESP_LOGD(TAG, "[调试] %s", hex_str);
                        hex_str[0] = '\0';
                    }
                }
                if (strlen(hex_str) > 0)
                {
                    ESP_LOGD(TAG, "[调试] %s", hex_str);
                }

                // 查找数据包起始位置（0xAA 0x01）
                size_t packet_start = 0;
                bool   found        = false;

                ESP_LOGD(TAG, "[调试] 查找数据包起始位置 (0x%02X 0x%02X)...", PACKET_HEADER_1, PACKET_HEADER_2);
                for (size_t i = 0; i <= (size_t)read_len - PACKET_SIZE; i++)
                {
                    if (rx_buffer_[i] == PACKET_HEADER_1 && rx_buffer_[i + 1] == PACKET_HEADER_2)
                    {
                        packet_start = i;
                        found        = true;
                        ESP_LOGD(TAG, "[调试] 找到数据包起始位置: 偏移 %lu", (unsigned long)i);
                        break;
                    }
                }

                if (!found)
                {
                    ESP_LOGW(TAG, "[错误] 未找到数据包起始标记 (0x%02X 0x%02X)", PACKET_HEADER_1,
                             PACKET_HEADER_2);
                    ESP_LOGW(TAG, "[错误] 接收到的前20字节 (HEX):");
                    char hex_str[128] = {0};
                    int  show_len    = (read_len > 20) ? 20 : read_len;
                    for (int i = 0; i < show_len; i++)
                    {
                        char temp[4];
                        snprintf(temp, sizeof(temp), "%02X ", rx_buffer_[i]);
                        strcat(hex_str, temp);
                    }
                    ESP_LOGW(TAG, "[错误] %s", hex_str);
                    return false;
                }

                // 如果找到的包不是从缓冲区开始，需要确保有完整的数据
                if (packet_start + PACKET_SIZE > (size_t)read_len)
                {
                    ESP_LOGD(TAG, "数据包不完整，需要读取更多数据");
                    // 需要读取更多数据
                    int needed = PACKET_SIZE - (read_len - packet_start);
                    int additional = uart_read_bytes(uart_num_, &rx_buffer_[read_len], needed,
                                                     200 / portTICK_PERIOD_MS);
                    if (additional < needed)
                    {
                        ESP_LOGW(TAG, "读取剩余数据失败: 需要 %d 字节，只读取到 %d 字节", needed, additional);
                        return false;
                    }
                    read_len += additional;
                }

                // 解析数据包
                ESP_LOGD(TAG, "[调试] 开始解析数据包，起始位置: %lu, 长度: %d", (unsigned long)packet_start, PACKET_SIZE);
                bool result = parsePacket(&rx_buffer_[packet_start], PACKET_SIZE, data);
                if (!result)
                {
                    ESP_LOGW(TAG, "[错误] 数据包解析失败");
                }
                return result;
            }

            bool M0404::startDataCollection(uint32_t interval_ms)
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
                task_config.name       = "m0404_collect";
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

            bool M0404::stopDataCollection()
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

            void M0404::dataCollectionTaskFunction(void* param)
            {
                ESP_LOGI(TAG, "压力传感器数据采集任务已启动");

                // 保存上一次的压力值，用于检测变化
                std::array<uint16_t, PRESSURE_COUNT> last_pressures;
                last_pressures.fill(0);
                bool has_last_data = false;
                int  last_pressure_status = -1; // 上次的压力状态，-1表示未初始化

                uint32_t last_log_time = 0;

                while (collection_running_)
                {
                    // 读取压力数据
                    PressureData data;
                    if (read(data))
                    {
                        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

                        // 判断是否有压力（16个压力值中任何一个>0）
                        bool has_pressure = false;
                        for (size_t i = 0; i < PRESSURE_COUNT; i++)
                        {
                            if (data.pressures[i] > 0)
                            {
                                has_pressure = true;
                                break;
                            }
                        }
                        int current_status = has_pressure ? 1 : 0;
                        // 更新当前状态
                        current_pressure_status_ = current_status;

                        // 检查是否有压力值变化
                        bool has_change = false;
                        if (!has_last_data)
                        {
                            // 第一次读取，检查是否有非零值
                            has_change = has_pressure;
                        }
                        else
                        {
                            // 比较是否有变化
                            for (size_t i = 0; i < PRESSURE_COUNT; i++)
                            {
                                if (data.pressures[i] != last_pressures[i])
                                {
                                    has_change = true;
                                    break;
                                }
                            }
                        }

                        // 只在有压力时输出（不按固定间隔输出）
                        if (has_pressure && has_change)
                        {
                            // 只显示有压力的传感器
                            bool has_output = false;
                            for (size_t i = 0; i < PRESSURE_COUNT; i++)
                            {
                                // 显示：1) 当前值 > 0，或 2) 值有变化
                                if (data.pressures[i] > 0 || (has_last_data && data.pressures[i] != last_pressures[i]))
                                {
                                    if (!has_output)
                                    {
                                        ESP_LOGI(TAG, "========== M0404 压力数据 ==========");
                                        ESP_LOGI(TAG, "  压力状态: 有压力");
                                        has_output = true;
                                    }
                                    if (has_last_data && data.pressures[i] != last_pressures[i])
                                    {
                                        ESP_LOGI(TAG, "  传感器[%2lu]: %5u -> %5u", 
                                                 (unsigned long)i, last_pressures[i], data.pressures[i]);
                                    }
                                    else if (data.pressures[i] > 0)
                                    {
                                        ESP_LOGI(TAG, "  传感器[%2lu]: %5u", 
                                                 (unsigned long)i, data.pressures[i]);
                                    }
                                }
                            }
                            if (has_output)
                            {
                                ESP_LOGI(TAG, "==========================================");
                            }

                            // 保存当前值
                            last_pressures = data.pressures;
                            has_last_data = true;
                            last_log_time = current_time;
                        }
                        else
                        {
                            // 即使没有变化，也更新保存的值（用于后续比较）
                            if (!has_last_data)
                            {
                                last_pressures = data.pressures;
                                has_last_data = true;
                            }
                        }

                        // 触发回调（只在状态变化时，且只在有压力时输出）
                        if (last_pressure_status != current_status || last_pressure_status == -1)
                        {
                            last_pressure_status = current_status;
                            
                            // 只在有压力时才显示状态变化信息和调用回调
                            if (current_status == 1)
                            {
                                ESP_LOGI(TAG, "压力状态变化: %d (有压力)", current_status);
                                
                                // 调用回调函数
                                if (pressure_status_callback_ != nullptr)
                                {
                                    pressure_status_callback_(current_status);
                                }
                            }
                            else
                            {
                                // 无压力时只调用回调，不输出日志
                                if (pressure_status_callback_ != nullptr)
                                {
                                    pressure_status_callback_(current_status);
                                }
                            }
                        }
                    }
                    else
                    {
                        // 读取失败，短暂延迟
                        app::sys::task::TaskManager::delayMs(100);
                    }
                }

                ESP_LOGI(TAG, "压力传感器数据采集任务已停止");
            }

        } // namespace pressure
    }     // namespace device
} // namespace app


