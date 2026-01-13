#pragma once

#include "message/message.hpp"

#include <string>

namespace app
{
    namespace chatbot
    {
        namespace handle
        {

            /**
             * @brief 消息处理器类
             *
             * 负责处理从服务器接收到的各种消息类型，并执行相应的业务逻辑
             */
            class MessageHandler
            {
            public:
                MessageHandler()          = default;
                virtual ~MessageHandler() = default;

                /**
                 * @brief 处理接收到的消息
                 * @param json_str JSON格式的消息字符串
                 * @return 是否处理成功
                 */
                bool handleMessage(const std::string& json_str);

                /**
                 * @brief 处理 transport_info 消息（数据上传）
                 * @param msg 消息对象
                 */
                virtual void handleTransportInfo(const message::TransportInfoMessage& msg) = 0;

                /**
                 * @brief 处理 bluetooth_info 消息（蓝牙信息）
                 * @param msg 消息对象
                 */
                virtual void handleBluetoothInfo(const message::BluetoothInfoMessage& msg) = 0;

                /**
                 * @brief 处理 recv_info 消息（数据接收控制）
                 * @param msg 消息对象
                 */
                virtual void handleRecvInfo(const message::RecvInfoMessage& msg) = 0;

                /**
                 * @brief 处理 mov_info 消息（运动数据控制）
                 * @param msg 消息对象
                 */
                virtual void handleMovInfo(const message::MovInfoMessage& msg) = 0;

                /**
                 * @brief 处理 listen 消息（音频监听）
                 * @param msg 消息对象
                 */
                virtual void handleListen(const message::ListenMessage& msg) = 0;

                /**
                 * @brief 处理 play 消息（音频播放）
                 * @param msg 消息对象
                 */
                virtual void handlePlay(const message::PlayMessage& msg) = 0;

                /**
                 * @brief 处理 error 消息（错误信息）
                 * @param msg 消息对象
                 */
                virtual void handleError(const message::ErrorMessage& msg) = 0;
            };

        } // namespace handle
    } // namespace chatbot
} // namespace app
