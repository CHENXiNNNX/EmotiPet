#pragma once

#include "message/message.hpp"
#include "protocol/websocket/websocket.hpp"

#include <cstddef>
#include <functional>
#include <memory>
#include <string>

namespace app
{
    namespace chatbot
    {

        /**
         * @brief Chatbot 主类
         *
         * 负责管理WebSocket连接、消息发送和接收等功能
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
                bool        use_global_ca_store     = false; // 使用全局 CA 存储 (WSS)
                bool        skip_cert_verification  = true;  // 跳过证书验证 (WSS)
            };

            /**
             * @brief 构造函数
             */
            Chatbot();

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
             * @brief 获取设备MAC地址
             * @return MAC地址字符串
             */
            std::string getDeviceMacAddress() const;

            // ========== 回调函数类型定义 ==========

            /**
             * @brief 发送回调函数类型
             *
             * 在消息发送前调用，可以：
             * - 统一填充基础字段（from, timestamp等）
             * - 验证消息内容
             * - 记录日志
             * - 修改消息内容
             *
             * @param msg 待发送的消息对象（可以修改）
             * @return 处理后的JSON字符串，如果返回空字符串则取消发送
             */
            using SendCallback = std::function<std::string(message::Message& msg)>;

            /**
             * @brief 接收回调函数类型
             *
             * 在消息接收后调用，按消息类型分发处理：
             * - 统一解析JSON
             * - 统一验证消息
             * - 统一记录日志
             * - 按类型调用对应的处理函数
             *
             * @param json_str 接收到的JSON字符串
             * @return 是否处理成功
             */
            using ReceiveCallback = std::function<bool(const std::string& json_str)>;

            /**
             * @brief 设置发送回调
             * @param callback 发送回调函数
             */
            void setSendCallback(SendCallback&& callback);

            /**
             * @brief 设置接收回调
             * @param callback 接收回调函数
             */
            void setReceiveCallback(ReceiveCallback&& callback);

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

            SendCallback                          send_callback_;    // 发送回调
            ReceiveCallback                       receive_callback_; // 接收回调
            protocol::websocket::WebSocketClient* ws_client_;        // WebSocket客户端指针
            Config                                config_;           // 配置信息
            bool                                  initialized_;      // 是否已初始化
        };

    } // namespace chatbot
} // namespace app
