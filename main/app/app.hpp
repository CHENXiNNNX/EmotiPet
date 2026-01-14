#pragma once

#include "assets/assets.hpp"
#include "chatbot/chatbot.hpp"
#include "i2c/i2c.hpp"
#include "device/qmi8658a/qmi8658a.hpp"
#include "device/APDS-9930/apds9930.hpp"
#include "device/mpr121/mpr121.hpp"
#include "device/pressure/m0404.hpp"
#include "device/led/led.hpp"
#include "media/audio/audio.hpp"
#include "media/audio/process/afe/afe.hpp"
#include "network/network.hpp"
#include <atomic>
#include <memory>
#include <string>

namespace app
{
    class App
    {
    public:
        App()
            : apds9930_(device::apds9930::APDS9930::getInstance()),
              mpr121_(device::mpr121::MPR121::getInstance()),
              m0404_(device::pressure::M0404::getInstance())
        {
        }
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

        // 获取 APDS-9930 实例
        device::apds9930::APDS9930& getAPDS9930();

        /**
         * @brief 检查当前是否检测到语音
         * @return true 如果检测到语音，false 如果静音
         */
        bool isSpeaking() const
        {
            return is_speaking_.load(std::memory_order_acquire);
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

        // 初始化 APDS-9930
        bool initAPDS9930(i2c_master_bus_handle_t i2c_handle);

        // 开启 APDS-9930 传感器数据获取
        bool startAPDS9930(bool light_interrupts = false, bool proximity_interrupts = false);

        // 关闭 APDS-9930 传感器数据获取
        bool stopAPDS9930();

        // 初始化 MPR121 触摸传感器
        bool initMPR121(i2c_master_bus_handle_t i2c_handle);

        // 获取 MPR121 是否已初始化
        bool isMPR121Initialized() const;

        // 初始化 M0404 压力传感器
        bool initM0404(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin,
                       int baud_rate = 115200);

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

        // LED闪烁（每次使用下一个颜色）
        void blinkLED();

        // 初始化呼吸灯（启动呼吸灯效果）
        void breathingLED();

        // 更新呼吸灯颜色（每次使用下一个颜色）
        void updateBreathingLEDColor();

        // 成员变量
        i2c::I2c                                         i2c_;
        device::qmi8658a::Qmi8658a                       qmi8658a_;
        media::audio::Audio                              audio_;
        std::unique_ptr<media::audio::process::afe::Afe> afe_;
        chatbot::Chatbot                                 chatbot_;

        device::apds9930::APDS9930& apds9930_;
        device::mpr121::MPR121&     mpr121_;
        device::pressure::M0404&    m0404_;
        device::led::WS2812         led_;

        // 音频采集相关
        int afe_callback_id_ = -1; // AudioCapture 回调 ID

        // VAD 状态
        std::atomic<bool> is_speaking_{false};
    };

} // namespace app
