#include "camera.hpp"
#include "esp_log.h"
#include "esp_cam_ctlr_dvp.h"
#include "esp_video_init.h"
#include "esp_video_device.h"
#include "linux/videodev2.h"
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>

static const char* const TAG = "Camera";

namespace app
{
    namespace media
    {
        namespace camera
        {
            Camera::Camera() : initialized_(false), device_fd_(-1)
            {
            }

            Camera::~Camera()
            {
                if (initialized_)
                {
                    if (device_fd_ >= 0)
                    {
                        close(device_fd_);
                        device_fd_ = -1;
                    }

                    esp_video_deinit();
                    initialized_ = false;
                }
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
                dvp_config.sccb_config.init_sccb             = false; // 不初始化 I2C，使用已有的
                dvp_config.sccb_config.i2c_handle            = config_.i2c_master_handle;
                dvp_config.sccb_config.freq                  = config_.sccb_freq;

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
                struct v4l2_format fmt = {};
                fmt.type                = V4L2_BUF_TYPE_VIDEO_CAPTURE;
                fmt.fmt.pix.width       = config_.frame_width;
                fmt.fmt.pix.height      = config_.frame_height;
                fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_JPEG; 

                if (ioctl(device_fd_, VIDIOC_S_FMT, &fmt) != 0)
                {
                    ESP_LOGW(TAG, "设置图像格式失败，将使用默认格式");
                }
                else
                {
                    ESP_LOGI(TAG, "图像格式设置成功: %dx%d", fmt.fmt.pix.width, fmt.fmt.pix.height);
                }

                initialized_ = true;
                ESP_LOGI(TAG, "摄像头初始化成功");
                return true;
            }

        } // namespace camera
    }     // namespace media
} // namespace app

