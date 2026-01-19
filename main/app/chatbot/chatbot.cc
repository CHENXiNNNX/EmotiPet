#include "chatbot.hpp"

#include <memory>

#include "esp_log.h"

static const char* const TAG = "Chatbot";

namespace app
{
    namespace chatbot
    {

        // ========== Chatbot 实现 ==========

        Chatbot::Chatbot() : ws_client_(nullptr), initialized_(false) {}

        Chatbot::~Chatbot()
        {
            deinit();
        }

        bool Chatbot::init(const Config& config)
        {
            if (initialized_)
            {
                ESP_LOGW(TAG, "Chatbot 已经初始化");
                return true;
            }

            config_ = config;

            // 获取WebSocket客户端实例
            ws_client_ = &protocol::websocket::WebSocketClient::getInstance();

            // 配置WebSocket客户端
            protocol::websocket::Config ws_config;
            ws_config.uri                     = config_.server_uri;
            ws_config.ping_interval_sec       = config_.ping_interval_sec;
            ws_config.pingpong_timeout_sec    = config_.pingpong_timeout_sec;
            ws_config.reconnect_timeout_ms    = config_.reconnect_timeout_ms;
            ws_config.network_timeout_ms      = config_.network_timeout_ms;
            ws_config.disable_auto_reconnect  = config_.disable_auto_reconnect;
            ws_config.disable_pingpong_discon = config_.disable_pingpong_discon;
            ws_config.use_global_ca_store     = config_.use_global_ca_store;
            ws_config.skip_cert_verification  = config_.skip_cert_verification;

            if (!ws_client_->init(ws_config))
            {
                ESP_LOGE(TAG, "WebSocket 客户端初始化失败");
                return false;
            }

            // 设置WebSocket回调
            ws_client_->setConnectedCallback([this]() { this->onWebSocketConnected(); });

            ws_client_->setDisconnectedCallback([this]() { this->onWebSocketDisconnected(); });

            ws_client_->setDataCallback([this](const protocol::websocket::DataEvent& event)
                                        { this->onWebSocketData(event); });

            ws_client_->setErrorCallback([this](const protocol::websocket::ErrorEvent& event)
                                         { this->onWebSocketError(event); });

            initialized_ = true;
            ESP_LOGI(TAG, "Chatbot 初始化成功");
            return true;
        }

        void Chatbot::deinit()
        {
            if (!initialized_)
            {
                return;
            }

            disconnect();

            if (ws_client_)
            {
                ws_client_->deinit();
            }

            initialized_ = false;
            ESP_LOGI(TAG, "Chatbot 已反初始化");
        }

        bool Chatbot::connect()
        {
            if (!initialized_)
            {
                ESP_LOGE(TAG, "Chatbot 未初始化");
                return false;
            }

            if (!ws_client_)
            {
                ESP_LOGE(TAG, "WebSocket 客户端未初始化");
                return false;
            }

            ESP_LOGI(TAG, "正在连接到服务器: %s", config_.server_uri.c_str());
            return ws_client_->connect();
        }

        bool Chatbot::disconnect()
        {
            if (!ws_client_)
            {
                return false;
            }

            return ws_client_->disconnect();
        }

        bool Chatbot::isConnected() const
        {
            if (!ws_client_)
            {
                return false;
            }

            return ws_client_->isConnected();
        }

        bool Chatbot::sendMessage(const message::Message& msg)
        {
            if (!isConnected())
            {
                ESP_LOGW(TAG, "未连接到服务器，无法发送消息");
                return false;
            }

            // 创建消息的副本以便回调可以修改
            std::unique_ptr<message::Message> msg_copy;
            message::Message*                 msg_ptr = nullptr;

            // 根据消息类型创建副本（只处理设备需要发送的消息类型）
            message::MessageType type = msg.getType();
            switch (type)
            {
            case message::MessageType::TRANSPORT_INFO:
            {
                const auto& src  = static_cast<const message::TransportInfoMessage&>(msg);
                auto        copy = std::make_unique<message::TransportInfoMessage>();
                copy->base       = src.base;
                copy->command    = src.command;
                copy->data       = src.data;
                msg_copy         = std::move(copy);
                msg_ptr          = msg_copy.get();
                break;
            }
            case message::MessageType::BLUETOOTH_INFO:
            {
                const auto& src  = static_cast<const message::BluetoothInfoMessage&>(msg);
                auto        copy = std::make_unique<message::BluetoothInfoMessage>();
                copy->base       = src.base;
                copy->data       = src.data;
                msg_copy         = std::move(copy);
                msg_ptr          = msg_copy.get();
                break;
            }
            case message::MessageType::LISTEN:
            {
                const auto& src  = static_cast<const message::ListenMessage&>(msg);
                auto        copy = std::make_unique<message::ListenMessage>();
                copy->base       = src.base;
                msg_copy         = std::move(copy);
                msg_ptr          = msg_copy.get();
                break;
            }
            case message::MessageType::ERROR:
            {
                const auto& src  = static_cast<const message::ErrorMessage&>(msg);
                auto        copy = std::make_unique<message::ErrorMessage>();
                copy->base       = src.base;
                copy->data       = src.data;
                msg_copy         = std::move(copy);
                msg_ptr          = msg_copy.get();
                break;
            }
            default:
                ESP_LOGE(TAG, "设备不支持发送此消息类型: %d", static_cast<int>(type));
                return false;
            }

            if (!msg_ptr)
            {
                ESP_LOGE(TAG, "消息副本创建失败");
                return false;
            }

            // 调用发送回调（可以修改消息、填充字段等）
            std::string json_str;
            if (send_callback_)
            {
                json_str = send_callback_(*msg_ptr);
                // 如果回调返回空字符串，表示取消发送
                if (json_str.empty())
                {
                    ESP_LOGD(TAG, "发送回调取消发送");
                    return false;
                }
            }
            else
            {
                // 没有回调时，直接序列化
                json_str = msg_ptr->toJson();
            }

            if (json_str.empty())
            {
                ESP_LOGE(TAG, "消息序列化失败");
                return false;
            }

            return sendMessage(json_str);
        }

        bool Chatbot::sendMessage(const std::string& json_str)
        {
            if (!isConnected())
            {
                ESP_LOGW(TAG, "未连接到服务器，无法发送消息");
                return false;
            }

            if (!ws_client_)
            {
                ESP_LOGE(TAG, "WebSocket 客户端未初始化");
                return false;
            }

            int sent = ws_client_->sendText(json_str);
            if (sent < 0)
            {
                ESP_LOGE(TAG, "发送消息失败");
                return false;
            }

            ESP_LOGD(TAG, "消息已发送，长度: %d", sent);
            return true;
        }

        bool Chatbot::sendBinary(const uint8_t* data, size_t len, int timeout_ms)
        {
            if (!isConnected())
            {
                ESP_LOGW(TAG, "未连接到服务器，无法发送二进制数据");
                return false;
            }

            if (!ws_client_)
            {
                ESP_LOGE(TAG, "WebSocket 客户端未初始化");
                return false;
            }

            int sent = ws_client_->sendBinary(data, len, timeout_ms);
            if (sent < 0)
            {
                ESP_LOGE(TAG, "发送二进制数据失败");
                return false;
            }

            ESP_LOGD(TAG, "二进制数据已发送，长度: %d", sent);
            return true;
        }

        void Chatbot::setSendCallback(SendCallback&& callback)
        {
            send_callback_ = std::move(callback);
        }

        void Chatbot::setReceiveCallback(ReceiveCallback&& callback)
        {
            receive_callback_ = std::move(callback);
        }

        std::string Chatbot::getDeviceMacAddress() const
        {
            return app::chatbot::message::getDeviceMacAddress();
        }

        void Chatbot::onWebSocketConnected()
        {
            ESP_LOGI(TAG, "WebSocket 已连接到服务器");
            // 可以在这里发送初始消息，如设备信息等
        }

        void Chatbot::onWebSocketDisconnected()
        {
            ESP_LOGW(TAG, "WebSocket 已断开连接");
            // 可以在这里实现重连逻辑（如果WebSocket客户端未自动重连）
        }

        void Chatbot::onWebSocketData(const protocol::websocket::DataEvent& event)
        {
            // 只处理文本消息（JSON格式）
            if (!event.is_text)
            {
                ESP_LOGD(TAG, "收到二进制数据，长度: %d", event.length);
                // 二进制数据可能是音频数据，这里暂时不处理
                return;
            }

            // 将接收到的数据转换为字符串
            std::string json_str(reinterpret_cast<const char*>(event.data), event.length);

            ESP_LOGI(TAG, "收到文本消息，长度: %d", event.length);

            // 调用接收回调（统一处理消息解析、类型分发等）
            if (receive_callback_)
            {
                if (!receive_callback_(json_str))
                {
                    ESP_LOGW(TAG, "接收回调处理失败");
                }
            }
            else
            {
                ESP_LOGW(TAG, "接收回调未设置");
            }
        }

        void Chatbot::onWebSocketError(const protocol::websocket::ErrorEvent& event)
        {
            ESP_LOGE(TAG, "WebSocket 错误:");
            ESP_LOGE(TAG, "  ESP错误码: 0x%x", event.esp_err_code);
            ESP_LOGE(TAG, "  错误类型: %d", static_cast<int>(event.error_type));
            ESP_LOGE(TAG, "  握手状态: %d", event.handshake_status);
            ESP_LOGE(TAG, "  错误消息: %s", event.message.c_str());
        }

    } // namespace chatbot
} // namespace app
