#pragma once

#include <cstdint>
#include <driver/gpio.h>
#include <driver/i2c_master.h>

#include "config/config.hpp"

namespace app
{
    namespace media
    {
        namespace camera
        {
            /**
             * @brief 摄像头配置结构体
             */
            struct Config
            {
                i2c_master_bus_handle_t i2c_master_handle; // I2C 主设备句柄

                // DVP 数据线引脚 (8位数据)
                gpio_num_t dvp_d0;
                gpio_num_t dvp_d1;
                gpio_num_t dvp_d2;
                gpio_num_t dvp_d3;
                gpio_num_t dvp_d4;
                gpio_num_t dvp_d5;
                gpio_num_t dvp_d6;
                gpio_num_t dvp_d7;

                // DVP 控制信号引脚
                gpio_num_t dvp_vsync; // 垂直同步
                gpio_num_t dvp_de;    // 数据使能
                gpio_num_t dvp_pclk;  // 像素时钟
                gpio_num_t dvp_xclk;  // 输出时钟

                // 摄像头控制引脚
                gpio_num_t reset_pin; // 复位引脚
                gpio_num_t pwdn_pin;  // 电源休眠引脚

                // 时钟频率配置
                uint32_t xclk_freq; // 输出时钟频率 (Hz)
                uint32_t sccb_freq; // SCCB(I2C) 频率 (Hz)

                // 图像配置
                uint32_t frame_width;  // 帧宽度
                uint32_t frame_height; // 帧高度

                // 缓冲区配置
                uint32_t buffer_count;  // 缓冲区数量
                uint32_t buffer_size;  // 单个缓冲区大小（字节），0表示自动计算

                Config()
                    : i2c_master_handle(nullptr), dvp_d0(config::CAM_DVP_D0),
                      dvp_d1(config::CAM_DVP_D1), dvp_d2(config::CAM_DVP_D2),
                      dvp_d3(config::CAM_DVP_D3), dvp_d4(config::CAM_DVP_D4),
                      dvp_d5(config::CAM_DVP_D5), dvp_d6(config::CAM_DVP_D6),
                      dvp_d7(config::CAM_DVP_D7), dvp_vsync(config::CAM_DVP_VSYNC),
                      dvp_de(config::CAM_DVP_DE), dvp_pclk(config::CAM_DVP_PCLK),
                      dvp_xclk(config::CAM_DVP_XCLK), reset_pin(config::CAM_RESET_PIN),
                      pwdn_pin(config::CAM_PWDN_PIN), xclk_freq(config::CAM_XCLK_FREQ),
                      sccb_freq(100000), frame_width(1280), frame_height(720),
                      buffer_count(3), buffer_size(0)  // buffer_size=0表示自动计算
                {
                }
            };

            /**
             * @brief 摄像头类
             */
            class Camera
            {
            public:
                Camera();
                ~Camera();

                /**
                 * @brief 初始化摄像头
                 * @param config 摄像头配置参数，如果为 nullptr 则使用默认配置
                 * @return true 成功，false 失败
                 */
                bool init(const Config* config = nullptr);

                /**
                 * @brief 反初始化摄像头，释放所有资源
                 * @return true 成功，false 失败
                 */
                bool deinit();

                /**
                 * @brief 检查摄像头是否已初始化
                 * @return true 已初始化，false 未初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 获取视频设备文件描述符
                 * @return 文件描述符，如果未初始化则返回 -1
                 */
                int getDeviceFd() const
                {
                    return device_fd_;
                }

                /**
                 * @brief 启动视频流
                 * @return true 成功，false 失败
                 * @note 必须在 init() 成功后调用
                 */
                bool startStream();

                /**
                 * @brief 停止视频流
                 * @return true 成功，false 失败
                 */
                bool stopStream();

                /**
                 * @brief 检查视频流是否正在运行
                 * @return true 正在运行，false 未运行
                 */
                bool isStreaming() const
                {
                    return streaming_;
                }

            private:
                Config config_;           // 摄像头配置
                bool   initialized_;      // 是否已初始化
                int    device_fd_;        // 视频设备文件描述符
                uint8_t* buffers_[4];     // 缓冲区指针数组
                uint32_t buffer_sizes_[4]; // 缓冲区大小数组
                uint32_t buffer_count_;   // 实际缓冲区数量
                bool    streaming_;       // 是否正在流式传输
                
                /**
                 * @brief 清理缓冲区资源
                 */
                void cleanupBuffers();
            };
        } // namespace camera
    } // namespace media
} // namespace app
