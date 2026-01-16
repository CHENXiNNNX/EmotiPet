#pragma once

#include "assets/assets.hpp"
#include "chatbot/chatbot.hpp"
#include "chatbot/handle/sender.hpp"
#include "chatbot/handle/receiver.hpp"
#include "i2c/i2c.hpp"
#include "device/qmi8658a/qmi8658a.hpp"
#include "device/apds9930/apds9930.hpp"
#include "device/mpr121/mpr121.hpp"
#include "device/m0404/m0404.hpp"
#include "device/led/led.hpp"
#include "media/audio/audio.hpp"
#include "media/audio/process/afe/afe.hpp"
#include "media/audio/wakeword/wakeword.hpp"
#include "media/camera/camera.hpp"
#include "network/network.hpp"
#include "logic/logic.h"
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
              m0404_(device::m0404::M0404::getInstance())
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

        // 初始化 APDS-9930
        bool initAPDS9930(i2c_master_bus_handle_t i2c_handle);

        // 初始化 MPR121 触摸传感器
        bool initMPR121(i2c_master_bus_handle_t i2c_handle);

        // 初始化 M0404 压力传感器
        bool initM0404(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin,
                       int baud_rate = 115200);

        // 初始化配网管理器
        bool initProvision();

        // 初始化 NTP
        bool initNTP();

        // 初始化音频
        bool initAudio(i2c_master_bus_handle_t i2c_handle, int sample_rate = 16000);

        // 初始化 AFE
        bool initAfe();

        // 初始化摄像头
        bool initCamera(i2c_master_bus_handle_t i2c_handle);

        // 初始化唤醒词检测
        bool initWakeWord();

        // 启动唤醒词检测
        bool startWakeWord();

        // 停止唤醒词检测
        void stopWakeWord();

        // 启动音频采集
        bool startAudioCapture();

        // 停止音频采集
        void stopAudioCapture();

        /**
         * @brief 初始化Chatbot
         * @param server_host 服务器主机地址（IP或域名），如 "192.168.1.100" 或 "example.com"
         * @param server_port 服务器端口，如 8080
         * @param ping_interval_sec Ping间隔（秒），默认10秒
         * @param pingpong_timeout_sec Pong超时（秒），默认10秒
         * @param reconnect_timeout_ms 重连超时（毫秒），默认10000毫秒
         * @param path WebSocket路径（可选），例如 "/ws/device/{MAC}"，{MAC}会被替换为实际MAC地址
         * @return 是否成功
         */
        bool initChatbot(const std::string& server_host, int server_port,
                         int ping_interval_sec = 10, int pingpong_timeout_sec = 10,
                         int reconnect_timeout_ms = 10000, const std::string& path = "");

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

        // 打印 MPR121 信息
        void logMPR121Info();

        // 打印 M0404 信息
        void logM0404Info();

        // 打印 APDS-9930 信息
        void logAPDS9930Info();

        /**
         * @brief 采集并发送传感器数据
         * @param command_str 5位二进制字符串命令（例如"11110"），由control值转换而来
         */
        void collectAndSendSensorData(const std::string& command_str);

        /**
         * @brief 捕获并发送图片数据
         * @return true=成功, false=失败
         */
        bool captureAndSendImage();

        /**
         * @brief 计算控制位并转换为二进制字符串
         * @param config 逻辑配置
         * @param zero_streak 零值连续计数（会被修改）
         * @return 5位二进制字符串命令（例如"11110"）
         */
        std::string calculateAndConvertControl(const logic_config_t& config, int& zero_streak);

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
        media::camera::Camera                            camera_;
        std::unique_ptr<media::audio::process::afe::Afe> afe_;
        chatbot::Chatbot                                 chatbot_;
        chatbot::handle::MessageSender                   message_sender_;   // 消息发送管理器
        chatbot::handle::MessageReceiver                 message_receiver_; // 消息接收管理器

        device::apds9930::APDS9930& apds9930_;
        device::mpr121::MPR121&     mpr121_;
        device::m0404::M0404&       m0404_;
        device::led::WS2812         led_;

        // 音频采集相关
        int afe_callback_id_      = -1; // AudioCapture 回调 ID（用于 AFE）
        int wakeword_callback_id_ = -1; // AudioCapture 回调 ID（用于唤醒词）

        // VAD 状态
        std::atomic<bool> is_speaking_{false};

        // 唤醒词检测
        std::unique_ptr<media::audio::wakeword::WakeWord> wakeword_;
    };

} // namespace app
