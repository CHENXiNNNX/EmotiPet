#pragma once

#include "message/message.hpp"

#include <functional>
#include <string>

namespace app
{
    namespace chatbot
    {
        namespace handle
        {

            /**
             * @brief 发送回调管理器
             *
             * 统一处理消息发送前的数据整合、字段填充、验证等逻辑
             */
            class MessageSender
            {
            public:
                /**
                 * @brief 构造函数
                 */
                MessageSender();

                /**
                 * @brief 处理发送消息
                 *
                 * 统一处理：
                 * - 自动填充基础字段（from, timestamp等）
                 * - 验证消息内容
                 * - 记录日志
                 * - 序列化为JSON
                 *
                 * @param msg 待发送的消息对象（会被修改）
                 * @return 处理后的JSON字符串，如果返回空字符串则取消发送
                 */
                std::string processMessage(message::Message& msg);

                /**
                 * @brief 设置是否自动填充基础字段
                 * @param auto_fill 是否自动填充
                 */
                void setAutoFillBaseFields(bool auto_fill)
                {
                    auto_fill_base_fields_ = auto_fill;
                }

                /**
                 * @brief 设置消息验证回调
                 * @param validator 验证函数，返回false表示消息无效，取消发送
                 */
                using MessageValidator = std::function<bool(const message::Message& msg)>;
                void setValidator(MessageValidator validator)
                {
                    validator_ = validator;
                }

                /**
                 * @brief 设置消息预处理回调
                 * @param preprocessor 预处理函数，可以在发送前最后修改消息
                 */
                using MessagePreprocessor = std::function<void(message::Message& msg)>;
                void setPreprocessor(MessagePreprocessor preprocessor)
                {
                    preprocessor_ = preprocessor;
                }

            private:
                bool                auto_fill_base_fields_; // 是否自动填充基础字段
                MessageValidator    validator_;             // 消息验证器
                MessagePreprocessor preprocessor_;          // 消息预处理器
            };

        } // namespace handle
    } // namespace chatbot
} // namespace app
