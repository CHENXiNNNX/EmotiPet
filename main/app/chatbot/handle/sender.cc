#include "sender.hpp"

#include "../message/message.hpp"
#include "../../tool/time/time.hpp"
#include "esp_log.h"

static const char* const TAG = "MessageSender";

namespace app
{
    namespace chatbot
    {
        namespace handle
        {

            MessageSender::MessageSender() : auto_fill_base_fields_(true) {}

            std::string MessageSender::processMessage(message::Message& msg)
            {
                // 1. 自动填充基础字段
                if (auto_fill_base_fields_)
                {
                    message::BaseMessage base = msg.getBase();

                    // 填充 from（设备MAC地址）
                    if (base.from.empty())
                    {
                        base.from = message::getDeviceMacAddress();
                    }

                    // 填充 timestamp
                    if (base.timestamp.empty())
                    {
                        base.timestamp = app::tool::time::iso8601Timestamp();
                    }

                    // 更新基础字段
                    msg.setBase(base);
                }

                // 2. 调用预处理器（可以在发送前最后修改消息）
                if (preprocessor_)
                {
                    preprocessor_(msg);
                }

                // 3. 验证消息
                if (validator_)
                {
                    if (!validator_(msg))
                    {
                        ESP_LOGW(TAG, "消息验证失败，取消发送");
                        return ""; // 返回空字符串表示取消发送
                    }
                }

                // 4. 记录日志
                ESP_LOGI(TAG, "准备发送消息: type=%s, from=%s, to=%s",
                         message::messageTypeToString(msg.getType()), msg.getBase().from.c_str(),
                         msg.getBase().to.c_str());

                // 5. 序列化为JSON
                std::string json_str = msg.toJson();
                if (json_str.empty())
                {
                    ESP_LOGE(TAG, "消息序列化失败");
                    return "";
                }

                return json_str;
            }

        } // namespace handle
    } // namespace chatbot
} // namespace app
