#include "websocket.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_websocket_client.h"

static const char* const TAG = "WebSocket";

namespace app
{
    namespace protocol
    {
        namespace websocket
        {

            WebSocketClient& WebSocketClient::getInstance()
            {
                static WebSocketClient instance;
                return instance;
            }

            bool WebSocketClient::init(const Config& config)
            {
                StateCallback state_cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (initialized_)
                    {
                        ESP_LOGW(TAG, "WebSocket 客户端已初始化");
                        return false;
                    }

                    config_ = config;

                    // 配置 WebSocket 客户端
                    esp_websocket_client_config_t ws_cfg = {};

                    if (!config.uri.empty())
                    {
                        ws_cfg.uri = config.uri.c_str();
                    }
                    else
                    {
                        if (!config.host.empty())
                        {
                            ws_cfg.host = config.host.c_str();
                        }
                        ws_cfg.port = config.port;
                        if (!config.path.empty())
                        {
                            ws_cfg.path = config.path.c_str();
                        }
                    }

                    if (!config.subprotocol.empty())
                    {
                        ws_cfg.subprotocol = config.subprotocol.c_str();
                    }

                    if (!config.headers.empty())
                    {
                        ws_cfg.headers = config.headers.c_str();
                    }

                    ws_cfg.ping_interval_sec           = config.ping_interval_sec;
                    ws_cfg.pingpong_timeout_sec        = config.pingpong_timeout_sec;
                    ws_cfg.reconnect_timeout_ms        = config.reconnect_timeout_ms;
                    ws_cfg.network_timeout_ms          = config.network_timeout_ms;
                    ws_cfg.disable_auto_reconnect      = config.disable_auto_reconnect;
                    ws_cfg.disable_pingpong_discon     = config.disable_pingpong_discon;
                    ws_cfg.skip_cert_common_name_check = config.skip_cert_common_name_check;

                    if (config.cert_pem != nullptr && config.cert_len > 0)
                    {
                        ws_cfg.cert_pem = config.cert_pem;
                        ws_cfg.cert_len = config.cert_len;
                    }

                    // 判断传输类型（如果 URI 不为空）
                    if (!config.uri.empty())
                    {
                        ws_cfg.transport =
                            config.uri.find("wss://") == 0 || config.uri.find("https://") == 0
                                ? WEBSOCKET_TRANSPORT_OVER_SSL
                                : WEBSOCKET_TRANSPORT_OVER_TCP;
                    }
                    else
                    {
                        // 如果 URI 为空，默认使用 TCP（可以通过 port 判断，443 通常为 SSL）
                        ws_cfg.transport = (config.port == 443) ? WEBSOCKET_TRANSPORT_OVER_SSL
                                                                : WEBSOCKET_TRANSPORT_OVER_TCP;
                    }

                    // 设置事件处理器
                    ws_cfg.user_context = this;

                    // 初始化客户端
                    client_handle_ = esp_websocket_client_init(&ws_cfg);
                    if (client_handle_ == nullptr)
                    {
                        ESP_LOGE(TAG, "WebSocket 客户端初始化失败");
                        return false;
                    }

                    // 注册事件处理器
                    esp_err_t ret = esp_websocket_register_events(
                        client_handle_, WEBSOCKET_EVENT_ANY, websocketEventHandler, this);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "注册事件处理器失败: %s", esp_err_to_name(ret));
                        esp_websocket_client_destroy(client_handle_);
                        client_handle_ = nullptr;
                        return false;
                    }

                    initialized_ = true;
                    state_       = State::INITIALIZED;
                    state_cb     = state_callback_;
                }

                // 在锁外调用回调
                if (state_cb)
                {
                    state_cb(State::INITIALIZED);
                }

                ESP_LOGI(TAG, "WebSocket 客户端初始化成功");
                return true;
            }

            WebSocketClient::~WebSocketClient()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    // 在析构函数中不调用 deinit()，因为它需要 unlock mutex
                    // 直接清理资源
                    if (state_ == State::CONNECTED || state_ == State::CONNECTING)
                    {
                        if (client_handle_ != nullptr)
                        {
                            esp_websocket_client_stop(client_handle_);
                        }
                    }

                    if (client_handle_ != nullptr)
                    {
                        esp_websocket_client_destroy(client_handle_);
                        client_handle_ = nullptr;
                    }

                    initialized_ = false;
                    state_       = State::IDLE;
                }
            }

            void WebSocketClient::deinit()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    return;
                }

                // 如果已连接，先断开
                if (state_ == State::CONNECTED || state_ == State::CONNECTING)
                {
                    disconnect();
                }

                if (client_handle_ != nullptr)
                {
                    esp_websocket_client_destroy(client_handle_);
                    client_handle_ = nullptr;
                }

                initialized_ = false;
                state_       = State::IDLE;

                // 清空回调
                connected_callback_    = nullptr;
                disconnected_callback_ = nullptr;
                data_callback_         = nullptr;
                error_callback_        = nullptr;
                state_callback_        = nullptr;

                ESP_LOGI(TAG, "WebSocket 客户端已反初始化");
            }

            bool WebSocketClient::connect()
            {
                StateCallback state_cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (!initialized_)
                    {
                        ESP_LOGE(TAG, "WebSocket 客户端未初始化");
                        return false;
                    }

                    if (state_ == State::CONNECTED || state_ == State::CONNECTING)
                    {
                        ESP_LOGW(TAG, "WebSocket 已连接或正在连接中");
                        return false;
                    }

                    if (client_handle_ == nullptr)
                    {
                        ESP_LOGE(TAG, "WebSocket 客户端句柄无效");
                        return false;
                    }

                    // 设置状态（在锁内）
                    if (state_ != State::CONNECTING)
                    {
                        state_   = State::CONNECTING;
                        state_cb = state_callback_;
                    }
                }

                // 在锁外调用状态回调
                if (state_cb)
                {
                    state_cb(State::CONNECTING);
                }

                esp_err_t ret = esp_websocket_client_start(client_handle_);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "启动 WebSocket 连接失败: %s", esp_err_to_name(ret));
                    // 设置失败状态
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (state_ == State::CONNECTING)
                        {
                            state_   = State::DISCONNECTED;
                            state_cb = state_callback_;
                        }
                        else
                        {
                            state_cb = nullptr;
                        }
                    }
                    if (state_cb)
                    {
                        state_cb(State::DISCONNECTED);
                    }
                    return false;
                }

                ESP_LOGI(TAG, "正在连接 WebSocket 服务器...");
                return true;
            }

            bool WebSocketClient::disconnect()
            {
                StateCallback state_cb;
                bool          should_stop = false;

                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (!initialized_ || client_handle_ == nullptr)
                    {
                        return false;
                    }

                    if (state_ != State::CONNECTED && state_ != State::CONNECTING)
                    {
                        return false;
                    }

                    should_stop = true;
                }

                // 在锁外调用 esp_websocket_client_stop
                if (should_stop)
                {
                    esp_err_t ret = esp_websocket_client_stop(client_handle_);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "断开 WebSocket 连接失败: %s", esp_err_to_name(ret));
                        return false;
                    }

                    // 设置状态（在锁内）
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        if (state_ != State::DISCONNECTED)
                        {
                            state_   = State::DISCONNECTED;
                            state_cb = state_callback_;
                        }
                    }

                    // 在锁外调用状态回调
                    if (state_cb)
                    {
                        state_cb(State::DISCONNECTED);
                    }

                    ESP_LOGI(TAG, "WebSocket 已断开连接");
                    return true;
                }

                return false;
            }

            int WebSocketClient::sendText(const std::string& text, int timeout_ms)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_ || client_handle_ == nullptr)
                {
                    ESP_LOGE(TAG, "WebSocket 客户端未初始化");
                    return -1;
                }

                if (state_ != State::CONNECTED)
                {
                    ESP_LOGE(TAG, "WebSocket 未连接，无法发送数据");
                    return -1;
                }

                TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
                int        sent          = esp_websocket_client_send_text(
                                    client_handle_, text.c_str(), static_cast<int>(text.length()), timeout_ticks);

                if (sent < 0)
                {
                    ESP_LOGE(TAG, "发送文本消息失败");
                }
                else
                {
                    ESP_LOGD(TAG, "已发送文本消息，长度: %d", sent);
                }

                return sent;
            }

            int WebSocketClient::sendBinary(const uint8_t* data, size_t len, int timeout_ms)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_ || client_handle_ == nullptr)
                {
                    ESP_LOGE(TAG, "WebSocket 客户端未初始化");
                    return -1;
                }

                if (state_ != State::CONNECTED)
                {
                    ESP_LOGE(TAG, "WebSocket 未连接，无法发送数据");
                    return -1;
                }

                TickType_t timeout_ticks = pdMS_TO_TICKS(timeout_ms);
                int        sent          = esp_websocket_client_send_bin(client_handle_,
                                                                         reinterpret_cast<const char*>(data),
                                                                         static_cast<int>(len), timeout_ticks);

                if (sent < 0)
                {
                    ESP_LOGE(TAG, "发送二进制数据失败");
                }
                else
                {
                    ESP_LOGD(TAG, "已发送二进制数据，长度: %d", sent);
                }

                return sent;
            }

            State WebSocketClient::getState() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return state_;
            }

            bool WebSocketClient::isConnected() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return state_ == State::CONNECTED;
            }

            bool WebSocketClient::isInitialized() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return initialized_;
            }

            void WebSocketClient::setConnectedCallback(ConnectedCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                connected_callback_ = callback;
            }

            void WebSocketClient::setDisconnectedCallback(DisconnectedCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                disconnected_callback_ = callback;
            }

            void WebSocketClient::setDataCallback(DataCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                data_callback_ = callback;
            }

            void WebSocketClient::setErrorCallback(ErrorCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                error_callback_ = callback;
            }

            void WebSocketClient::setStateCallback(StateCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_callback_ = callback;
            }

            void WebSocketClient::websocketEventHandler(void* handler_args, esp_event_base_t base,
                                                        int32_t event_id, void* event_data)
            {
                auto* client = static_cast<WebSocketClient*>(handler_args);
                if (client == nullptr)
                {
                    return;
                }

                esp_websocket_event_id_t ws_event_id =
                    static_cast<esp_websocket_event_id_t>(event_id);
                auto* event_data_ptr = static_cast<esp_websocket_event_data_t*>(event_data);

                switch (ws_event_id)
                {
                case WEBSOCKET_EVENT_CONNECTED:
                    client->handleConnected();
                    break;

                case WEBSOCKET_EVENT_DISCONNECTED:
                    client->handleDisconnected();
                    break;

                case WEBSOCKET_EVENT_DATA:
                    client->handleData(event_data_ptr);
                    break;

                case WEBSOCKET_EVENT_ERROR:
                    client->handleError(event_data_ptr);
                    break;

                case WEBSOCKET_EVENT_CLOSED:
                    client->handleDisconnected();
                    break;

                default:
                    break;
                }
            }

            void WebSocketClient::handleConnected()
            {
                ConnectedCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    cb = connected_callback_;
                }

                ESP_LOGI(TAG, "WebSocket 已连接");

                setState(State::CONNECTED);

                if (cb)
                {
                    cb();
                }
            }

            void WebSocketClient::handleDisconnected()
            {
                DisconnectedCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    cb = disconnected_callback_;
                }

                ESP_LOGI(TAG, "WebSocket 已断开连接");

                setState(State::DISCONNECTED);

                if (cb)
                {
                    cb();
                }
            }

            void WebSocketClient::handleData(esp_websocket_event_data_t* event_data)
            {
                if (event_data == nullptr)
                {
                    return;
                }

                // 跳过长度为0的数据
                if (event_data->data_len == 0)
                {
                    return;
                }

                DataCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    cb = data_callback_;
                }

                if (cb)
                {
                    DataEvent event;
                    event.data    = reinterpret_cast<const uint8_t*>(event_data->data_ptr);
                    event.length  = static_cast<size_t>(event_data->data_len);
                    event.is_text = (event_data->op_code == 0x1); // 文本帧 opcode = 0x1

                    ESP_LOGD(TAG, "收到 WebSocket 数据，长度: %d, 类型: %s", event.length,
                             event.is_text ? "文本" : "二进制");

                    cb(event);
                }
            }

            void WebSocketClient::handleError(esp_websocket_event_data_t* event_data)
            {
                if (event_data == nullptr)
                {
                    return;
                }

                ErrorCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    cb = error_callback_;
                }

                if (cb)
                {
                    ErrorEvent event;
                    event.esp_err_code     = event_data->error_handle.esp_tls_last_esp_err;
                    event.error_type       = event_data->error_handle.error_type;
                    event.handshake_status = event_data->error_handle.esp_ws_handshake_status_code;

                    // 生成错误消息
                    char msg[128];
                    snprintf(msg, sizeof(msg),
                             "WebSocket 错误: type=%d, esp_err=0x%x, handshake=%d",
                             event.error_type, event.esp_err_code, event.handshake_status);
                    event.message = msg;

                    ESP_LOGE(TAG, "%s", event.message.c_str());

                    cb(event);
                }
            }

            void WebSocketClient::setState(State new_state)
            {
                StateCallback state_cb;
                bool          state_changed = false;

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (state_ != new_state)
                    {
                        state_        = new_state;
                        state_changed = true;
                        state_cb      = state_callback_;
                    }
                }

                if (state_changed && state_cb)
                {
                    state_cb(new_state);
                }
            }

        } // namespace websocket
    }     // namespace protocol
} // namespace app
