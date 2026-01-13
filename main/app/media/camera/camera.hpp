#pragma once

#include <cstdint>
#include <driver/i2c_master.h>
#include <esp_video_init.h>
#include <esp_video_device.h>
#include <linux/videodev2.h>
#include <memory>
#include <string>
#include <vector>

#include "config/config.hpp"

namespace app
{
    namespace media
    {
        namespace camera
        {
            /**
             * @brief 摄像头视频格式
             */
            enum class PixelFormat
            {
                RGB565, // 16位RGB格式
                RGB24,  // 24位RGB格式
                YUV422, // YUV422格式
                YUV420, // YUV420格式
                JPEG,   // JPEG压缩格式
                UNKNOWN
            };

            /**
             * @brief 摄像头分辨率
             */
            struct Resolution
            {
                uint16_t width;
                uint16_t height;

                Resolution() : width(0), height(0) {}
                Resolution(uint16_t w, uint16_t h) : width(w), height(h) {}
            };

            /**
             * @brief 帧缓冲区结构
             */
            struct FrameBuffer
            {
                uint8_t*    data;   // 图像数据指针
                size_t      len;    // 数据长度
                Resolution  res;    // 分辨率
                PixelFormat format; // 像素格式

                FrameBuffer() : data(nullptr), len(0), format(PixelFormat::UNKNOWN) {}

                ~FrameBuffer()
                {
                    if (data)
                    {
                        free(data);
                        data = nullptr;
                    }
                }

                // 禁止拷贝
                FrameBuffer(const FrameBuffer&)            = delete;
                FrameBuffer& operator=(const FrameBuffer&) = delete;

                // 允许移动
                FrameBuffer(FrameBuffer&& other) noexcept
                    : data(other.data), len(other.len), res(other.res), format(other.format)
                {
                    other.data = nullptr;
                    other.len  = 0;
                }
            };

            /**
             * @brief 摄像头配置结构
             */
            struct Config
            {
                i2c_master_bus_handle_t i2c_handle;   // I2C 总线句柄
                uint32_t                xclk_freq;    // 外部时钟频率
                Resolution              resolution;   // 默认分辨率
                PixelFormat             pixel_format; // 默认像素格式
                bool                    auto_detect;  // 是否自动检测传感器

                Config()
                    : i2c_handle(nullptr), xclk_freq(config::CAM_XCLK_FREQ), resolution(240, 240),
                      pixel_format(PixelFormat::RGB565), auto_detect(true)
                {
                }
            };

            /**
             * @brief 摄像头驱动类
             */
            class Camera
            {
            public:
                Camera();
                ~Camera();

                /**
                 * @brief 初始化摄像头
                 * @param config 配置参数
                 * @return true=成功, false=失败
                 */
                bool init(const Config* config = nullptr);

                /**
                 * @brief 反初始化
                 */
                void deinit();

                /**
                 * @brief 捕获一帧图像
                 * @param frame_out 输出帧缓冲区（自动分配内存）
                 * @param skip_frames 跳过的帧数（默认2，获取最新帧）
                 * @return true=成功, false=失败
                 */
                bool capture(FrameBuffer& frame_out, int skip_frames = 2);

                /**
                 * @brief 获取当前分辨率
                 */
                Resolution getResolution() const
                {
                    return current_resolution_;
                }

                /**
                 * @brief 获取当前像素格式
                 */
                PixelFormat getPixelFormat() const
                {
                    return current_format_;
                }

                /**
                 * @brief 设置水平镜像
                 */
                bool setHMirror(bool enable);

                /**
                 * @brief 设置垂直翻转
                 */
                bool setVFlip(bool enable);

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 获取传感器名称
                 */
                std::string getSensorName() const
                {
                    return sensor_name_;
                }

            private:
                // 初始化 DVP 视频设备
                bool initDvpDevice();

                // 初始化 V4L2 视频设备
                bool initV4l2Device();

                // 选择最佳像素格式
                uint32_t selectBestFormat();

                // 像素格式转换
                static PixelFormat toPixelFormat(uint32_t v4l2_fmt);
                static uint32_t    fromPixelFormat(PixelFormat fmt);

                // 成员变量
                Config      config_;
                bool        initialized_;
                int         video_fd_;     // V4L2 设备文件描述符
                bool        streaming_on_; // 流是否已启动
                std::string sensor_name_;  // 传感器名称

                Resolution  current_resolution_; // 当前分辨率
                PixelFormat current_format_;     // 当前格式
                uint32_t    sensor_format_;      // 传感器输出格式 (V4L2格式)

                // mmap 缓冲区
                struct MmapBuffer
                {
                    void*  start;
                    size_t length;
                };
                std::vector<MmapBuffer> mmap_buffers_;
            };

        } // namespace camera
    } // namespace media
} // namespace app
