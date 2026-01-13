#include "app.hpp"

#include "config/config.hpp"
#include "network/wifi/wifi.hpp"
#include "system/event/event.hpp"
#include "system/info/info.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

#include <sstream>

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
        if (!assets.init())
        {
            ESP_LOGE(TAG, "Assets 初始化失败");
            return false;
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
        return true;
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
        if (!chatbot_.isConnected())
        {
            ESP_LOGI(TAG, "正在连接WebSocket服务器...");
            if (chatbot_.connect())
            {
                ESP_LOGI(TAG, "WebSocket连接请求已发送");
            }
            else
            {
                ESP_LOGE(TAG, "WebSocket连接请求失败");
            }
        }
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