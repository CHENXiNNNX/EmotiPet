#include "chatbot.hpp"

#include "esp_log.h"

static const char* const TAG = "Chatbot";

namespace app
{
    namespace chatbot
    {

        // ========== DefaultMessageHandler 实现 ==========

        void DefaultMessageHandler::handleTransportInfo(const message::TransportInfoMessage& msg)
        {
            ESP_LOGI(TAG, "收到 transport_info 消息:");
            ESP_LOGI(TAG, "  from: %s", msg.base.from.c_str());
            ESP_LOGI(TAG, "  to: %s", msg.base.to.c_str());
            ESP_LOGI(TAG, "  command: %s", msg.command.c_str());
            ESP_LOGI(TAG, "  touch: %d", msg.data.touch);
            ESP_LOGI(TAG, "  pressure: %.2f", msg.data.pressure);
            ESP_LOGI(TAG, "  gyroscope: x=%.2f, y=%.2f, z=%.2f", msg.data.gyroscope.x,
                     msg.data.gyroscope.y, msg.data.gyroscope.z);
            ESP_LOGI(TAG, "  photosensitive: %d", msg.data.photosensitive);
            // TODO: 实现数据上传处理逻辑
        }

        void DefaultMessageHandler::handleBluetoothInfo(const message::BluetoothInfoMessage& msg)
        {
            ESP_LOGI(TAG, "收到 bluetooth_info 消息:");
            ESP_LOGI(TAG, "  from: %s", msg.base.from.c_str());
            ESP_LOGI(TAG, "  to: %s", msg.base.to.c_str());
            ESP_LOGI(TAG, "  rssi: %d", msg.data.rssi);
            ESP_LOGI(TAG, "  opposite_mac: %s", msg.data.opposite_mac.c_str());
            // TODO: 实现蓝牙信息处理逻辑
        }

        void DefaultMessageHandler::handleRecvInfo(const message::RecvInfoMessage& msg)
        {
            ESP_LOGI(TAG, "收到 recv_info 消息:");
            ESP_LOGI(TAG, "  from: %s", msg.base.from.c_str());
            ESP_LOGI(TAG, "  to: %s", msg.base.to.c_str());
            ESP_LOGI(TAG, "  command: %s", msg.command.c_str());

            // 解析command字段
            message::SensorEnable enable;
            if (enable.fromCommandString(msg.command))
            {
                ESP_LOGI(TAG, "  数据上传控制:");
                ESP_LOGI(TAG, "    触摸: %s", enable.touch ? "上传" : "不上传");
                ESP_LOGI(TAG, "    压力: %s", enable.pressure ? "上传" : "不上传");
                ESP_LOGI(TAG, "    陀螺仪: %s", enable.gyroscope ? "上传" : "不上传");
                ESP_LOGI(TAG, "    光敏: %s", enable.photosensitive ? "上传" : "不上传");
                ESP_LOGI(TAG, "    摄像头: %s", enable.camera ? "上传" : "不上传");
            }
            // TODO: 实现数据上传控制逻辑，保存控制状态，用于后续数据上传时判断
        }

        void DefaultMessageHandler::handleMovInfo(const message::MovInfoMessage& msg)
        {
            ESP_LOGI(TAG, "收到 mov_info 消息:");
            ESP_LOGI(TAG, "  from: %s", msg.base.from.c_str());
            ESP_LOGI(TAG, "  to: %s", msg.base.to.c_str());
            ESP_LOGI(TAG, "  舵机数量: %d", static_cast<int>(msg.data.size()));

            for (const auto& pair : msg.data)
            {
                ESP_LOGI(TAG, "  %s:", pair.first.c_str());
                ESP_LOGI(TAG, "    起始时间: %s", pair.second.start_time.c_str());
                ESP_LOGI(TAG, "    角度: %d", pair.second.angle);
                ESP_LOGI(TAG, "    持续时间: %d ms", pair.second.duration);
            }
            // TODO: 实现运动控制逻辑，调用舵机控制接口
        }

        void DefaultMessageHandler::handleListen(const message::ListenMessage& msg)
        {
            ESP_LOGI(TAG, "收到 listen 消息:");
            ESP_LOGI(TAG, "  from: %s", msg.base.from.c_str());
            ESP_LOGI(TAG, "  to: %s", msg.base.to.c_str());
            // TODO: 实现音频监听准备逻辑，准备音频采集，开始上传音频数据
        }

        void DefaultMessageHandler::handlePlay(const message::PlayMessage& msg)
        {
            ESP_LOGI(TAG, "收到 play 消息:");
            ESP_LOGI(TAG, "  from: %s", msg.base.from.c_str());
            ESP_LOGI(TAG, "  to: %s", msg.base.to.c_str());
            // TODO: 实现音频播放准备逻辑，准备接收并播放服务器下发的音频数据
        }

        void DefaultMessageHandler::handleError(const message::ErrorMessage& msg)
        {
            ESP_LOGE(TAG, "收到 error 消息:");
            ESP_LOGE(TAG, "  from: %s", msg.base.from.c_str());
            ESP_LOGE(TAG, "  to: %s", msg.base.to.c_str());
            ESP_LOGE(TAG, "  错误码: %d", msg.data.code);
            ESP_LOGE(TAG, "  错误信息: %s", msg.data.message.c_str());
            // TODO: 实现错误处理逻辑，根据错误码执行相应的处理
        }

        // ========== Chatbot 实现 ==========

        Chatbot::Chatbot()
            : handler_(nullptr), default_handler_(std::make_shared<DefaultMessageHandler>()),
              ws_client_(nullptr), initialized_(false)
        {
            handler_ = default_handler_;
        }

        Chatbot::Chatbot(std::shared_ptr<handle::MessageHandler> handler)
            : handler_(handler), default_handler_(nullptr), ws_client_(nullptr), initialized_(false)
        {
            if (!handler_)
            {
                ESP_LOGE(TAG, "消息处理器不能为空，使用默认处理器");
                default_handler_ = std::make_shared<DefaultMessageHandler>();
                handler_         = default_handler_;
            }
        }

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

            if (!handler_)
            {
                ESP_LOGE(TAG, "消息处理器未设置");
                return false;
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

            std::string json_str = msg.toJson();
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

        bool Chatbot::sendImage(const uint8_t* image_data, size_t image_len, int timeout_ms)
        {
            if (!image_data || image_len == 0)
            {
                ESP_LOGE(TAG, "图片数据无效");
                return false;
            }

            // 验证是否为JPEG格式（检查文件头）
            if (image_len < 3 || image_data[0] != 0xFF || image_data[1] != 0xD8 ||
                image_data[2] != 0xFF)
            {
                ESP_LOGW(TAG, "警告：数据可能不是有效的JPEG格式");
            }

            ESP_LOGI(TAG, "发送图片数据，长度: %d 字节", image_len);
            return sendBinary(image_data, image_len, timeout_ms);
        }

        bool Chatbot::sendAudio(const uint8_t* audio_data, size_t audio_len, int timeout_ms)
        {
            if (!audio_data || audio_len == 0)
            {
                ESP_LOGE(TAG, "音频数据无效");
                return false;
            }

            ESP_LOGI(TAG, "发送音频数据，长度: %d 字节", audio_len);
            return sendBinary(audio_data, audio_len, timeout_ms);
        }

        std::string Chatbot::getDeviceMacAddress() const
        {
            return message::getDeviceMacAddress();
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

            ESP_LOGI(TAG, "收到文本消息:");
            ESP_LOGI(TAG, "%s", json_str.c_str());

            // 使用消息处理器处理消息
            if (handler_)
            {
                handler_->handleMessage(json_str);
            }
            else
            {
                ESP_LOGW(TAG, "消息处理器未设置");
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
