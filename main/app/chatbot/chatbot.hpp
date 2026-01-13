#pragma once

#include "handle/handle.hpp"
#include "message/message.hpp"
#include "protocol/websocket/websocket.hpp"

#include <memory>
#include <string>

namespace app
{
    namespace chatbot
    {

        /**
         * @brief 默认消息处理器（内部实现）
         *
         * 所有消息处理方法都保留空实现，等待后续完善
         */
        class DefaultMessageHandler : public handle::MessageHandler
        {
        public:
            DefaultMessageHandler()           = default;
            ~DefaultMessageHandler() override = default;

            void handleTransportInfo(const message::TransportInfoMessage& msg) override;
            void handleBluetoothInfo(const message::BluetoothInfoMessage& msg) override;
            void handleRecvInfo(const message::RecvInfoMessage& msg) override;
            void handleMovInfo(const message::MovInfoMessage& msg) override;
            void handleListen(const message::ListenMessage& msg) override;
            void handlePlay(const message::PlayMessage& msg) override;
            void handleError(const message::ErrorMessage& msg) override;
        };

        /**
         * @brief Chatbot 主类
         *
         * 负责管理WebSocket连接、消息发送和接收、消息路由等功能
         */
        class Chatbot
        {
        public:
            /**
             * @brief 配置结构
             */
            struct Config
            {
                std::string server_uri;                      // WebSocket服务器URI
                int         ping_interval_sec       = 10;    // Ping间隔（秒）
                int         pingpong_timeout_sec    = 10;    // Pong超时（秒）
                int         reconnect_timeout_ms    = 10000; // 重连超时（毫秒）
                int         network_timeout_ms      = 10000; // 网络操作超时（毫秒）
                bool        disable_auto_reconnect  = false; // 启用自动重连
                bool        disable_pingpong_discon = false; // 启用自动心跳包
            };

            /**
             * @brief 构造函数（使用默认消息处理器）
             */
            Chatbot();

            /**
             * @brief 构造函数
             * @param handler 消息处理器（必须实现MessageHandler接口）
             */
            explicit Chatbot(std::shared_ptr<handle::MessageHandler> handler);

            /**
             * @brief 析构函数
             */
            ~Chatbot();

            /**
             * @brief 初始化Chatbot
             * @param config 配置信息
             * @return 是否成功
             */
            bool init(const Config& config);

            /**
             * @brief 反初始化，释放资源
             */
            void deinit();

            /**
             * @brief 连接到服务器
             * @return 是否成功启动连接
             */
            bool connect();

            /**
             * @brief 断开连接
             * @return 是否成功
             */
            bool disconnect();

            /**
             * @brief 是否已连接
             * @return true 如果已连接
             */
            bool isConnected() const;

            /**
             * @brief 发送消息
             * @param msg 消息对象
             * @return 是否发送成功
             */
            bool sendMessage(const message::Message& msg);

            /**
             * @brief 发送消息（通过JSON字符串）
             * @param json_str JSON格式的消息字符串
             * @return 是否发送成功
             */
            bool sendMessage(const std::string& json_str);

            /**
             * @brief 发送二进制数据（用于发送图片、音频等）
             * @param data 数据指针
             * @param len 数据长度
             * @param timeout_ms 超时时间（毫秒），默认5000
             * @return 是否发送成功
             */
            bool sendBinary(const uint8_t* data, size_t len, int timeout_ms = 5000);

            /**
             * @brief 发送图片数据（JPEG格式）
             * @param image_data 图片数据指针
             * @param image_len 图片数据长度
             * @param timeout_ms 超时时间（毫秒），默认5000
             * @return 是否发送成功
             */
            bool sendImage(const uint8_t* image_data, size_t image_len, int timeout_ms = 5000);

            /**
             * @brief 发送音频数据（OPUS格式）
             * @param audio_data 音频数据指针
             * @param audio_len 音频数据长度
             * @param timeout_ms 超时时间（毫秒），默认5000
             * @return 是否发送成功
             */
            bool sendAudio(const uint8_t* audio_data, size_t audio_len, int timeout_ms = 5000);

            /**
             * @brief 获取设备MAC地址（用于消息的from字段）
             * @return MAC地址字符串
             */
            std::string getDeviceMacAddress() const;

        private:
            /**
             * @brief WebSocket连接成功回调
             */
            void onWebSocketConnected();

            /**
             * @brief WebSocket断开连接回调
             */
            void onWebSocketDisconnected();

            /**
             * @brief WebSocket数据接收回调
             * @param event 数据事件
             */
            void onWebSocketData(const protocol::websocket::DataEvent& event);

            /**
             * @brief WebSocket错误回调
             * @param event 错误事件
             */
            void onWebSocketError(const protocol::websocket::ErrorEvent& event);

            std::shared_ptr<handle::MessageHandler> handler_; // 消息处理器
            std::shared_ptr<DefaultMessageHandler>
                default_handler_; // 默认消息处理器（当未提供自定义handler时使用）
            protocol::websocket::WebSocketClient* ws_client_;   // WebSocket客户端指针
            Config                                config_;      // 配置信息
            bool                                  initialized_; // 是否已初始化
        };

    } // namespace chatbot
} // namespace app
