#include "handle.hpp"

#include "esp_log.h"

static const char* const TAG = "MessageHandler";

namespace app
{
    namespace chatbot
    {
        namespace handle
        {

            bool MessageHandler::handleMessage(const std::string& json_str)
            {
                using namespace app::chatbot::message;

                // 使用消息工厂创建消息对象
                auto msg = MessageFactory::createFromJson(json_str);
                if (!msg)
                {
                    ESP_LOGE(TAG, "无法解析消息: %s", json_str.c_str());
                    return false;
                }

                // 根据消息类型分发到对应的处理函数
                MessageType type = msg->getType();
                switch (type)
                {
                case MessageType::TRANSPORT_INFO:
                {
                    auto* transport_msg = dynamic_cast<TransportInfoMessage*>(msg.get());
                    if (transport_msg)
                    {
                        handleTransportInfo(*transport_msg);
                    }
                    break;
                }

                case MessageType::BLUETOOTH_INFO:
                {
                    auto* bt_msg = dynamic_cast<BluetoothInfoMessage*>(msg.get());
                    if (bt_msg)
                    {
                        handleBluetoothInfo(*bt_msg);
                    }
                    break;
                }

                case MessageType::RECV_INFO:
                {
                    auto* recv_msg = dynamic_cast<RecvInfoMessage*>(msg.get());
                    if (recv_msg)
                    {
                        handleRecvInfo(*recv_msg);
                    }
                    break;
                }

                case MessageType::MOV_INFO:
                {
                    auto* mov_msg = dynamic_cast<MovInfoMessage*>(msg.get());
                    if (mov_msg)
                    {
                        handleMovInfo(*mov_msg);
                    }
                    break;
                }

                case MessageType::LISTEN:
                {
                    auto* listen_msg = dynamic_cast<ListenMessage*>(msg.get());
                    if (listen_msg)
                    {
                        handleListen(*listen_msg);
                    }
                    break;
                }

                case MessageType::PLAY:
                {
                    auto* play_msg = dynamic_cast<PlayMessage*>(msg.get());
                    if (play_msg)
                    {
                        handlePlay(*play_msg);
                    }
                    break;
                }

                case MessageType::ERROR:
                {
                    auto* error_msg = dynamic_cast<ErrorMessage*>(msg.get());
                    if (error_msg)
                    {
                        handleError(*error_msg);
                    }
                    break;
                }

                default:
                    ESP_LOGW(TAG, "未知的消息类型: %d", static_cast<int>(type));
                    return false;
                }

                return true;
            }

        } // namespace handle
    } // namespace chatbot
} // namespace app
