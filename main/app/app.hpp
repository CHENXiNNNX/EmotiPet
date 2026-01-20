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
#include "media/audio/process/opus/encode/opus_enc.hpp"
#include "media/audio/wakeword/wakeword.hpp"
#include "media/camera/camera.hpp"
#include "network/network.hpp"
#include "logic/logic.h"
#include <atomic>
#include <memory>
#include <string>

namespace app
{
    /**
     * @brief 设备状态枚举
     */
    enum class DeviceState
    {
        INIT,          // 设备初始化
        PROVISIONING,  // 配网中
        NTP_SYNC,      // NTP时间同步
        CONNECTING,    // WebSocket连接中
        WAKEWORD_WAIT, // 等待唤醒词
        RUNNING,       // 正常运行
        ERROR,         // 错误状态
        RECOVERY       // 恢复中
    };

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

        /**
         * @brief 初始化设备（只做基础初始化）
         * @return true=成功, false=失败
         */
        bool setup();

        /**
         * @brief 主运行循环（状态机驱动）
         */
        void run();

        /**
         * @brief 获取 I2C 总线句柄
         * @return I2C 总线句柄
         */
        i2c_master_bus_handle_t getI2CBusHandle() const;

    private:
        // ==================== 状态管理 ====================
        DeviceState current_state_{DeviceState::INIT}; // 当前状态
        int         retry_count_{0};                   // 重试计数
        int64_t     state_start_time_{0};              // 状态开始时间（微秒）
        int         max_retries_{5};                   // 最大重试次数

        /**
         * @brief 设置设备状态
         * @param new_state 新状态
         */
        void setState(DeviceState new_state);

        /**
         * @brief 获取当前状态
         */
        DeviceState getState() const
        {
            return current_state_;
        }

        /**
         * @brief 检查当前状态是否超时
         * @param timeout_ms 超时时间（毫秒）
         * @return true=超时, false=未超时
         */
        bool isStateTimeout(int timeout_ms) const;

        /**
         * @brief 重置重试计数
         */
        void resetRetryCount()
        {
            retry_count_ = 0;
        }

        // ==================== 状态处理函数 ====================
        void handleInitState();         // 处理初始化状态
        void handleProvisioningState(); // 处理配网状态
        void handleNtpSyncState();      // 处理NTP同步状态
        void handleConnectingState();   // 处理WebSocket连接状态
        void handleWakewordWaitState(); // 处理唤醒词等待状态
        void handleRunningState();      // 处理正常运行状态
        void handleErrorState();        // 处理错误状态
        void handleRecoveryState();     // 处理恢复状态

        // ==================== 初始化方法 ====================
        bool initNVS();                                                // 初始化NVS
        bool initI2C(gpio_num_t sda, gpio_num_t scl, i2c_port_t port); // 初始化I2C
        bool initAssets();                                             // 初始化Assets
        bool initEvent();                                              // 初始化事件系统
        bool initQMI8658A(i2c_master_bus_handle_t i2c_handle);         // 初始化QMI8658A
        bool initAPDS9930(i2c_master_bus_handle_t i2c_handle);         // 初始化 APDS-9930
        bool initMPR121(i2c_master_bus_handle_t i2c_handle);           // 初始化 MPR121 触摸传感器
        bool initM0404(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin,
                       int baud_rate = 115200); // 初始化 M0404 压力传感器
        bool initProvision();                   // 初始化配网管理器
        bool initNTP();                         // 初始化NTP管理器
        bool
             initChatbot(const std::string& server_host, int server_port = -1,
                         int ping_interval_sec = 10, int pingpong_timeout_sec = 10,
                         int reconnect_timeout_ms = 10000, const std::string& path = "",
                         const std::string& protocol = "ws"); // 初始化Chatbot (protocol: "ws" or "wss")
        bool initAudio(i2c_master_bus_handle_t i2c_handle,
                       int                     sample_rate = 16000);             // 初始化音频
        bool initOpusEnc();                                  // 初始化 Opus 编码器
        bool initAfe();                                      // 初始化AFE（音频前端处理）
        bool initCamera(i2c_master_bus_handle_t i2c_handle); // 初始化摄像头
        bool startAudioCapture();                            // 启动音频采集
        void stopAudioCapture();                             // 停止音频采集
        bool initWakeWord();                                 // 初始化唤醒词检测
        bool startWakeWord();                                // 启动唤醒词检测
        void stopWakeWord();                                 // 停止唤醒词检测

        // ==================== 数据上传 ====================
        void collectAndSendSensorData(const std::string& command_str); // 采集并发送传感器数据
        std::string calculateAndConvertControl(const logic_config_t& config,
                                               int& zero_streak); // 计算控制位并转换
        bool        captureAndSendImage();                        // 捕获并发送图片

        // ==================== 消息处理 ====================
        void setupMessageHandlers(); // 设置消息处理函数
        void sendListenMessage();    // 发送listen消息

        // 消息处理函数
        void handleRecvInfoMessage(const chatbot::message::RecvInfoMessage& msg);
        void handleMovInfoMessage(const chatbot::message::MovInfoMessage& msg);
        void handlePlayMessage(const chatbot::message::PlayMessage& msg);
        void handleEmotionMessage(const chatbot::message::EmotionMessage& msg);
        void handleErrorMessage(const chatbot::message::ErrorMessage& msg);

        /**
         * @brief 检查当前是否检测到语音
         * @return true 如果检测到语音，false 如果静音
         */
        bool isSpeaking() const
        {
            return is_speaking_.load(std::memory_order_acquire);
        }

        // ==================== 日志打印 ====================
        /**
         * @brief 打印所有系统信息
         */
        void logSystemInfo();

        void logMemoryInfo();   // 打印内存信息
        void logWiFiInfo();     // 打印 WiFi 信息
        void logQMI8658AInfo(); // 打印 QMI8658A 信息
        void logAPDS9930Info(); // 打印 APDS-9930 信息
        void logMPR121Info();   // 打印 MPR121 信息
        void logM0404Info();    // 打印 M0404 信息

        // ==================== 回调方法 ====================
        void onProvisionStatus(app::network::ProvisionStatus status); // 配网状态回调
        void onProvisionComplete(bool success, const char* ssid);     // 配网完成回调

        // ==================== 成员变量 ====================
        i2c::I2c                                                          i2c_;
        device::qmi8658a::Qmi8658a                                        qmi8658a_;
        media::audio::Audio                                               audio_;
        media::camera::Camera                                             camera_;
        std::unique_ptr<media::audio::process::afe::Afe>                  afe_;
        std::unique_ptr<media::audio::process::opus::encode::OpusEncoder> opus_encoder_;
        chatbot::Chatbot                                                  chatbot_;
        chatbot::handle::MessageSender                                    message_sender_;
        chatbot::handle::MessageReceiver                                  message_receiver_;

        device::apds9930::APDS9930& apds9930_;
        device::mpr121::MPR121&     mpr121_;
        device::m0404::M0404&       m0404_;
        device::led::WS2812         led_;

        // 音频采集相关
        int afe_callback_id_{-1};      // AudioCapture 回调 ID（用于 AFE）
        int wakeword_callback_id_{-1}; // AudioCapture 回调 ID（用于唤醒词）

        // VAD 状态
        std::atomic<bool> is_speaking_{false};

        // 唤醒词检测
        std::unique_ptr<media::audio::wakeword::WakeWord> wakeword_;

        // 配网状态
        bool provision_completed_{false}; // 配网是否完成
        bool provision_success_{false};   // 配网是否成功

        // NTP同步状态
        bool ntp_sync_completed_{false}; // NTP同步是否完成
        bool ntp_sync_success_{false};   // NTP同步是否成功

        // WebSocket连接状态
        bool    chatbot_initialized_{false};   // Chatbot是否已初始化
        int64_t last_connect_attempt_time_{0}; // 上次连接尝试时间（微秒）

        // 唤醒词检测状态
        bool wakeword_detected_{false}; // 是否检测到唤醒词

        // 音频上传状态
        bool listen_message_sent_{false}; // 是否已发送过 listen 消息
    };

} // namespace app
