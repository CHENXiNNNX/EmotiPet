#pragma once

#include "../message/message.hpp"

#include <functional>
#include <string>

namespace app
{
    namespace chatbot
    {
        namespace handle
        {

            /**
             * @brief 接收回调管理器
             *
             * 统一处理接收到的消息，按类型分发到对应的处理函数
             */
            class MessageReceiver
            {
            public:
                /**
                 * @brief 各类型消息的处理函数类型
                 */
                using RecvInfoHandler = std::function<void(const message::RecvInfoMessage&)>;
                using MovInfoHandler  = std::function<void(const message::MovInfoMessage&)>;
                using PlayHandler     = std::function<void(const message::PlayMessage&)>;
                using EmotionHandler  = std::function<void(const message::EmotionMessage&)>;
                using ErrorHandler    = std::function<void(const message::ErrorMessage&)>;

                /**
                 * @brief 构造函数
                 */
                MessageReceiver() = default;

                /**
                 * @brief 设置各类型消息的处理函数
                 */
                void setRecvInfoHandler(RecvInfoHandler&& handler)
                {
                    recv_info_handler_ = std::move(handler);
                }

                void setMovInfoHandler(MovInfoHandler&& handler)
                {
                    mov_info_handler_ = std::move(handler);
                }

                void setPlayHandler(PlayHandler&& handler)
                {
                    play_handler_ = std::move(handler);
                }

                void setEmotionHandler(EmotionHandler&& handler)
                {
                    emotion_handler_ = std::move(handler);
                }

                void setErrorHandler(ErrorHandler&& handler)
                {
                    error_handler_ = std::move(handler);
                }

                /**
                 * @brief 处理接收到的JSON消息
                 *
                 * 统一处理：解析、验证、日志、类型分发
                 *
                 * @param json_str JSON格式的消息字符串
                 * @return 是否处理成功
                 */
                bool handleMessage(const std::string& json_str);

            private:
                RecvInfoHandler recv_info_handler_;
                MovInfoHandler  mov_info_handler_;
                PlayHandler     play_handler_;
                EmotionHandler  emotion_handler_;
                ErrorHandler    error_handler_;
            };

        } // namespace handle
    } // namespace chatbot
} // namespace app
