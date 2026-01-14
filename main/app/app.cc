#include "app.hpp"

#include "assets/assets.hpp"
#include "config/config.hpp"
#include "media/audio/capture/capture.hpp"
#include "network/wifi/wifi.hpp"
#include "system/event/event.hpp"
#include "system/info/info.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

#include <sstream>
#include <cstdint>
#include <cstddef>

static const char* const TAG = "App";

namespace app
{

    bool App::setup()
    {
        if (!initNVS())
        {
            ESP_LOGE(TAG, "NVS 初始化失败");
            return false;
        }

        if (!initEvent())
        {
            ESP_LOGE(TAG, "事件系统初始化失败");
            return false;
        }

        if (!initI2C(app::config::I2C_SDA, app::config::I2C_SCL, I2C_NUM_1))
        {
            ESP_LOGE(TAG, "I2C 初始化失败");
            return false;
        }

        if (!initAssets())
        {
            ESP_LOGW(TAG, "Assets 初始化失败");
        }

        if (!initQMI8658A(getI2CBusHandle()))
        {
            ESP_LOGE(TAG, "QMI8658A 初始化失败");
            return false;
        }

        if (!initAudio(getI2CBusHandle(), 16000))
        {
            ESP_LOGE(TAG, "Audio 初始化失败");
            return false;
        }

        if (!initAfe())
        {
            ESP_LOGW(TAG, "AFE 初始化失败");
        }

        // 启动音频采集并连接 AFE
        if (!startAudioCapture())
        {
            ESP_LOGW(TAG, "启动音频采集失败");
        }

        if (!initProvision())
        {
            ESP_LOGE(TAG, "配网管理器初始化失败");
            return false;
        }

        if (!initChatbot("192.168.50.68", 8080, 5, 5, 10000))
        {
            ESP_LOGE(TAG, "Chatbot 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "应用初始化成功");
        return true;
    }

    void App::run()
    {
        auto& provision = app::network::ProvisionManager::getInstance();
        auto& wifi      = app::network::wifi::WiFiManager::getInstance();

        // wifi.removeCredentials("yf");

        if (!wifi.hasSavedCredentials())
        {
            ESP_LOGE(TAG, "没有保存的 WiFi 凭证");
            // 启动配网
            if (!provision.start())
            {
                ESP_LOGE(TAG, "启动配网失败");
                return;
            }
        }
        else
        {
            std::vector<app::network::wifi::Credentials> saved_creds;
            if (wifi.getCredentials(saved_creds) && !saved_creds.empty())
            {
                ESP_LOGI(TAG, "已保存的 WiFi 凭证数量: %u", (unsigned int)saved_creds.size());
                for (size_t i = 0; i < saved_creds.size(); i++)
                {
                    ESP_LOGI(TAG, "  [%u] SSID: %s", (unsigned int)i, saved_creds[i].ssid);
                }

                if (!provision.start())
                {
                    ESP_LOGE(TAG, "启动配网失败");
                    return;
                }
            }
        }

        while (true)
        {
            app::sys::task::TaskManager::delayMs(5000); // 5秒间隔

            // 打印系统信息
            // ESP_LOGI(TAG, "================= 系统信息 ===================");
            // logMemoryInfo();
            // logWiFiInfo();
            // logQMI8658AInfo();
            // ESP_LOGI(TAG, "==============================================");
        }
    }

    bool App::initNVS()
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        return true;
    }

    bool App::initEvent()
    {
        auto& event_mgr = app::sys::event::EventManager::getInstance();
        if (!event_mgr.init())
        {
            ESP_LOGE(TAG, "事件系统初始化失败");
            return false;
        }
        return true;
    }

    bool App::initAssets()
    {
        auto& assets = app::assets::Assets::getInstance();

        // 初始化 Assets
        if (!assets.init())
        {
            ESP_LOGE(TAG, "Assets 初始化失败");
            return false;
        }

        // 加载模型配置
        if (!assets.apply())
        {
            ESP_LOGE(TAG, "加载模型失败");
            return false;
        }

        // 打印模型列表
        srmodel_list_t* models = assets.getModelsList();
        if (models != nullptr && models->num > 0)
        {
            ESP_LOGI(TAG, "成功加载 %d 个模型:", models->num);
            for (int i = 0; i < models->num; i++)
            {
                ESP_LOGI(TAG, "  [%d] %s", i, models->model_name[i]);
            }
        }
        return true;
    }

    bool App::initI2C(gpio_num_t sda, gpio_num_t scl, i2c_port_t port)
    {
        i2c::Config cfg;
        cfg.sda_pin = sda;
        cfg.scl_pin = scl;
        cfg.port    = port;

        if (!i2c_.init(&cfg))
        {
            ESP_LOGE(TAG, "I2C 初始化失败");
            return false;
        }
        else
        {
            i2c_.scan(200);
        }
        return true;
    }

    i2c_master_bus_handle_t App::getI2CBusHandle() const
    {
        return i2c_.getBusHandle();
    }

    bool App::initQMI8658A(i2c_master_bus_handle_t i2c_handle)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }
        if (!qmi8658a_.init(i2c_handle, device::qmi8658a::QMI8658A_ADDR_LOW))
        {
            ESP_LOGE(TAG, "QMI8658A 初始化失败");
            return false;
        }
        return true;
    }

    device::qmi8658a::Qmi8658a& App::getQMI8658A()
    {
        return qmi8658a_;
    }

    bool App::initAudio(i2c_master_bus_handle_t i2c_handle, int sample_rate)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }

        if (sample_rate <= 0)
        {
            ESP_LOGE(TAG, "采样率无效: %d", sample_rate);
            return false;
        }

        media::audio::Config cfg;
        cfg.i2c_master_handle  = i2c_handle;
        cfg.input_sample_rate  = sample_rate;
        cfg.output_sample_rate = sample_rate;
        cfg.input_reference    = false;

        if (!audio_.init(&cfg))
        {
            ESP_LOGE(TAG, "Audio 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "Audio 初始化成功 (采样率=%dHz)", sample_rate);

        // 初始化 AudioCapture
        auto& capture = media::audio::capture::AudioCapture::getInstance();

        // 帧大小设置为 160（对应 10ms @ 16kHz）
        // 这个值需要与 AFE 等处理模块匹配
        const size_t frame_size = 160;

        if (!capture.init(&audio_, frame_size))
        {
            ESP_LOGE(TAG, "AudioCapture 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "AudioCapture 初始化成功");
        // ESP_LOGI(TAG, "  - 帧大小: %u 样本", (unsigned int)capture.getFrameSize());
        // ESP_LOGI(TAG, "  - 采样率: %d Hz", capture.getSampleRate());
        // ESP_LOGI(TAG, "  - 通道数: %d", capture.getChannels());
        // ESP_LOGI(TAG, "  注意: AudioCapture 已初始化，但尚未启动采集");

        return true;
    }

    bool App::initAfe()
    {
        // 获取 Assets 中的模型列表
        auto&           assets      = app::assets::Assets::getInstance();
        srmodel_list_t* models_list = nullptr;

        if (assets.isPartitionValid())
        {
            models_list = assets.getModelsList();
            if (models_list != nullptr && models_list->num > 0)
            {
                ESP_LOGI(TAG, "使用 Assets 加载的模型 (数量=%d)", models_list->num);
            }
            else
            {
                ESP_LOGW(TAG, "未加载模型，AFE 将使用默认模型");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Assets 分区无效，AFE 将使用默认模型");
        }

        // 配置 AFE
        // 注意：AFE_TYPE_VC 模式只支持单麦克风通道，多通道输入时只会选择第一个通道
        // 对于多麦输入，我们需要先做波束成形，转换为单声道
        media::audio::process::afe::Config afe_config;
        afe_config.input_format     = "M"; // 单麦克风
        afe_config.sample_rate      = 16000;
        afe_config.enable_aec       = false;              // 回声消除
        afe_config.enable_vad       = true;               // 人声检测
        afe_config.enable_ns        = false;              // 噪声抑制
        afe_config.enable_agc       = false;              // 自动增益控制
        afe_config.vad_mode         = VAD_MODE_0;         // 人声检测模式（0 最宽松，4 最严格）
        afe_config.vad_min_noise_ms = 100;                // 最小静音时长（毫秒）
        afe_config.afe_type         = AFE_TYPE_VC;        // 语音通信模式
        afe_config.afe_mode         = AFE_MODE_HIGH_PERF; // 高性能模式
        afe_config.models_list      = models_list;        // 模型列表

        // 创建 AFE 实例
        afe_ = std::make_unique<media::audio::process::afe::Afe>(afe_config);

        if (!afe_ || !afe_->isValid())
        {
            ESP_LOGE(TAG, "AFE 初始化失败");
            afe_.reset();
            return false;
        }

        ESP_LOGI(TAG, "AFE 初始化成功");
        // ESP_LOGI(TAG, "  - 输入帧大小: %u 样本", (unsigned int)afe_->getFeedSize());
        // ESP_LOGI(TAG, "  - 输出帧大小: %u 样本", (unsigned int)afe_->getFetchSize());
        // ESP_LOGI(TAG, "  - 通道数: %d", afe_->getChannelNum());
        // ESP_LOGI(TAG, "  - 采样率: %d Hz", afe_->getSampleRate());

        // 设置 AFE 的 VAD 状态回调
        afe_->setVadStateCallback(
            [](bool is_speaking)
            {
                // VAD 状态变化时的处理
                if (is_speaking)
                {
                    ESP_LOGI(TAG, "检测到语音");
                }
                // else
                // {
                //     ESP_LOGI(TAG, "检测到静音");
                // }
            });

        // 设置 AFE 的音频输出回调（处理后的音频）
        afe_->setAudioOutputCallback(
            [](const int16_t* data, size_t samples)
            {
                // 处理后的音频数据可以用于：
                // 1. 音频上传（OPUS 编码）
                // 2. 唤醒词检测
                // 3. 其他音频处理
                // 这里暂时不处理，后续可以在 handleListen 中启用上传
                (void)data;
                (void)samples;
            });

        return true;
    }

    bool App::startAudioCapture()
    {
        // 检查 AFE 是否已初始化
        if (!afe_ || !afe_->isValid())
        {
            ESP_LOGE(TAG, "AFE 未初始化，无法启动音频采集");
            return false;
        }

        // 获取 AudioCapture 实例
        auto& capture = media::audio::capture::AudioCapture::getInstance();

        // 检查 AudioCapture 是否已初始化
        if (capture.getSampleRate() == 0)
        {
            ESP_LOGE(TAG, "AudioCapture 未初始化，请先调用 initAudio()");
            return false;
        }

        // 如果已经启动，先停止
        if (capture.isCapturing())
        {
            ESP_LOGW(TAG, "音频采集已在运行，先停止");
            stopAudioCapture();
        }

        // 注册AFE的音频数据回调(不要在此函数中执行耗时操作，避免阻塞音频采集)
        afe_callback_id_ = capture.registerCallback(
            [this](const int16_t* data, size_t samples, int channels, int sample_rate)
            {
                if (!afe_ || !afe_->isValid())
                {
                    return;
                }

                // 获取 AFE 需要的输入帧大小
                static size_t afe_feed_size = 0;
                if (afe_feed_size == 0)
                {
                    afe_feed_size = afe_->getFeedSize();
                    if (afe_feed_size == 0)
                    {
                        return;
                    }
                    ESP_LOGI(TAG, "AFE 输入帧大小: %u 样本", (unsigned int)afe_feed_size);
                }

                // 波束成形：将多通道转换为单声道（AFE_TYPE_VC 只需要单声道输入）
                // 所有通道的平均值（简单波束成形）
                static int16_t mono_buffer[160]; // 单帧缓冲区
                const size_t   mono_samples = (samples < 160) ? samples : 160;

                if (channels > 1)
                {
                    // 多通道转单声道
                    for (size_t i = 0; i < mono_samples; i++)
                    {
                        int32_t sum = 0;
                        for (int ch = 0; ch < channels; ch++)
                        {
                            sum += static_cast<int32_t>(data[(i * channels) + ch]);
                        }
                        mono_buffer[i] = static_cast<int16_t>(sum / channels);
                    }
                }
                else
                {
                    // 已经是单声道，直接复制
                    for (size_t i = 0; i < mono_samples; i++)
                    {
                        mono_buffer[i] = data[i];
                    }
                }

                // 用于累积多帧数据，以满足 AFE 的输入要求
                static int16_t afe_buffer[512];    // AFE 最大输入帧大小
                static size_t  afe_buffer_pos = 0; // 当前累积位置

                // 将当前帧数据复制到累积缓冲区
                size_t copy_size = (afe_buffer_pos + mono_samples <= afe_feed_size)
                                       ? mono_samples
                                       : (afe_feed_size - afe_buffer_pos);
                for (size_t i = 0; i < copy_size; i++)
                {
                    afe_buffer[afe_buffer_pos + i] = mono_buffer[i];
                }
                afe_buffer_pos += copy_size;

                // 当累积到足够的数据时，输入到 AFE
                if (afe_buffer_pos >= afe_feed_size)
                {
                    // 输入到 AFE
                    if (afe_->feed(afe_buffer, afe_feed_size))
                    {
                        // 获取处理结果
                        afe_->fetch(0); // 0 表示非阻塞
                    }

                    // 如果还有剩余数据，保留在缓冲区中
                    if (afe_buffer_pos > afe_feed_size)
                    {
                        size_t remaining = afe_buffer_pos - afe_feed_size;
                        for (size_t i = 0; i < remaining; i++)
                        {
                            afe_buffer[i] = afe_buffer[afe_feed_size + i];
                        }
                        afe_buffer_pos = remaining;
                    }
                    else
                    {
                        afe_buffer_pos = 0;
                    }
                }
            });

        if (afe_callback_id_ < 0)
        {
            ESP_LOGE(TAG, "注册音频数据回调失败");
            return false;
        }

        ESP_LOGI(TAG, "音频数据回调已注册 (ID=%d)", afe_callback_id_);

        // 启动音频采集
        if (!capture.start())
        {
            ESP_LOGE(TAG, "启动音频采集失败");
            capture.unregisterCallback(afe_callback_id_);
            afe_callback_id_ = -1;
            return false;
        }

        ESP_LOGI(TAG, "音频采集已启动");
        return true;
    }

    void App::stopAudioCapture()
    {
        auto& capture = media::audio::capture::AudioCapture::getInstance();

        // 停止采集
        if (capture.isCapturing())
        {
            capture.stop();
            ESP_LOGI(TAG, "音频采集已停止");
        }

        // 取消注册回调
        if (afe_callback_id_ >= 0)
        {
            capture.unregisterCallback(afe_callback_id_);
            afe_callback_id_ = -1;
            ESP_LOGI(TAG, "音频数据回调已取消注册");
        }
    }

    media::audio::Audio& App::getAudio()
    {
        return audio_;
    }

    bool App::initProvision()
    {
        auto& provision = app::network::ProvisionManager::getInstance();
        if (!provision.init("EmotiPet"))
        {
            return false;
        }

        provision.setStatusCallback([this](app::network::ProvisionStatus status)
                                    { onProvisionStatus(status); });

        provision.setCompleteCallback([this](bool success, const char* ssid)
                                      { onProvisionComplete(success, ssid); });

        return true;
    }

    bool App::initChatbot(const std::string& server_host, int server_port, int ping_interval_sec,
                          int pingpong_timeout_sec, int reconnect_timeout_ms)
    {
        // 构建WebSocket URI
        std::ostringstream uri_stream;
        uri_stream << "ws://" << server_host << ":" << server_port;
        std::string server_uri = uri_stream.str();

        ESP_LOGI(TAG, "服务器地址: %s", server_uri.c_str());

        // 配置Chatbot
        chatbot::Chatbot::Config chatbot_config;
        chatbot_config.server_uri              = server_uri;
        chatbot_config.ping_interval_sec       = ping_interval_sec;
        chatbot_config.pingpong_timeout_sec    = pingpong_timeout_sec;
        chatbot_config.reconnect_timeout_ms    = reconnect_timeout_ms;
        chatbot_config.network_timeout_ms      = 10000; // 网络操作超时
        chatbot_config.disable_auto_reconnect  = false; // 启用自动重连
        chatbot_config.disable_pingpong_discon = true;  // 关闭自动心跳包

        // 初始化Chatbot
        if (!chatbot_.init(chatbot_config))
        {
            ESP_LOGE(TAG, "Chatbot 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "Chatbot 初始化成功");
        return true;
    }

    chatbot::Chatbot& App::getChatbot()
    {
        return chatbot_;
    }

    void App::onProvisionStatus(app::network::ProvisionStatus status)
    {
        // 只记录失败状态
        if (status == app::network::ProvisionStatus::FAILED_TIMEOUT ||
            status == app::network::ProvisionStatus::FAILED_WRONG_PWD ||
            status == app::network::ProvisionStatus::FAILED_NOT_FOUND ||
            status == app::network::ProvisionStatus::FAILED_UNKNOWN)
        {
            const char* status_str = "Unknown";
            switch (status)
            {
            case app::network::ProvisionStatus::FAILED_TIMEOUT:
                status_str = "超时";
                break;
            case app::network::ProvisionStatus::FAILED_WRONG_PWD:
                status_str = "密码错误";
                break;
            case app::network::ProvisionStatus::FAILED_NOT_FOUND:
                status_str = "网络未找到";
                break;
            case app::network::ProvisionStatus::FAILED_UNKNOWN:
                status_str = "未知错误";
                break;
            default:
                break;
            }
            ESP_LOGE(TAG, "配网失败: %s", status_str);
        }
    }

    void App::onProvisionComplete(bool success, const char* ssid)
    {
        if (!success)
        {
            ESP_LOGE(TAG, "配网失败: SSID=%s", ssid ? ssid : "未知");
        }

        ESP_LOGI(TAG, "配网成功: SSID=%s", ssid ? ssid : "未知");

        // WiFi连接成功后，连接WebSocket
        // if (!chatbot_.isConnected())
        // {
        //     ESP_LOGI(TAG, "正在连接WebSocket服务器...");
        //     if (chatbot_.connect())
        //     {
        //         ESP_LOGI(TAG, "WebSocket连接请求已发送");
        //     }
        //     else
        //     {
        //         ESP_LOGE(TAG, "WebSocket连接请求失败");
        //     }
        // }
    }

    void App::logMemoryInfo()
    {
        auto mem_info = app::sys::info::MemoryInfo::getMemoryInfo();
        auto cpu_info = app::sys::info::CpuInfo::getCpuInfo();

        ESP_LOGI(TAG, "CPU 频率: %lu MHz", cpu_info.getCpuFrequency() / 1000000);
        ESP_LOGI(TAG, "内部 SRAM: %u / %u KB (空闲/总量)",
                 (unsigned int)(mem_info.getSramFree() / 1024),
                 (unsigned int)(mem_info.getSramTotal() / 1024));
        ESP_LOGI(TAG, "PSRAM: %u / %u KB (空闲/总量)",
                 (unsigned int)(mem_info.getPsramFree() / 1024),
                 (unsigned int)(mem_info.getPsramTotal() / 1024));
    }

    void App::logWiFiInfo()
    {
        auto& provision = app::network::ProvisionManager::getInstance();
        auto  status    = provision.getStatus();

        if (status == app::network::ProvisionStatus::CONNECTED)
        {
            std::string ssid = provision.getCurrentSsid();
            std::string ip   = provision.getCurrentIp();
            ESP_LOGI(TAG, "WiFi SSID: %s, IP: %s", ssid.c_str(), ip.c_str());
        }
    }

    void App::logQMI8658AInfo()
    {
        device::qmi8658a::SensorData data;
        // 读取传感器数据并计算姿态角
        if (qmi8658a_.read(data, device::qmi8658a::READ_ALL))
        {
            ESP_LOGI(TAG, "QMI8658A 加速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f m/s²", data.accel_x,
                     data.accel_y, data.accel_z);
            ESP_LOGI(TAG, "QMI8658A 角速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f rad/s", data.gyro_x,
                     data.gyro_y, data.gyro_z);
            ESP_LOGI(TAG, "QMI8658A 姿态:   Roll=%+7.1f°  Pitch=%+7.1f°  Yaw=%+7.1f°", data.angle_x,
                     data.angle_y, data.angle_z);
        }
    }

} // namespace app