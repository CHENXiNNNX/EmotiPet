#include "m0404.hpp"

#include <cstring>
#include <cstdlib>
#include <cstdint>
#include <climits>
#include <vector>
#include <algorithm>
#include <esp_log.h>
#include "system/task/task.hpp"
#include "driver/uart.h"
#include "nvs.h"

static const char* const TAG = "M0404";
static const char* const NVS_NAMESPACE = "m0404";
static const char* const NVS_KEY_ZERO_POINT = "zero_point";

namespace app
{
    namespace device
    {
        namespace m0404
        {
            M0404::~M0404()
            {
                stopDataCollection();
                deinit();
            }

            bool M0404::init(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin,
                             int baud_rate)
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
                uart_config.baud_rate     = baud_rate;
                uart_config.data_bits     = UART_DATA_8_BITS;
                uart_config.parity        = UART_PARITY_DISABLE;
                uart_config.stop_bits     = UART_STOP_BITS_1;
                uart_config.flow_ctrl     = UART_HW_FLOWCTRL_DISABLE;
                uart_config.source_clk    = UART_SCLK_DEFAULT;

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

                ret =
                    uart_set_pin(uart_num_, tx_pin, rx_pin, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "设置 UART 引脚失败: %s", esp_err_to_name(ret));
                    uart_driver_delete(uart_num_);
                    return false;
                }

                initialized_ = true;
                
                // 初始化零点值
                zero_points_.fill(0);
                zero_point_calibrated_ = false;
                
                // 初始化触摸检测
                last_row_pressures_.fill(0);
                row_active_history_.fill(false);
                row_activate_time_.fill(0); // 0表示未激活
                last_touch_detect_time_ = 0;
                last_activated_row_ = -1;
                current_activated_row_ = -1;
                
                // 尝试从NVS加载零点值
                if (loadZeroPointFromNVS())
                {
                    ESP_LOGI(TAG, "已从NVS加载零点标定值");
                }
                else
                {
                    ESP_LOGI(TAG, "未找到保存的零点标定值，需要执行标定");
                }
                
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
                    ESP_LOGW(TAG, "[错误] 数据包长度不足: %lu (需要 %d)", (unsigned long)length,
                             PACKET_SIZE);
                    return false;
                }

                // 检查包头
                if (buffer[0] != PACKET_HEADER_1 || buffer[1] != PACKET_HEADER_2)
                {
                    ESP_LOGW(TAG, "[错误] 包头错误: 0x%02X 0x%02X (期望 0x%02X 0x%02X)", buffer[0],
                             buffer[1], PACKET_HEADER_1, PACKET_HEADER_2);
                    return false;
                }

                ESP_LOGD(TAG, "[调试] 包头验证通过: 0x%02X 0x%02X", buffer[0], buffer[1]);

                // 计算校验和（前34字节）
                uint8_t calculated_checksum = calculateChecksum(buffer, PACKET_SIZE - 1);
                uint8_t received_checksum   = buffer[PACKET_SIZE - 1];

                ESP_LOGD(TAG, "[调试] 校验和: 计算值=0x%02X, 接收值=0x%02X", calculated_checksum,
                         received_checksum);

                if (calculated_checksum != received_checksum)
                {
                    ESP_LOGW(TAG, "[错误] 校验和错误: 计算值=0x%02X, 接收值=0x%02X",
                             calculated_checksum, received_checksum);
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
                    size_t  offset    = 2 + i * 2; // 跳过2字节包头
                    uint8_t high_byte = buffer[offset];
                    uint8_t low_byte  = buffer[offset + 1];
                    data.pressures[i] = (high_byte << 8) | low_byte;
                    ESP_LOGD(TAG, "[调试] 压力[%lu]: 原始字节 [0x%02X 0x%02X] = %u",
                             (unsigned long)i, high_byte, low_byte, data.pressures[i]);
                }

                data.valid     = true;
                data.timestamp = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGD(TAG, "[调试] 数据包解析完成，时间戳: %lu ms",
                         (unsigned long)data.timestamp);
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
                    ESP_LOGD(TAG, "[调试] 缓冲区数据不足 (%lu < %d)，等待数据到达（最多500ms）",
                             (unsigned long)available, PACKET_SIZE);
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
                    ESP_LOGD(TAG, "[调试] 未读取到任何数据，缓冲区: %lu 字节",
                             (unsigned long)available);
                    return false;
                }

                if (read_len < PACKET_SIZE)
                {
                    ESP_LOGD(TAG, "[调试] 读取数据不足: %d 字节 (需要 %d 字节)", read_len,
                             PACKET_SIZE);
                    // 打印前几个字节用于调试
                    if (read_len > 0)
                    {
                        ESP_LOGD(TAG,
                                 "[调试] 接收到的前 %d 字节 (HEX):", read_len > 16 ? 16 : read_len);
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
                int  print_len    = (read_len > PACKET_SIZE) ? PACKET_SIZE : read_len;
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

                ESP_LOGD(TAG, "[调试] 查找数据包起始位置 (0x%02X 0x%02X)...", PACKET_HEADER_1,
                         PACKET_HEADER_2);
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
                    int  show_len     = (read_len > 20) ? 20 : read_len;
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
                    int needed     = PACKET_SIZE - (read_len - packet_start);
                    int additional = uart_read_bytes(uart_num_, &rx_buffer_[read_len], needed,
                                                     200 / portTICK_PERIOD_MS);
                    if (additional < needed)
                    {
                        ESP_LOGW(TAG, "读取剩余数据失败: 需要 %d 字节，只读取到 %d 字节", needed,
                                 additional);
                        return false;
                    }
                    read_len += additional;
                }

                // 解析数据包
                ESP_LOGD(TAG, "[调试] 开始解析数据包，起始位置: %lu, 长度: %d",
                         (unsigned long)packet_start, PACKET_SIZE);
                bool result = parsePacket(&rx_buffer_[packet_start], PACKET_SIZE, data);
                if (!result)
                {
                    ESP_LOGW(TAG, "[错误] 数据包解析失败");
                    return false;
                }
                
                // 应用零点补偿
                applyZeroPointCompensation(data);
                
                return true;
            }

            bool M0404::readRaw(PressureData& data)
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化");
                    return false;
                }

                // 检查缓冲区中是否有数据
                size_t available = 0;
                uart_get_buffered_data_len(uart_num_, &available);

                // 如果缓冲区数据不足一个完整包，等待数据到达
                int read_len = 0;
                if (available < PACKET_SIZE)
                {
                    read_len = uart_read_bytes(uart_num_, rx_buffer_, sizeof(rx_buffer_),
                                              500 / portTICK_PERIOD_MS);
                }
                else
                {
                    read_len = uart_read_bytes(uart_num_, rx_buffer_, sizeof(rx_buffer_), 0);
                }

                if (read_len <= 0 || read_len < PACKET_SIZE)
                {
                    return false;
                }

                // 查找数据包起始位置（0xAA 0x01）
                size_t packet_start = 0;
                bool   found        = false;

                for (size_t i = 0; i <= (size_t)read_len - PACKET_SIZE; i++)
                {
                    if (rx_buffer_[i] == PACKET_HEADER_1 && rx_buffer_[i + 1] == PACKET_HEADER_2)
                    {
                        packet_start = i;
                        found        = true;
                        break;
                    }
                }

                if (!found)
                {
                    return false;
                }

                // 如果找到的包不是从缓冲区开始，需要确保有完整的数据
                if (packet_start + PACKET_SIZE > (size_t)read_len)
                {
                    int needed = PACKET_SIZE - (read_len - packet_start);
                    int additional = uart_read_bytes(uart_num_, &rx_buffer_[read_len], needed,
                                                     200 / portTICK_PERIOD_MS);
                    if (additional < needed)
                    {
                        return false;
                    }
                    read_len += additional;
                }

                // 解析数据包（不应用零点补偿）
                return parsePacket(&rx_buffer_[packet_start], PACKET_SIZE, data);
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
                bool has_last_data        = false;
                int  last_pressure_status = -1; // 上次的压力状态，-1表示未初始化

                uint32_t last_log_time = 0;

                while (collection_running_)
                {
                    // 读取压力数据
                    PressureData data;
                    if (read(data))
                    {
                        // 更新最新的压力数据（供外部获取）
                        latest_data_ = data;
                        
                        uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;

                        // 判断是否有压力（16个压力值中任何一个超过死区阈值）
                        bool has_pressure = false;
                        for (size_t i = 0; i < PRESSURE_COUNT; i++)
                        {
                            if (data.pressures[i] > DEAD_ZONE_THRESHOLD)
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
                        // 检查是否有任何传感器的值超过死区阈值
                        bool has_above_threshold = false;
                        for (size_t i = 0; i < PRESSURE_COUNT; i++)
                        {
                            if (data.pressures[i] > DEAD_ZONE_THRESHOLD || 
                                (has_last_data && last_pressures[i] > DEAD_ZONE_THRESHOLD))
                            {
                                has_above_threshold = true;
                                break;
                            }
                        }

                        if (has_pressure && has_change && has_above_threshold)
                        {
                            // 日志输出已注释
                            // ESP_LOGI(TAG, "========== M0404 压力数据 ==========");
                            // ESP_LOGI(TAG, "  压力状态: 有压力");
                            // for (size_t i = 0; i < PRESSURE_COUNT; i++)
                            // {
                            //     if (has_last_data && data.pressures[i] != last_pressures[i])
                            //     {
                            //         ESP_LOGI(TAG, "  传感器[%2lu]: %5u -> %5u", 
                            //                  (unsigned long)i, last_pressures[i], data.pressures[i]);
                            //     }
                            //     else if (data.pressures[i] > DEAD_ZONE_THRESHOLD)
                            //     {
                            //         ESP_LOGI(TAG, "  传感器[%2lu]: %5u", 
                            //                  (unsigned long)i, data.pressures[i]);
                            //     }
                            // }
                            // ESP_LOGI(TAG, "==========================================");

                            // 保存当前值
                            last_pressures = data.pressures;
                            has_last_data  = true;
                            last_log_time  = current_time;
                        }
                        else
                        {
                            // 即使没有变化，也更新保存的值（用于后续比较）
                            if (!has_last_data)
                            {
                                last_pressures = data.pressures;
                                has_last_data  = true;
                            }
                        }

                        // 检测触摸状态和方向（有压力时）
                        if (has_pressure)
                        {
                            detectTouchState(data);
                        }

                        // 触发回调（只在状态变化时，且只在有压力时输出）
                        if (last_pressure_status != current_status || last_pressure_status == -1)
                        {
                            last_pressure_status = current_status;

                            // 只在有压力时才显示状态变化信息和调用回调
                            if (current_status == 1)
                            {
                                // ESP_LOGI(TAG, "压力状态变化: %d (有压力)", current_status);

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

            bool M0404::calibrateZeroPoint(uint32_t sample_count, uint32_t sample_interval_ms)
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "传感器未初始化，无法执行零点标定");
                    return false;
                }

                ESP_LOGI(TAG, "开始零点标定，采集次数: %lu, 间隔: %lu ms", (unsigned long)sample_count, (unsigned long)sample_interval_ms);

                // 累加数组和统计信息
                std::array<uint32_t, PRESSURE_COUNT> sum;
                std::array<uint16_t, PRESSURE_COUNT> min_val, max_val;
                sum.fill(0);
                min_val.fill(UINT16_MAX);
                max_val.fill(0);
                uint32_t valid_samples = 0;

                // 采集数据
                for (uint32_t i = 0; i < sample_count; i++)
                {
                    PressureData data;
                    // 使用readRaw读取原始值（不应用零点补偿）
                    if (readRaw(data))
                    {
                        // 累加原始值，并记录最小最大值
                        for (size_t j = 0; j < PRESSURE_COUNT; j++)
                        {
                            sum[j] += data.pressures[j];
                            if (data.pressures[j] < min_val[j])
                            {
                                min_val[j] = data.pressures[j];
                            }
                            if (data.pressures[j] > max_val[j])
                            {
                                max_val[j] = data.pressures[j];
                            }
                        }
                        valid_samples++;
                    }
                    
                    if (i < sample_count - 1) // 最后一次不需要延迟
                    {
                        app::sys::task::TaskManager::delayMs(sample_interval_ms);
                    }
                }

                if (valid_samples == 0)
                {
                    ESP_LOGE(TAG, "零点标定失败：未能采集到有效数据");
                    return false;
                }

                // 计算平均值，并加上波动范围的一半作为安全余量
                // 这样可以确保标定后的值更稳定
                for (size_t i = 0; i < PRESSURE_COUNT; i++)
                {
                    uint32_t avg = sum[i] / valid_samples;
                    uint16_t range = max_val[i] - min_val[i];
                    // 零点值 = 平均值 + 波动范围的一半，这样标定后相对值会更接近0
                    zero_points_[i] = (uint16_t)(avg + range / 2);
                }

                zero_point_calibrated_ = true;

                // 保存到NVS
                if (saveZeroPointToNVS())
                {
                    ESP_LOGI(TAG, "零点标定值已保存到NVS");
                }
                else
                {
                    ESP_LOGW(TAG, "保存零点标定值到NVS失败");
                }

                // 打印标定结果
                ESP_LOGI(TAG, "零点标定完成（有效样本数: %lu）:", (unsigned long)valid_samples);
                for (size_t i = 0; i < PRESSURE_COUNT; i++)
                {
                    uint32_t avg = sum[i] / valid_samples;
                    ESP_LOGI(TAG, "  传感器[%2lu]: 平均值=%5u, 范围=[%5u-%5u], 零点值=%5u", 
                             (unsigned long)i, avg, min_val[i], max_val[i], zero_points_[i]);
                }

                return true;
            }

            bool M0404::clearZeroPoint()
            {
                zero_points_.fill(0);
                zero_point_calibrated_ = false;

                // 从NVS删除
                nvs_handle_t nvs_handle;
                esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
                if (ret == ESP_OK)
                {
                    nvs_erase_key(nvs_handle, NVS_KEY_ZERO_POINT);
                    nvs_commit(nvs_handle);
                    nvs_close(nvs_handle);
                    ESP_LOGI(TAG, "零点标定值已清除");
                }

                return true;
            }

            bool M0404::getZeroPoint(std::array<uint16_t, PRESSURE_COUNT>& zero_points) const
            {
                zero_points = zero_points_;
                return zero_point_calibrated_;
            }

            void M0404::applyZeroPointCompensation(PressureData& data) const
            {
                if (!zero_point_calibrated_)
                {
                    return; // 未标定，不应用补偿
                }

                for (size_t i = 0; i < PRESSURE_COUNT; i++)
                {
                    // 计算相对压力值（原始值 - 零点值）
                    // 使用有符号运算，避免下溢
                    int32_t compensated = (int32_t)data.pressures[i] - (int32_t)zero_points_[i];
                    // 如果补偿后为负值，设为0
                    data.pressures[i] = (compensated > 0) ? (uint16_t)compensated : 0;
                }
            }

            bool M0404::loadZeroPointFromNVS()
            {
                nvs_handle_t nvs_handle;
                esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
                if (ret != ESP_OK)
                {
                    return false;
                }

                size_t required_size = sizeof(zero_points_);
                ret = nvs_get_blob(nvs_handle, NVS_KEY_ZERO_POINT, zero_points_.data(), &required_size);
                nvs_close(nvs_handle);

                if (ret == ESP_OK && required_size == sizeof(zero_points_))
                {
                    zero_point_calibrated_ = true;
                    return true;
                }

                return false;
            }

            bool M0404::saveZeroPointToNVS() const
            {
                nvs_handle_t nvs_handle;
                esp_err_t ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "打开NVS失败: %s", esp_err_to_name(ret));
                    return false;
                }

                ret = nvs_set_blob(nvs_handle, NVS_KEY_ZERO_POINT, zero_points_.data(), sizeof(zero_points_));
                if (ret == ESP_OK)
                {
                    ret = nvs_commit(nvs_handle);
                }
                nvs_close(nvs_handle);

                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "保存零点值到NVS失败: %s", esp_err_to_name(ret));
                    return false;
                }

                return true;
            }

            void M0404::detectTouchState(const PressureData& data)
            {
                if (touch_state_callback_ == nullptr)
                {
                    return; // 没有设置回调，不检测
                }

                uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                
                // 限制检测频率
                if (current_time - last_touch_detect_time_ < TOUCH_DETECT_INTERVAL_MS)
                {
                    return;
                }
                last_touch_detect_time_ = current_time;

                // 计算每行的最大压力值
                // 行0: 传感器[12, 13, 14, 15] (最上面)
                // 行1: 传感器[8, 9, 10, 11]
                // 行2: 传感器[4, 5, 6, 7]
                // 行3: 传感器[0, 1, 2, 3] (最下面)
                std::array<uint16_t, 4> row_max_pressures;
                row_max_pressures.fill(0);
                
                // 行0: 传感器[12, 13, 14, 15]
                for (size_t i = 12; i < 16; i++)
                {
                    if (data.pressures[i] > row_max_pressures[0])
                    {
                        row_max_pressures[0] = data.pressures[i];
                    }
                }
                
                // 行1: 传感器[8, 9, 10, 11]
                for (size_t i = 8; i < 12; i++)
                {
                    if (data.pressures[i] > row_max_pressures[1])
                    {
                        row_max_pressures[1] = data.pressures[i];
                    }
                }
                
                // 行2: 传感器[4, 5, 6, 7]
                for (size_t i = 4; i < 8; i++)
                {
                    if (data.pressures[i] > row_max_pressures[2])
                    {
                        row_max_pressures[2] = data.pressures[i];
                    }
                }
                
                // 行3: 传感器[0, 1, 2, 3]
                for (size_t i = 0; i < 4; i++)
                {
                    if (data.pressures[i] > row_max_pressures[3])
                    {
                        row_max_pressures[3] = data.pressures[i];
                    }
                }

                // 找出所有行的最大压力值
                uint16_t max_pressure = 0;
                for (size_t i = 0; i < 4; i++)
                {
                    if (row_max_pressures[i] > max_pressure)
                    {
                        max_pressure = row_max_pressures[i];
                    }
                }

                // 如果没有有效压力，返回
                if (max_pressure <= DEAD_ZONE_THRESHOLD)
                {
                    return;
                }

                // 判断触摸强度
                TouchIntensity intensity = (max_pressure >= HEAVY_TOUCH_THRESHOLD) ? 
                                           TouchIntensity::HEAVY : TouchIntensity::LIGHT;

                // 检测触摸方向
                // 找出激活的行（压力值超过阈值），找出当前激活的行号
                std::array<bool, 4> row_active;
                int new_activated_row = -1;
                for (size_t i = 0; i < 4; i++)
                {
                    row_active[i] = (row_max_pressures[i] > DEAD_ZONE_THRESHOLD);
                    if (row_active[i])
                    {
                        new_activated_row = i; // 记录当前激活的行号
                    }
                }

                TouchDirection direction = TouchDirection::NONE;
                
                // 如果当前有激活的行，且与上次不同，则判断方向
                if (new_activated_row != -1)
                {
                    // 更新当前激活的行
                    if (current_activated_row_ != new_activated_row)
                    {
                        // 行发生了变化
                        last_activated_row_ = current_activated_row_;
                        current_activated_row_ = new_activated_row;
                        
                        // 如果已经有两次不同的行激活，立即判断方向
                        if (last_activated_row_ != -1 && last_activated_row_ != current_activated_row_)
                        {
                            // 两次不同的行，判断方向
                            if (last_activated_row_ < current_activated_row_)
                            {
                                // 从上往下（行0->行1->行2->行3）
                                direction = TouchDirection::TOP_TO_BOTTOM;
                            }
                            else
                            {
                                // 从下往上（行3->行2->行1->行0）
                                direction = TouchDirection::BOTTOM_TO_TOP;
                            }
                        }
                        // 如果last_activated_row_ == -1，说明是第一次激活，不判断方向
                        // 如果last_activated_row_ == current_activated_row_，说明是同一行，direction保持NONE
                    }
                }
                else
                {
                    // 当前没有激活的行，清除当前激活的行号
                    // 但保留last_activated_row_，以便下次判断
                    current_activated_row_ = -1;
                }

                // 更新历史记录
                row_active_history_ = row_active;
                last_row_pressures_ = row_max_pressures;

                // 调用回调函数
                if (touch_state_callback_ != nullptr)
                {
                    touch_state_callback_(intensity, direction, max_pressure);
                }
            }

        } // namespace pressure
    }     // namespace device
} // namespace app
