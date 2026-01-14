#pragma once

#include "assets/assets.hpp"
#include "chatbot/chatbot.hpp"
#include "i2c/i2c.hpp"
#include "device/qmi8658a/qmi8658a.hpp"
#include "media/audio/audio.hpp"
#include "media/audio/process/afe/afe.hpp"
#include "network/network.hpp"
#include <memory>
#include <string>

namespace app
{
    class App
    {
    public:
        App()  = default;
        ~App() = default;

        bool setup();
        void run();

        // 获取 I2C 总线句柄
        i2c_master_bus_handle_t getI2CBusHandle() const;

        // 获取 QMI8658A 实例
        device::qmi8658a::Qmi8658a& getQMI8658A();

        // 获取 Audio 实例
        media::audio::Audio& getAudio();

        // 获取 Chatbot 实例
        chatbot::Chatbot& getChatbot();

        // 获取 AFE 实例
        media::audio::process::afe::Afe* getAfe()
        {
            return afe_.get();
        }

    private:
        // 初始化 NVS
        bool initNVS();

        // 初始化事件系统
        bool initEvent();

        // 初始化 Assets
        bool initAssets();

        // 初始化 I2C
        bool initI2C(gpio_num_t sda, gpio_num_t scl, i2c_port_t port);

        // 初始化QMI8658A
        bool initQMI8658A(i2c_master_bus_handle_t i2c_handle);

        // 初始化音频
        bool initAudio(i2c_master_bus_handle_t i2c_handle, int sample_rate = 16000);

        // 初始化 AFE
        bool initAfe();

        // 启动音频采集并连接 AFE
        bool startAudioCapture();

        // 停止音频采集
        void stopAudioCapture();

        // 初始化配网管理器
        bool initProvision();

        /**
         * @brief 初始化Chatbot
         * @param server_host 服务器主机地址（IP或域名），如 "192.168.1.100" 或 "example.com"
         * @param server_port 服务器端口，如 8080
         * @param ping_interval_sec Ping间隔（秒），默认10秒
         * @param pingpong_timeout_sec Pong超时（秒），默认10秒
         * @param reconnect_timeout_ms 重连超时（毫秒），默认10000毫秒
         * @return 是否成功
         */
        bool initChatbot(const std::string& server_host, int server_port,
                         int ping_interval_sec = 10, int pingpong_timeout_sec = 10,
                         int reconnect_timeout_ms = 10000);

        // 配网状态回调
        void onProvisionStatus(app::network::ProvisionStatus status);

        // 配网完成回调
        void onProvisionComplete(bool success, const char* ssid);

        // 打印内存信息
        void logMemoryInfo();

        // 打印 WiFi 信息
        void logWiFiInfo();

        // 打印 QMI8658A 信息
        void logQMI8658AInfo();

        // 成员变量
        i2c::I2c                                    i2c_;
        device::qmi8658a::Qmi8658a                  qmi8658a_;
        media::audio::Audio                         audio_;
        std::unique_ptr<media::audio::process::afe::Afe> afe_;
        chatbot::Chatbot                            chatbot_;
        
        // 音频采集相关
        int afe_callback_id_ = -1;  // AudioCapture 回调 ID
    };

} // namespace app
