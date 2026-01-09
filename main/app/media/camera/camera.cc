#include "camera.hpp"
#include "esp_log.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <errno.h>
#include <cstring>

static const char* const TAG = "Camera";

namespace app
{
    namespace media
    {
        namespace camera
        {
            Camera::Camera() 
                : initialized_(false), device_fd_(-1), buffer_count_(0), streaming_(false)
            {
                memset(buffers_, 0, sizeof(buffers_));
                memset(buffer_sizes_, 0, sizeof(buffer_sizes_));
            }

            Camera::~Camera()
            {
                deinit();
            }

            bool Camera::deinit()
            {
                if (!initialized_)
                {
                    return true;  // 已经反初始化，返回成功
                }

                ESP_LOGI(TAG, "开始释放摄像头资源...");

                // 停止流
                stopStream();

                // 清理缓冲区
                cleanupBuffers();

                // 关闭设备
                if (device_fd_ >= 0)
                {
                    close(device_fd_);
                    device_fd_ = -1;
                }

                // 反初始化视频系统
                esp_err_t ret = esp_video_deinit();
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_video_deinit 失败: %s", esp_err_to_name(ret));
                    initialized_ = false;
                    return false;
                }

                initialized_ = false;
                ESP_LOGI(TAG, "摄像头资源释放完成");
                return true;
            }

            bool Camera::init(const Config* config)
            {
                if (initialized_)
                {
                    ESP_LOGW(TAG, "摄像头已初始化");
                    return true;
                }

                if (config != nullptr)
                {
                    config_ = *config;
                }

                if (config_.i2c_master_handle == nullptr)
                {
                    ESP_LOGE(TAG, "I2C master handle 为空");
                    return false;
                }

                // 配置 DVP 视频设备
                esp_video_init_dvp_config_t dvp_config = {};

                // 配置 SCCB (I2C)
                dvp_config.sccb_config.init_sccb  = false; // 不初始化 I2C，使用已有的
                dvp_config.sccb_config.i2c_handle = config_.i2c_master_handle;
                dvp_config.sccb_config.freq       = config_.sccb_freq;

                // 配置摄像头控制引脚
                dvp_config.reset_pin = config_.reset_pin;
                dvp_config.pwdn_pin  = config_.pwdn_pin;

                // 配置 DVP 数据和控制引脚
                dvp_config.dvp_pin.data_width = CAM_CTLR_DATA_WIDTH_8;
                dvp_config.dvp_pin.data_io[0] = config_.dvp_d0;
                dvp_config.dvp_pin.data_io[1] = config_.dvp_d1;
                dvp_config.dvp_pin.data_io[2] = config_.dvp_d2;
                dvp_config.dvp_pin.data_io[3] = config_.dvp_d3;
                dvp_config.dvp_pin.data_io[4] = config_.dvp_d4;
                dvp_config.dvp_pin.data_io[5] = config_.dvp_d5;
                dvp_config.dvp_pin.data_io[6] = config_.dvp_d6;
                dvp_config.dvp_pin.data_io[7] = config_.dvp_d7;
                dvp_config.dvp_pin.vsync_io   = config_.dvp_vsync;
                dvp_config.dvp_pin.de_io      = config_.dvp_de;
                dvp_config.dvp_pin.pclk_io    = config_.dvp_pclk;
                dvp_config.dvp_pin.xclk_io    = config_.dvp_xclk;

                // 配置输出时钟频率
                dvp_config.xclk_freq = config_.xclk_freq;

                // 创建视频初始化配置
                esp_video_init_config_t video_config = {
                    .dvp = &dvp_config,
                };

                // 初始化视频系统
                esp_err_t ret = esp_video_init(&video_config);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_video_init 失败: %s", esp_err_to_name(ret));
                    return false;
                }

                ESP_LOGI(TAG, "视频系统初始化成功");

                // 打开 DVP 视频设备
                device_fd_ = open(ESP_VIDEO_DVP_DEVICE_NAME, O_RDWR);
                if (device_fd_ < 0)
                {
                    ESP_LOGE(TAG, "打开视频设备失败: %s", ESP_VIDEO_DVP_DEVICE_NAME);
                    esp_video_deinit();
                    return false;
                }

                ESP_LOGI(TAG, "打开视频设备成功: %s (fd=%d)", ESP_VIDEO_DVP_DEVICE_NAME,
                         device_fd_);

                // 查询设备能力
                struct v4l2_capability cap;
                if (ioctl(device_fd_, VIDIOC_QUERYCAP, &cap) == 0)
                {
                    ESP_LOGI(TAG, "设备信息:");
                    ESP_LOGI(TAG, "  驱动: %s", cap.driver);
                    ESP_LOGI(TAG, "  设备: %s", cap.card);
                    ESP_LOGI(TAG, "  总线: %s", cap.bus_info);
                    ESP_LOGI(TAG, "  版本: %d.%d.%d", (cap.version >> 16) & 0xFF,
                             (cap.version >> 8) & 0xFF, cap.version & 0xFF);
                }
                else
                {
                    ESP_LOGW(TAG, "查询设备能力失败");
                }

                // 设置图像格式
                struct v4l2_format fmt  = {};
                fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                fmt.fmt.pix.width       = config_.frame_width;
                fmt.fmt.pix.height      = config_.frame_height;
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG;

                if (ioctl(device_fd_, VIDIOC_S_FMT, &fmt) != 0)
                {
                    ESP_LOGW(TAG, "设置图像格式失败，将使用默认格式");
                    // 获取实际设置的格式
                    if (ioctl(device_fd_, VIDIOC_G_FMT, &fmt) != 0)
                    {
                        ESP_LOGE(TAG, "获取图像格式失败");
                        close(device_fd_);
                        device_fd_ = -1;
                        esp_video_deinit();
                        return false;
                    }
                }
                ESP_LOGI(TAG, "图像格式设置成功: %dx%d, 像素格式=0x%08X", 
                         fmt.fmt.pix.width, fmt.fmt.pix.height, fmt.fmt.pix.pixelformat);

                struct v4l2_requestbuffers req = {};
                req.count  = config_.buffer_count;  
                req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                req.memory = V4L2_MEMORY_MMAP;  // 内存映射模式

                // 验证缓冲区数量范围
                if (req.count < 1 || req.count > 4)
                {
                    ESP_LOGW(TAG, "缓冲区数量 %u 超出范围(1-4)，调整为3", req.count);
                    req.count = 3;
                }

                if (ioctl(device_fd_, VIDIOC_REQBUFS, &req) != 0)
                {
                    ESP_LOGE(TAG, "请求缓冲区失败: errno=%d", errno);
                    close(device_fd_);
                    device_fd_ = -1;
                    esp_video_deinit();
                    return false;
                }

                if (req.count < 1)
                {
                    ESP_LOGE(TAG, "请求的缓冲区数量不足: %u", req.count);
                    close(device_fd_);
                    device_fd_ = -1;
                    esp_video_deinit();
                    return false;
                }

                buffer_count_ = req.count;
                ESP_LOGI(TAG, "成功请求 %u 个缓冲区", buffer_count_);

                for (uint32_t i = 0; i < buffer_count_; i++)
                {
                    struct v4l2_buffer buf = {};
                    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    buf.memory = V4L2_MEMORY_MMAP;
                    buf.index  = i;

                    // 查询缓冲区信息
                    if (ioctl(device_fd_, VIDIOC_QUERYBUF, &buf) != 0)
                    {
                        ESP_LOGE(TAG, "查询缓冲区 %u 失败: errno=%d", i, errno);
                        cleanupBuffers();
                        close(device_fd_);
                        device_fd_ = -1;
                        esp_video_deinit();
                        return false;
                    }

                    // 使用mmap映射缓冲区到用户空间
                    buffers_[i] = (uint8_t*)mmap(NULL, buf.length, 
                                                  PROT_READ | PROT_WRITE, 
                                                  MAP_SHARED, 
                                                  device_fd_, 
                                                  buf.m.offset);

                    if (buffers_[i] == MAP_FAILED)
                    {
                        ESP_LOGE(TAG, "映射缓冲区 %u 失败: errno=%d", i, errno);
                        buffers_[i] = nullptr;
                        cleanupBuffers();
                        close(device_fd_);
                        device_fd_ = -1;
                        esp_video_deinit();
                        return false;
                    }

                    buffer_sizes_[i] = buf.length;
                    ESP_LOGI(TAG, "缓冲区 %u: 地址=%p, 大小=%u 字节", i, buffers_[i], buffer_sizes_[i]);
                }

                // 将缓冲区加入队列
                for (uint32_t i = 0; i < buffer_count_; i++)
                {
                    struct v4l2_buffer buf = {};
                    buf.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    buf.memory = V4L2_MEMORY_MMAP;
                    buf.index  = i;

                    if (ioctl(device_fd_, VIDIOC_QBUF, &buf) != 0)
                    {
                        ESP_LOGE(TAG, "将缓冲区 %u 加入队列失败: errno=%d", i, errno);
                        cleanupBuffers();
                        close(device_fd_);
                        device_fd_ = -1;
                        esp_video_deinit();
                        return false;
                    }
                }
                ESP_LOGI(TAG, "所有缓冲区已加入队列");

                initialized_ = true;
                ESP_LOGI(TAG, "摄像头初始化完成");
                return true;
            }

            bool Camera::startStream()
            {
                if (!initialized_)
                {
                    ESP_LOGE(TAG, "摄像头未初始化，无法启动流");
                    return false;
                }

                if (streaming_)
                {
                    ESP_LOGW(TAG, "视频流已在运行");
                    return true;
                }

                if (device_fd_ < 0)
                {
                    ESP_LOGE(TAG, "设备文件描述符无效");
                    return false;
                }

                int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (ioctl(device_fd_, VIDIOC_STREAMON, &type) != 0)
                {
                    ESP_LOGE(TAG, "启动视频流失败: errno=%d", errno);
                    return false;
                }

                streaming_ = true;
                ESP_LOGI(TAG, "视频流已启动，摄像头可以开始捕获数据");
                return true;
            }

            bool Camera::stopStream()
            {
                if (!streaming_)
                {
                    ESP_LOGW(TAG, "视频流未运行");
                    return true;
                }

                if (device_fd_ < 0)
                {
                    ESP_LOGE(TAG, "设备文件描述符无效");
                    streaming_ = false;
                    return false;
                }

                int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (ioctl(device_fd_, VIDIOC_STREAMOFF, &type) != 0)
                {
                    ESP_LOGE(TAG, "停止视频流失败: errno=%d", errno);
                    return false;
                }

                streaming_ = false;
                ESP_LOGI(TAG, "视频流已停止");
                return true;
            }

            void Camera::cleanupBuffers()
            {
                // 取消映射所有缓冲区
                for (uint32_t i = 0; i < buffer_count_; i++)
                {
                    if (buffers_[i] != nullptr)
                    {
                        munmap(buffers_[i], buffer_sizes_[i]);
                        buffers_[i] = nullptr;
                        buffer_sizes_[i] = 0;
                    }
                }
                buffer_count_ = 0;
            }

        } // namespace camera
    } // namespace media
} // namespace app
