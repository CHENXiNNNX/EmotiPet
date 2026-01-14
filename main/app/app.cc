#include "app.hpp"

#include "assets/assets.hpp"
#include "config/config.hpp"
#include "media/audio/capture/capture.hpp"
#include "network/wifi/wifi.hpp"
#include "system/event/event.hpp"
#include "system/info/info.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "logic/logic.h"
#include "device/led/led.hpp"

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
            return false;
        }

        // 初始化 Audio
        if (!initAudio(getI2CBusHandle(), 16000))
        {
            ESP_LOGW(TAG, "Audio 初始化失败");
            return false;
        }

        if (!initQMI8658A(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "QMI8658A 初始化失败");
        }

        // 初始化 APDS-9930 传感器
        if (!initAPDS9930(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "APDS-9930 初始化失败");
        }

        // 初始化 MPR121 触摸传感器
        if (!initMPR121(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "MPR121 触摸传感器初始化失败");
        }

        // 初始化 M0404 压力传感器
        if (!initM0404(UART_NUM_2, GPIO_NUM_7, GPIO_NUM_15, 115200))
        {
            ESP_LOGW(TAG, "M0404 压力传感器初始化失败");
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

        // 启动 APDS-9930 传感器数据获取
        if (apds9930_.isInitialized())
        {
            // 设置环境光状态回调函数
            // 当环境光 >= 1500 lux 时，light_status = 1（亮）
            // 当环境光 < 1500 lux 时，light_status = 0（灭）
            apds9930_.setLightStatusCallback(
                [](int light_status)
                {
                    const char* status_str = (light_status == 1) ? "亮" : "灭";
                    ESP_LOGI(TAG, "环境光状态回调: %d (%s)", light_status, status_str);
                    // 通过以下方式获取当前光状态：
                    // int status = apds9930_.getCurrentLightStatus();
                });

            // 启动传感器数据获取
            if (apds9930_.start())
            {
                ESP_LOGI(TAG, "APDS-9930 传感器数据获取已开启");
                // 启动后台数据采集任务（每5秒采集一次）
                if (apds9930_.startDataCollection(5000))
                {
                    ESP_LOGI(TAG, "APDS-9930 数据采集任务已启动");
                }
                else
                {
                    ESP_LOGW(TAG, "APDS-9930 数据采集任务启动失败");
                }
            }
            else
            {
                ESP_LOGW(TAG, "APDS-9930 启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "APDS-9930 未初始化，跳过启动");
        }

        // 启动 M0404 压力传感器数据采集
        if (m0404_.isInitialized())
        {
            // 设置压力状态回调函数
            // 当有压力时，pressure_status = 1（有压力）
            // 当无压力时，pressure_status = 0（无压力）
            m0404_.setPressureStatusCallback(
                [](int pressure_status)
                {
                    // 只在有压力时才输出日志
                    // if (pressure_status == 1)
                    // {
                    //     ESP_LOGI(TAG, "压力状态回调: %d (有压力)", pressure_status);
                    // }
                    // 通过以下方式获取当前压力状态：
                    // int status = m0404_.getCurrentPressureStatus();
                });

            // 启动后台数据采集任务（每10秒采集一次）
            if (m0404_.startDataCollection(10000))
            {
                ESP_LOGI(TAG, "M0404 压力传感器数据采集任务已启动");
            }
            else
            {
                ESP_LOGW(TAG, "M0404 压力传感器数据采集任务启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "M0404 压力传感器未初始化，跳过启动");
        }

        // 启动 MPR121 触摸传感器数据采集
        if (mpr121_.isInitialized())
        {
            // 设置触摸状态回调函数
            // 当有触摸时，touch_status = 1（触摸）
            // 当未触摸时，touch_status = 0（未触摸）
            mpr121_.setTouchStatusCallback(
                [](int touch_status)
                {
                    // 只在触摸时才输出日志
                    // if (touch_status == 1)
                    // {
                    //     ESP_LOGI(TAG, "触摸状态回调: %d (触摸)", touch_status);
                    // }
                    // 通过以下方式获取当前触摸状态：
                    // int status = mpr121_.getCurrentTouchStatus();
                });

            // 启动后台数据采集任务（每100ms采集一次）
            if (mpr121_.startDataCollection(100))
            {
                ESP_LOGI(TAG, "MPR121 触摸传感器数据采集任务已启动");
            }
            else
            {
                ESP_LOGW(TAG, "MPR121 触摸传感器数据采集任务启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "MPR121 触摸传感器未初始化，跳过启动");
        }

        // 启动 QMI8658A 陀螺仪数据采集
        if (qmi8658a_.isInitialized())
        {
            // 设置运动状态回调函数
            // 当加速度变化很大时，motion_status = 1（动了）
            // 当加速度没变化时，motion_status = 0（没动）
            qmi8658a_.setMotionStatusCallback(
                [](int motion_status)
                {
                    // 只在"动了"时才输出日志
                    // if (motion_status == 1)
                    //{
                    //    ESP_LOGI(TAG, "运动状态回调: %d (动了)", motion_status);
                    //}
                    // 通过以下方式获取当前运动状态：
                    // int status = qmi8658a_.getCurrentMotionStatus();
                });

            // 启动后台数据采集任务（每100ms采集一次）
            if (qmi8658a_.startDataCollection(100))
            {
                ESP_LOGI(TAG, "QMI8658A 陀螺仪数据采集任务已启动");
            }
            else
            {
                ESP_LOGW(TAG, "QMI8658A 陀螺仪数据采集任务启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "QMI8658A 陀螺仪未初始化，跳过启动");
        }

        // 初始化配置和变量
        logic_config_t config        = initLogicConfig();
        static int     s_zero_streak = 0;

        // 初始化呼吸灯
        breathingLED();

        while (true)
        {
            app::sys::task::TaskManager::delayMs(5000); // 5秒间隔

            // 更新呼吸灯颜色（每次使用下一个颜色）
            updateBreathingLEDColor();

            // 打印系统信息
            ESP_LOGI(TAG, "================= 系统信息 ===================");
            // logMemoryInfo();
            // logWiFiInfo();
            // logQMI8658AInfo();
            calculateControl(mpr121_.getCurrentTouchStatus(), m0404_.getCurrentPressureStatus(),
                             qmi8658a_.getCurrentMotionStatus(), apds9930_.getCurrentLightStatus(),isSpeaking(),
                             config, s_zero_streak, TAG);
            ESP_LOGI(TAG, "==============================================");
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
        // 对于多麦输入，需要先做波束成形，转换为单声道
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
            [this](bool is_speaking)
            {
                // 更新 VAD 状态
                is_speaking_.store(is_speaking, std::memory_order_release);

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
                static size_t s_afe_feed_size = 0;
                if (s_afe_feed_size == 0)
                {
                    s_afe_feed_size = afe_->getFeedSize();
                    if (s_afe_feed_size == 0)
                    {
                        return;
                    }
                    ESP_LOGI(TAG, "AFE 输入帧大小: %u 样本", (unsigned int)s_afe_feed_size);
                }

                // 波束成形：将多通道转换为单声道（AFE_TYPE_VC 只需要单声道输入）
                // 所有通道的平均值（简单波束成形）
                static int16_t s_mono_buffer[160]; // 单帧缓冲区
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
                        s_mono_buffer[i] = static_cast<int16_t>(sum / channels);
                    }
                }
                else
                {
                    // 已经是单声道，直接复制
                    for (size_t i = 0; i < mono_samples; i++)
                    {
                        s_mono_buffer[i] = data[i];
                    }
                }

                // 用于累积多帧数据，以满足 AFE 的输入要求
                static int16_t s_afe_buffer[512];    // AFE 最大输入帧大小
                static size_t  s_afe_buffer_pos = 0; // 当前累积位置

                // 将当前帧数据复制到累积缓冲区
                size_t copy_size = (s_afe_buffer_pos + mono_samples <= s_afe_feed_size)
                                       ? mono_samples
                                       : (s_afe_feed_size - s_afe_buffer_pos);
                for (size_t i = 0; i < copy_size; i++)
                {
                    s_afe_buffer[s_afe_buffer_pos + i] = s_mono_buffer[i];
                }
                s_afe_buffer_pos += copy_size;

                // 当累积到足够的数据时，输入到 AFE
                if (s_afe_buffer_pos >= s_afe_feed_size)
                {
                    // 输入到 AFE
                    if (afe_->feed(s_afe_buffer, s_afe_feed_size))
                    {
                        // 获取处理结果
                        afe_->fetch(0); // 0 表示非阻塞
                    }

                    // 如果还有剩余数据，保留在缓冲区中
                    if (s_afe_buffer_pos > s_afe_feed_size)
                    {
                        size_t remaining = s_afe_buffer_pos - s_afe_feed_size;
                        for (size_t i = 0; i < remaining; i++)
                        {
                            s_afe_buffer[i] = s_afe_buffer[s_afe_feed_size + i];
                        }
                        s_afe_buffer_pos = remaining;
                    }
                    else
                    {
                        s_afe_buffer_pos = 0;
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

    bool App::initAPDS9930(i2c_master_bus_handle_t i2c_handle)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }
        if (!apds9930_.init(i2c_handle, device::apds9930::APDS9930_I2C_ADDR))
        {
            ESP_LOGE(TAG, "APDS-9930 初始化失败");
            return false;
        }
        return true;
    }

    bool App::initMPR121(i2c_master_bus_handle_t i2c_handle)
    {
        if (i2c_handle == nullptr)
        {
            ESP_LOGE(TAG, "I2C 总线句柄为空");
            return false;
        }

        if (!mpr121_.init(i2c_handle, device::mpr121::MPR121_I2C_ADDR, GPIO_NUM_NC))
        {
            ESP_LOGE(TAG, "MPR121 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "MPR121 初始化成功");
        return true;
    }

    bool App::isMPR121Initialized() const
    {
        return mpr121_.isInitialized();
    }

    bool App::initM0404(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate)
    {
        if (!m0404_.init(uart_num, tx_pin, rx_pin, baud_rate))
        {
            ESP_LOGE(TAG, "M0404 初始化失败");
            return false;
        }
        return true;
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
        chatbot_config.disable_auto_reconnect  = false; // 自动重连
        chatbot_config.disable_pingpong_discon = true;  // 心跳包

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

    void App::blinkLED()
    {
        // 定义颜色数组
        static const app::device::led::Color s_color_sequence[] = {
            app::device::led::Color(255, 0, 0),     // 红色
            app::device::led::Color(255, 127, 0),   // 橙色
            app::device::led::Color(255, 255, 0),   // 黄色
            app::device::led::Color(0, 255, 0),     // 绿色
            app::device::led::Color(0, 0, 255),     // 蓝色
            app::device::led::Color(75, 0, 130),    // 靛蓝色
            app::device::led::Color(148, 0, 211),   // 紫色
            app::device::led::Color(255, 255, 255), // 白色
        };
        static const size_t s_color_count = sizeof(s_color_sequence) / sizeof(s_color_sequence[0]);
        static size_t       s_color_index = 0;

        // 每5秒亮一次，每次使用不同颜色
        app::device::led::Color current_color = s_color_sequence[s_color_index];
        app::device::led::Color colors[2]     = {
            current_color, // 第一个LED
            current_color  // 第二个LED
        };
        led_.setColors(app::config::LED_GPIO, colors, 2);
        app::sys::task::TaskManager::delayMs(1000); // 亮1000ms
        // 熄灭两个LED
        colors[0] = app::device::led::Color(0, 0, 0);
        colors[1] = app::device::led::Color(0, 0, 0);
        led_.setColors(app::config::LED_GPIO, colors, 2);

        // 切换到下一个颜色
        s_color_index = (s_color_index + 1) % s_color_count;
    }

    void App::breathingLED()
    {
        // 定义颜色数组
        static const app::device::led::Color s_color_sequence[] = {
            app::device::led::Color(255, 0, 0),     // 红色
            app::device::led::Color(255, 127, 0),   // 橙色
            app::device::led::Color(255, 255, 0),   // 黄色
            app::device::led::Color(0, 255, 0),     // 绿色
            app::device::led::Color(0, 0, 255),     // 蓝色
            app::device::led::Color(75, 0, 130),    // 靛蓝色
            app::device::led::Color(148, 0, 211),   // 紫色
            app::device::led::Color(255, 255, 255), // 白色
        };

        // 启动呼吸灯
        led_.startBreathing(app::config::LED_GPIO, s_color_sequence[0], 2000, 2);
    }

    void App::updateBreathingLEDColor()
    {
        // 定义颜色数组
        static const app::device::led::Color s_color_sequence[] = {
            app::device::led::Color(255, 0, 0),     // 红色
            app::device::led::Color(255, 127, 0),   // 橙色
            app::device::led::Color(255, 255, 0),   // 黄色
            app::device::led::Color(0, 255, 0),     // 绿色
            app::device::led::Color(0, 0, 255),     // 蓝色
            app::device::led::Color(75, 0, 130),    // 靛蓝色
            app::device::led::Color(148, 0, 211),   // 紫色
            app::device::led::Color(255, 255, 255), // 白色
        };
        static const size_t s_color_count = sizeof(s_color_sequence) / sizeof(s_color_sequence[0]);
        static size_t       s_color_index = 0;

        // 切换到下一个颜色
        s_color_index = (s_color_index + 1) % s_color_count;

        // 更新呼吸灯颜色
        led_.updateBreathingColor(s_color_sequence[s_color_index]);
    }

} // namespace app