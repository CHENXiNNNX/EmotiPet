#include "receiver.hpp"

#include "../message/message.hpp"
#include "esp_log.h"

static const char* const TAG = "MessageReceiver";

namespace app
{
    namespace chatbot
    {
        namespace handle
        {

            bool MessageReceiver::handleMessage(const std::string& json_str)
            {
                using namespace app::chatbot::message;

                // 统一解析JSON
                auto msg = MessageFactory::createFromJson(json_str);
                if (!msg)
                {
                    ESP_LOGE(TAG, "无法解析消息: %s", json_str.c_str());
                    return false;
                }

                // 统一日志记录
                MessageType type = msg->getType();
                ESP_LOGI(TAG, "收到消息类型: %s", messageTypeToString(type));

                // 按类型分发到对应的处理函数
                switch (type)
                {
                case MessageType::RECV_INFO:
                {
                    auto* recv_msg = dynamic_cast<RecvInfoMessage*>(msg.get());
                    if (recv_msg && recv_info_handler_)
                    {
                        recv_info_handler_(*recv_msg);
                    }
                    else if (!recv_info_handler_)
                    {
                        ESP_LOGW(TAG, "recv_info 处理函数未设置");
                    }
                    break;
                }

                case MessageType::MOV_INFO:
                {
                    auto* mov_msg = dynamic_cast<MovInfoMessage*>(msg.get());
                    if (mov_msg && mov_info_handler_)
                    {
                        mov_info_handler_(*mov_msg);
                    }
                    else if (!mov_info_handler_)
                    {
                        ESP_LOGW(TAG, "mov_info 处理函数未设置");
                    }
                    break;
                }

                case MessageType::PLAY:
                {
                    auto* play_msg = dynamic_cast<PlayMessage*>(msg.get());
                    if (play_msg && play_handler_)
                    {
                        play_handler_(*play_msg);
                    }
                    else if (!play_handler_)
                    {
                        ESP_LOGW(TAG, "play 处理函数未设置");
                    }
                    break;
                }

                case MessageType::EMOTION:
                {
                    auto* emotion_msg = dynamic_cast<EmotionMessage*>(msg.get());
                    if (emotion_msg && emotion_handler_)
                    {
                        emotion_handler_(*emotion_msg);
                    }
                    else if (!emotion_handler_)
                    {
                        ESP_LOGW(TAG, "emotion 处理函数未设置");
                    }
                    break;
                }

                case MessageType::ERROR:
                {
                    auto* error_msg = dynamic_cast<ErrorMessage*>(msg.get());
                    if (error_msg && error_handler_)
                    {
                        error_handler_(*error_msg);
                    }
                    else if (!error_handler_)
                    {
                        ESP_LOGW(TAG, "error 处理函数未设置");
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
