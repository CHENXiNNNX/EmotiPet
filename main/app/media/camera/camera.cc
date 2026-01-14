#include "camera.hpp"

#include <esp_cam_ctlr_dvp.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_video_device.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>

static const char* const TAG = "Camera";

#ifndef MAP_FAILED
#define MAP_FAILED nullptr
#endif

namespace app
{
    namespace media
    {
        namespace camera
        {

            Camera::Camera()
                : initialized_(false), video_fd_(-1), streaming_on_(false),
                  current_format_(PixelFormat::UNKNOWN), sensor_format_(0)
            {
            }

            Camera::~Camera()
            {
                deinit();
            }

            bool Camera::init(const Config* config)
            {
                if (initialized_)
                {
                    ESP_LOGW(TAG, "摄像头已初始化");
                    return true;
                }

                // 使用默认配置或用户配置
                if (config)
                {
                    config_ = *config;
                }
                else
                {
                    // 使用默认配置
                    config_.xclk_freq    = app::config::CAM_XCLK_FREQ;
                    config_.resolution   = Resolution(240, 240);
                    config_.pixel_format = PixelFormat::RGB565;
                }

                if (!config_.i2c_handle)
                {
                    ESP_LOGE(TAG, "I2C 句柄无效");
                    return false;
                }

                // 初始化 DVP 设备
                if (!initDvpDevice())
                {
                    ESP_LOGE(TAG, "DVP 设备初始化失败");
                    return false;
                }

                // 初始化 V4L2 设备
                if (!initV4l2Device())
                {
                    ESP_LOGE(TAG, "V4L2 设备初始化失败");
                    deinit();
                    return false;
                }

                initialized_ = true;
                ESP_LOGI(TAG, "摄像头初始化成功: %s, %dx%d", sensor_name_.c_str(),
                         current_resolution_.width, current_resolution_.height);

                return true;
            }

            void Camera::deinit()
            {
                if (!initialized_)
                {
                    return;
                }

                // 停止视频流
                if (streaming_on_ && video_fd_ >= 0)
                {
                    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    ioctl(video_fd_, VIDIOC_STREAMOFF, &type);
                    streaming_on_ = false;
                }

                // 释放 mmap 缓冲区
                for (auto& buf : mmap_buffers_)
                {
                    if (buf.start && buf.length)
                    {
                        munmap(buf.start, buf.length);
                    }
                }
                mmap_buffers_.clear();

                // 关闭设备
                if (video_fd_ >= 0)
                {
                    close(video_fd_);
                    video_fd_ = -1;
                }

                // 反初始化 esp_video
                esp_video_deinit();

                initialized_   = false;
                sensor_format_ = 0;
                ESP_LOGI(TAG, "摄像头已反初始化");
            }

            bool Camera::initDvpDevice()
            {
                // 配置 DVP 引脚
                static esp_cam_ctlr_dvp_pin_config_t s_dvp_pin_config = {
                    .data_width = CAM_CTLR_DATA_WIDTH_8,
                    .data_io =
                        {
                            [0] = app::config::CAM_DVP_D0,
                            [1] = app::config::CAM_DVP_D1,
                            [2] = app::config::CAM_DVP_D2,
                            [3] = app::config::CAM_DVP_D3,
                            [4] = app::config::CAM_DVP_D4,
                            [5] = app::config::CAM_DVP_D5,
                            [6] = app::config::CAM_DVP_D6,
                            [7] = app::config::CAM_DVP_D7,
                        },
                    .vsync_io = app::config::CAM_DVP_VSYNC,
                    .de_io    = app::config::CAM_DVP_HREF,
                    .pclk_io  = app::config::CAM_DVP_PCLK,
                    .xclk_io  = app::config::CAM_DVP_XCLK,
                };

                // 配置 SCCB (复用 I2C)
                esp_video_init_sccb_config_t sccb_config = {
                    .init_sccb  = false, // 使用已有的 I2C
                    .i2c_handle = config_.i2c_handle,
                    .freq       = 100000, // 100kHz
                };

                // 配置 DVP 接口
                esp_video_init_dvp_config_t dvp_config = {
                    .sccb_config = sccb_config,
                    .reset_pin   = app::config::CAM_RESET_PIN,
                    .pwdn_pin    = app::config::CAM_PWDN_PIN,
                    .dvp_pin     = s_dvp_pin_config,
                    .xclk_freq   = config_.xclk_freq,
                };

                // 初始化视频系统
                esp_video_init_config_t video_config = {
                    .dvp = &dvp_config,
                };

                esp_err_t ret = esp_video_init(&video_config);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "esp_video_init 失败: %s", esp_err_to_name(ret));
                    return false;
                }

                ESP_LOGI(TAG, "DVP 设备初始化成功");
                return true;
            }

            bool Camera::initV4l2Device()
            {
                // 打开视频设备（DVP 接口）
                const char* video_device = ESP_VIDEO_DVP_DEVICE_NAME;
                video_fd_                = open(video_device, O_RDWR);
                if (video_fd_ < 0)
                {
                    ESP_LOGE(TAG, "打开 %s 失败: %d", video_device, errno);
                    return false;
                }

                // 查询设备能力
                struct v4l2_capability cap = {};
                if (ioctl(video_fd_, VIDIOC_QUERYCAP, &cap) != 0)
                {
                    ESP_LOGE(TAG, "VIDIOC_QUERYCAP 失败: %d", errno);
                    return false;
                }

                sensor_name_ = reinterpret_cast<const char*>(cap.card);
                ESP_LOGI(TAG, "检测到传感器: %s (驱动=%s)", sensor_name_.c_str(), cap.driver);

                // 获取当前格式
                struct v4l2_format format = {};
                format.type               = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (ioctl(video_fd_, VIDIOC_G_FMT, &format) != 0)
                {
                    ESP_LOGE(TAG, "VIDIOC_G_FMT 失败: %d", errno);
                    return false;
                }

                current_resolution_.width  = format.fmt.pix.width;
                current_resolution_.height = format.fmt.pix.height;

                // 选择最佳格式
                sensor_format_ = selectBestFormat();
                if (sensor_format_ == 0)
                {
                    ESP_LOGE(TAG, "未找到支持的像素格式");
                    return false;
                }

                // 设置格式
                struct v4l2_format setformat  = {};
                setformat.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                setformat.fmt.pix.width       = current_resolution_.width;
                setformat.fmt.pix.height      = current_resolution_.height;
                setformat.fmt.pix.pixelformat = sensor_format_;

                if (ioctl(video_fd_, VIDIOC_S_FMT, &setformat) != 0)
                {
                    ESP_LOGE(TAG, "VIDIOC_S_FMT 失败: %d", errno);
                    return false;
                }

                current_format_ = toPixelFormat(sensor_format_);

                // 打印选择的格式信息
                const char* format_name = "UNKNOWN";
                switch (current_format_)
                {
                case PixelFormat::RGB565:
                    format_name = "RGB565";
                    break;
                case PixelFormat::RGB24:
                    format_name = "RGB24";
                    break;
                case PixelFormat::YUV422:
                    format_name = "YUV422";
                    break;
                case PixelFormat::YUV420:
                    format_name = "YUV420";
                    break;
                case PixelFormat::JPEG:
                    format_name = "JPEG";
                    break;
                default:
                    break;
                }
                ESP_LOGI(TAG, "选择的格式: %s (0x%08lx)", format_name, sensor_format_);

                // 申请缓冲区
                struct v4l2_requestbuffers req = {};
                req.count                      = 1; // DVP 使用 1 个缓冲区
                req.type                       = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                req.memory                     = V4L2_MEMORY_MMAP;

                if (ioctl(video_fd_, VIDIOC_REQBUFS, &req) != 0)
                {
                    ESP_LOGE(TAG, "VIDIOC_REQBUFS 失败: %d", errno);
                    return false;
                }

                // mmap 缓冲区
                mmap_buffers_.resize(req.count);
                for (uint32_t i = 0; i < req.count; i++)
                {
                    struct v4l2_buffer buf = {};
                    buf.type               = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    buf.memory             = V4L2_MEMORY_MMAP;
                    buf.index              = i;

                    if (ioctl(video_fd_, VIDIOC_QUERYBUF, &buf) != 0)
                    {
                        ESP_LOGE(TAG, "VIDIOC_QUERYBUF 失败: %d", errno);
                        return false;
                    }

                    void* start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED,
                                       video_fd_, buf.m.offset);
                    if (start == MAP_FAILED)
                    {
                        ESP_LOGE(TAG, "mmap 失败: %d", errno);
                        return false;
                    }

                    mmap_buffers_[i].start  = start;
                    mmap_buffers_[i].length = buf.length;

                    // 将缓冲区入队
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0)
                    {
                        ESP_LOGE(TAG, "VIDIOC_QBUF 失败: %d", errno);
                        return false;
                    }
                }

                // 启动视频流
                int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                if (ioctl(video_fd_, VIDIOC_STREAMON, &type) != 0)
                {
                    ESP_LOGE(TAG, "VIDIOC_STREAMON 失败: %d", errno);
                    return false;
                }

                streaming_on_ = true;
                ESP_LOGI(TAG, "视频流已启动");

                return true;
            }

            uint32_t Camera::selectBestFormat()
            {
                // 枚举支持的格式，按优先级选择
                struct v4l2_fmtdesc fmtdesc = {};
                fmtdesc.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                fmtdesc.index               = 0;

                uint32_t best_fmt  = 0;
                int      best_rank = 1 << 30; // 大数字

                // 格式优先级 (数字越小优先级越高)
                auto get_rank = [](uint32_t fmt) -> int
                {
                    switch (fmt)
                    {
                    case V4L2_PIX_FMT_YUV422P: // YUYV
                        return 10;
                    case V4L2_PIX_FMT_RGB565:
                        return 11;
                    case V4L2_PIX_FMT_RGB24:
                        return 12;
                    case V4L2_PIX_FMT_YUV420:
                        return 13;
                    case V4L2_PIX_FMT_JPEG:
                        return 5;
                    default:
                        return 1 << 29; // 不支持
                    }
                };

                while (ioctl(video_fd_, VIDIOC_ENUM_FMT, &fmtdesc) == 0)
                {
                    int rank = get_rank(fmtdesc.pixelformat);
                    ESP_LOGD(TAG, "支持格式: 0x%08lx (%s), rank=%d", fmtdesc.pixelformat,
                             fmtdesc.description, rank);

                    if (rank < best_rank)
                    {
                        best_rank = rank;
                        best_fmt  = fmtdesc.pixelformat;
                    }
                    fmtdesc.index++;
                }

                if (best_fmt != 0)
                {
                    ESP_LOGI(TAG, "选择格式: 0x%08lx", best_fmt);
                }

                return best_fmt;
            }

            bool Camera::capture(FrameBuffer& frame_out, int skip_frames)
            {
                if (!initialized_ || !streaming_on_)
                {
                    ESP_LOGE(TAG, "摄像头未初始化或流未启动");
                    return false;
                }

                // 跳过旧帧，获取最新帧
                for (int i = 0; i <= skip_frames; i++)
                {
                    struct v4l2_buffer buf = {};
                    buf.type               = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                    buf.memory             = V4L2_MEMORY_MMAP;

                    // 出队
                    if (ioctl(video_fd_, VIDIOC_DQBUF, &buf) != 0)
                    {
                        ESP_LOGE(TAG, "VIDIOC_DQBUF 失败: %d", errno);
                        return false;
                    }

                    // 最后一帧：拷贝数据到 PSRAM
                    if (i == skip_frames)
                    {
                        size_t   frame_size = buf.bytesused;
                        uint8_t* frame_data = (uint8_t*)heap_caps_malloc(
                            frame_size, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

                        if (!frame_data)
                        {
                            ESP_LOGE(TAG, "分配帧缓冲失败: %u 字节", (unsigned int)frame_size);
                            ioctl(video_fd_, VIDIOC_QBUF, &buf); // 归还缓冲
                            return false;
                        }

                        // 拷贝数据
                        memcpy(frame_data, mmap_buffers_[buf.index].start, frame_size);

                        // 填充输出结构
                        frame_out.data   = frame_data;
                        frame_out.len    = frame_size;
                        frame_out.res    = current_resolution_;
                        frame_out.format = current_format_;

                        ESP_LOGD(TAG, "捕获帧: %ux%u, %u 字节", frame_out.res.width,
                                 frame_out.res.height, (unsigned int)frame_out.len);
                    }

                    // 归还缓冲区
                    if (ioctl(video_fd_, VIDIOC_QBUF, &buf) != 0)
                    {
                        ESP_LOGE(TAG, "VIDIOC_QBUF 失败: %d", errno);
                    }
                }

                return true;
            }

            bool Camera::setHMirror(bool enable)
            {
                if (video_fd_ < 0)
                {
                    return false;
                }

                struct v4l2_ext_controls ctrls = {};
                struct v4l2_ext_control  ctrl  = {};
                ctrl.id                        = V4L2_CID_HFLIP;
                ctrl.value                     = enable ? 1 : 0;
                ctrls.ctrl_class               = V4L2_CTRL_CLASS_USER;
                ctrls.count                    = 1;
                ctrls.controls                 = &ctrl;

                if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0)
                {
                    ESP_LOGE(TAG, "设置水平镜像失败");
                    return false;
                }

                ESP_LOGI(TAG, "水平镜像: %s", enable ? "开启" : "关闭");
                return true;
            }

            bool Camera::setVFlip(bool enable)
            {
                if (video_fd_ < 0)
                {
                    return false;
                }

                struct v4l2_ext_controls ctrls = {};
                struct v4l2_ext_control  ctrl  = {};
                ctrl.id                        = V4L2_CID_VFLIP;
                ctrl.value                     = enable ? 1 : 0;
                ctrls.ctrl_class               = V4L2_CTRL_CLASS_USER;
                ctrls.count                    = 1;
                ctrls.controls                 = &ctrl;

                if (ioctl(video_fd_, VIDIOC_S_EXT_CTRLS, &ctrls) != 0)
                {
                    ESP_LOGE(TAG, "设置垂直翻转失败");
                    return false;
                }

                ESP_LOGI(TAG, "垂直翻转: %s", enable ? "开启" : "关闭");
                return true;
            }

            // 格式转换辅助函数
            PixelFormat Camera::toPixelFormat(uint32_t v4l2_fmt)
            {
                switch (v4l2_fmt)
                {
                case V4L2_PIX_FMT_RGB565:
                    return PixelFormat::RGB565;
                case V4L2_PIX_FMT_RGB24:
                    return PixelFormat::RGB24;
                case V4L2_PIX_FMT_YUV422P:
                case V4L2_PIX_FMT_YUYV:
                    return PixelFormat::YUV422;
                case V4L2_PIX_FMT_YUV420:
                    return PixelFormat::YUV420;
                case V4L2_PIX_FMT_JPEG:
                    return PixelFormat::JPEG;
                default:
                    return PixelFormat::UNKNOWN;
                }
            }

            uint32_t Camera::fromPixelFormat(PixelFormat fmt)
            {
                switch (fmt)
                {
                case PixelFormat::RGB565:
                    return V4L2_PIX_FMT_RGB565;
                case PixelFormat::RGB24:
                    return V4L2_PIX_FMT_RGB24;
                case PixelFormat::YUV422:
                    return V4L2_PIX_FMT_YUV422P;
                case PixelFormat::YUV420:
                    return V4L2_PIX_FMT_YUV420;
                case PixelFormat::JPEG:
                    return V4L2_PIX_FMT_JPEG;
                default:
                    return 0;
                }
            }

        } // namespace camera
    } // namespace media
} // namespace app
