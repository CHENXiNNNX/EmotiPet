#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

#include "esp_err.h"
#include "esp_websocket_client.h"

namespace app
{
    namespace protocol
    {
        namespace websocket
        {

            /**
             * @brief WebSocket 连接状态
             */
            enum class State
            {
                IDLE,        // 空闲，未初始化
                INITIALIZED, // 已初始化，未连接
                CONNECTING,  // 正在连接
                CONNECTED,   // 已连接
                DISCONNECTED // 已断开
            };

            /**
             * @brief WebSocket 事件类型
             */
            enum class EventType
            {
                CONNECTED,    // 连接成功
                DISCONNECTED, // 断开连接
                DATA,         // 接收数据
                ERROR         // 发生错误
            };

            /**
             * @brief WebSocket 数据事件信息
             */
            struct DataEvent
            {
                const uint8_t* data;    // 数据指针
                size_t         length;  // 数据长度
                bool           is_text; // 是否为文本数据
            };

            /**
             * @brief WebSocket 错误事件信息
             */
            struct ErrorEvent
            {
                esp_err_t                  esp_err_code;     // ESP 错误码
                esp_websocket_error_type_t error_type;       // WebSocket 错误类型
                int                        handshake_status; // 握手状态码
                std::string                message;          // 错误消息
            };

            // 回调函数类型定义
            using ConnectedCallback    = std::function<void()>;
            using DisconnectedCallback = std::function<void()>;
            using DataCallback         = std::function<void(const DataEvent& event)>;
            using ErrorCallback        = std::function<void(const ErrorEvent& event)>;
            using StateCallback        = std::function<void(State state)>;

            /**
             * @brief WebSocket 客户端配置
             */
            struct Config
            {
                std::string
                    uri; // WebSocket URI，例如 "ws://example.com/ws" 或 "wss://example.com/ws"
                std::string host;        // 主机地址（可选，URI中包含时可不填）
                int         port = 0;    // 端口（可选，URI中包含时可不填）
                std::string path;        // 路径（可选，URI中包含时可不填）
                std::string subprotocol; // 子协议（可选）
                std::string headers;     // 额外的 HTTP 头部（可选）
                int         ping_interval_sec       = 10;      // Ping 间隔（秒）
                int         pingpong_timeout_sec    = 10;      // Pong 超时（秒）
                int         reconnect_timeout_ms    = 10000;   // 重连超时（毫秒）
                int         network_timeout_ms      = 10000;   // 网络操作超时（毫秒）
                bool        disable_auto_reconnect  = false;   // 禁用自动重连
                bool        disable_pingpong_discon = false;   // 禁用 ping/pong 断开
                const char* cert_pem                = nullptr; // 服务器证书（WSS时需要）
                size_t      cert_len                = 0;       // 证书长度
                bool        skip_cert_common_name_check = false; // 跳过证书通用名检查
            };

            /**
             * @brief WebSocket 客户端管理器
             *
             * 提供单例模式的 WebSocket 客户端封装，支持连接、断开、发送和接收消息
             */
            class WebSocketClient
            {
            public:
                static WebSocketClient& getInstance();

                /**
                 * @brief 初始化 WebSocket 客户端
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
                 * @brief 发送文本消息（用于发送 JSON 消息）
                 * @param text 文本内容
                 * @param timeout_ms 超时时间（毫秒）
                 * @return 发送的字节数，-1 表示失败
                 */
                int sendText(const std::string& text, int timeout_ms = 5000);

                /**
                 * @brief 发送二进制数据
                 * @param data 数据指针
                 * @param len 数据长度
                 * @param timeout_ms 超时时间（毫秒）
                 * @return 发送的字节数，-1 表示失败
                 */
                int sendBinary(const uint8_t* data, size_t len, int timeout_ms = 5000);

                /**
                 * @brief 获取当前状态
                 * @return 连接状态
                 */
                State getState() const;

                /**
                 * @brief 是否已连接
                 * @return true 如果已连接
                 */
                bool isConnected() const;

                /**
                 * @brief 是否已初始化
                 * @return true 如果已初始化
                 */
                bool isInitialized() const;

                // 回调函数设置
                void setConnectedCallback(ConnectedCallback callback);
                void setDisconnectedCallback(DisconnectedCallback callback);
                void setDataCallback(DataCallback callback);
                void setErrorCallback(ErrorCallback callback);
                void setStateCallback(StateCallback callback);

            private:
                // 事件处理函数
                static void websocketEventHandler(void* handler_args, esp_event_base_t base,
                                                  int32_t event_id, void* event_data);

                void handleConnected();
                void handleDisconnected();
                void handleData(esp_websocket_event_data_t* event_data);
                void handleError(esp_websocket_event_data_t* event_data);
                void setState(State new_state);

                // 单例模式
                WebSocketClient()                       = default;
                ~WebSocketClient();
                WebSocketClient(const WebSocketClient&)            = delete;
                WebSocketClient& operator=(const WebSocketClient&) = delete;

                mutable std::mutex mutex_;
                State              state_       = State::IDLE;
                bool               initialized_ = false;

                esp_websocket_client_handle_t client_handle_ = nullptr;
                Config                        config_;

                // 回调函数
                ConnectedCallback    connected_callback_;
                DisconnectedCallback disconnected_callback_;
                DataCallback         data_callback_;
                ErrorCallback        error_callback_;
                StateCallback        state_callback_;
            };

        } // namespace websocket
    }     // namespace protocol
} // namespace app
