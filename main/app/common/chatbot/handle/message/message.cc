#include "message.hpp"

#include <cstring>

#include "cJSON.h"
#include "esp_log.h"
#include "tool/time/time.hpp"

static const char* const TAG = "Message";

namespace app
{
    namespace common
    {
        namespace chatbot
        {
            namespace handle
            {
                namespace message
                {

                    std::string buildHelloMessage(const std::string& device_id,
                                                  const std::string& client_id,
                                                  const Features&    features,
                                                  const AudioParams& audio_params)
                    {
                        cJSON* json = cJSON_CreateObject();
                        if (!json)
                        {
                            ESP_LOGE(TAG, "创建 JSON 对象失败");
                            return "";
                        }

                        // 基本字段
                        cJSON_AddStringToObject(json, "type", "hello");
                        cJSON_AddNumberToObject(json, "version", 1.0);
                        cJSON_AddStringToObject(json, "transport", "websocket");
                        cJSON_AddStringToObject(json, "device_id", device_id.c_str());
                        cJSON_AddStringToObject(json, "client_id", client_id.c_str());

                        // 功能特性
                        cJSON* features_obj = cJSON_CreateObject();
                        if (features_obj)
                        {
                            cJSON_AddBoolToObject(features_obj, "aec", features.aec);
                            cJSON_AddBoolToObject(features_obj, "mcp", features.mcp);
                            cJSON_AddItemToObject(json, "features", features_obj);
                        }

                        // 音频参数
                        cJSON* audio_obj = cJSON_CreateObject();
                        if (audio_obj)
                        {
                            cJSON_AddStringToObject(audio_obj, "format",
                                                    audio_params.format.c_str());
                            cJSON_AddNumberToObject(audio_obj, "sample_rate",
                                                    audio_params.sample_rate);
                            cJSON_AddNumberToObject(audio_obj, "channels", audio_params.channels);
                            cJSON_AddNumberToObject(audio_obj, "frame_duration",
                                                    audio_params.frame_duration);
                            cJSON_AddItemToObject(json, "audio_params", audio_obj);
                        }

                        char*       json_str = cJSON_Print(json);
                        std::string result;
                        if (json_str)
                        {
                            result = json_str;
                            free(json_str);
                        }
                        cJSON_Delete(json);

                        return result;
                    }

                    std::string buildResSyncMessage(const std::string& device_id,
                                                    const std::string& data)
                    {
                        cJSON* json = cJSON_CreateObject();
                        if (!json)
                        {
                            ESP_LOGE(TAG, "创建 JSON 对象失败");
                            return "";
                        }

                        // 基本字段
                        cJSON_AddStringToObject(json, "type", "res_sync");
                        cJSON_AddStringToObject(json, "from", device_id.c_str());
                        cJSON_AddStringToObject(json, "to", "server");
                        cJSON_AddStringToObject(json, "timestamp",
                                                app::tool::time::iso8601Timestamp().c_str());

                        // 数据字段
                        cJSON* data_obj = cJSON_Parse(data.c_str());
                        if (data_obj && cJSON_IsObject(data_obj))
                        {
                            cJSON_AddItemToObject(json, "data", data_obj);
                        }
                        else
                        {
                            // 如果解析失败，创建空对象
                            if (data_obj)
                            {
                                cJSON_Delete(data_obj);
                            }
                            cJSON* empty_data = cJSON_CreateObject();
                            cJSON_AddItemToObject(json, "data", empty_data);
                        }

                        char*       json_str = cJSON_Print(json);
                        std::string result;
                        if (json_str)
                        {
                            result = json_str;
                            free(json_str);
                        }
                        cJSON_Delete(json);

                        return result;
                    }

                    std::string buildErrorMessage(const std::string& device_id, int error_code,
                                                  const std::string& error_message)
                    {
                        cJSON* json = cJSON_CreateObject();
                        if (!json)
                        {
                            ESP_LOGE(TAG, "创建 JSON 对象失败");
                            return "";
                        }

                        // 基本字段
                        cJSON_AddStringToObject(json, "type", "error");
                        cJSON_AddStringToObject(json, "from", device_id.c_str());
                        cJSON_AddStringToObject(json, "to", "server");
                        cJSON_AddStringToObject(json, "timestamp",
                                                app::tool::time::iso8601Timestamp().c_str());

                        // 错误数据
                        cJSON* data_obj = cJSON_CreateObject();
                        if (data_obj)
                        {
                            cJSON_AddNumberToObject(data_obj, "code", error_code);
                            cJSON_AddStringToObject(data_obj, "message", error_message.c_str());
                            cJSON_AddItemToObject(json, "data", data_obj);
                        }

                        char*       json_str = cJSON_Print(json);
                        std::string result;
                        if (json_str)
                        {
                            result = json_str;
                            free(json_str);
                        }
                        cJSON_Delete(json);

                        return result;
                    }

                    bool parseHelloAck(const std::string& json, Features& features)
                    {
                        cJSON* root = cJSON_Parse(json.c_str());
                        if (!root)
                        {
                            ESP_LOGE(TAG, "解析 JSON 失败");
                            return false;
                        }

                        // 验证消息类型
                        cJSON* type_item = cJSON_GetObjectItem(root, "type");
                        if (!type_item || !cJSON_IsString(type_item) ||
                            strcmp(type_item->valuestring, "hello_ack") != 0)
                        {
                            ESP_LOGE(TAG, "不是 hello_ack 消息");
                            cJSON_Delete(root);
                            return false;
                        }

                        // 解析功能特性
                        cJSON* features_item = cJSON_GetObjectItem(root, "features");
                        if (features_item && cJSON_IsObject(features_item))
                        {
                            cJSON* aec_item = cJSON_GetObjectItem(features_item, "aec");
                            if (cJSON_IsBool(aec_item))
                            {
                                features.aec = cJSON_IsTrue(aec_item);
                            }

                            cJSON* mcp_item = cJSON_GetObjectItem(features_item, "mcp");
                            if (cJSON_IsBool(mcp_item))
                            {
                                features.mcp = cJSON_IsTrue(mcp_item);
                            }
                        }

                        cJSON_Delete(root);
                        return true;
                    }

                    bool parseCommand(const std::string& json, std::string& from, std::string& to,
                                      std::string& timestamp, CommandData& cmd_data)
                    {
                        cJSON* root = cJSON_Parse(json.c_str());
                        if (!root)
                        {
                            ESP_LOGE(TAG, "解析 JSON 失败");
                            return false;
                        }

                        // 验证消息类型
                        cJSON* type_item = cJSON_GetObjectItem(root, "type");
                        if (!type_item || !cJSON_IsString(type_item) ||
                            strcmp(type_item->valuestring, "command") != 0)
                        {
                            ESP_LOGE(TAG, "不是 command 消息");
                            cJSON_Delete(root);
                            return false;
                        }

                        // 解析基本字段
                        cJSON* from_item = cJSON_GetObjectItem(root, "from");
                        if (cJSON_IsString(from_item))
                        {
                            from = from_item->valuestring;
                        }

                        cJSON* to_item = cJSON_GetObjectItem(root, "to");
                        if (cJSON_IsString(to_item))
                        {
                            to = to_item->valuestring;
                        }

                        cJSON* timestamp_item = cJSON_GetObjectItem(root, "timestamp");
                        if (cJSON_IsString(timestamp_item))
                        {
                            timestamp = timestamp_item->valuestring;
                        }

                        // 解析命令数据
                        cJSON* data_item = cJSON_GetObjectItem(root, "data");
                        if (data_item && cJSON_IsObject(data_item))
                        {
                            cJSON* cmd_item = cJSON_GetObjectItem(data_item, "cmd");
                            if (cJSON_IsString(cmd_item))
                            {
                                cmd_data.cmd = cmd_item->valuestring;
                            }

                            cJSON* sound_id_item = cJSON_GetObjectItem(data_item, "sound_id");
                            if (cJSON_IsString(sound_id_item))
                            {
                                cmd_data.sound_id = sound_id_item->valuestring;
                            }

                            cJSON* reason_item = cJSON_GetObjectItem(data_item, "reason");
                            if (cJSON_IsString(reason_item))
                            {
                                cmd_data.reason = reason_item->valuestring;
                            }
                        }
                        else
                        {
                            ESP_LOGE(TAG, "缺少 data 字段");
                            cJSON_Delete(root);
                            return false;
                        }

                        cJSON_Delete(root);
                        return true;
                    }

                    bool parseResSync(const std::string& json, std::string& from, std::string& to,
                                      std::string& timestamp, std::string& data)
                    {
                        cJSON* root = cJSON_Parse(json.c_str());
                        if (!root)
                        {
                            ESP_LOGE(TAG, "解析 JSON 失败");
                            return false;
                        }

                        // 验证消息类型
                        cJSON* type_item = cJSON_GetObjectItem(root, "type");
                        if (!type_item || !cJSON_IsString(type_item) ||
                            strcmp(type_item->valuestring, "res_sync") != 0)
                        {
                            ESP_LOGE(TAG, "不是 res_sync 消息");
                            cJSON_Delete(root);
                            return false;
                        }

                        // 解析基本字段
                        cJSON* from_item = cJSON_GetObjectItem(root, "from");
                        if (cJSON_IsString(from_item))
                        {
                            from = from_item->valuestring;
                        }

                        cJSON* to_item = cJSON_GetObjectItem(root, "to");
                        if (cJSON_IsString(to_item))
                        {
                            to = to_item->valuestring;
                        }

                        cJSON* timestamp_item = cJSON_GetObjectItem(root, "timestamp");
                        if (cJSON_IsString(timestamp_item))
                        {
                            timestamp = timestamp_item->valuestring;
                        }

                        // 解析数据字段
                        cJSON* data_item = cJSON_GetObjectItem(root, "data");
                        if (data_item && cJSON_IsObject(data_item))
                        {
                            char* data_str = cJSON_Print(data_item);
                            if (data_str)
                            {
                                data = data_str;
                                free(data_str);
                            }
                            else
                            {
                                data = "{}";
                            }
                        }
                        else
                        {
                            data = "{}";
                        }

                        cJSON_Delete(root);
                        return true;
                    }

                    bool parseError(const std::string& json, std::string& from, std::string& to,
                                    std::string& timestamp, ErrorData& error_data)
                    {
                        cJSON* root = cJSON_Parse(json.c_str());
                        if (!root)
                        {
                            ESP_LOGE(TAG, "解析 JSON 失败");
                            return false;
                        }

                        // 验证消息类型
                        cJSON* type_item = cJSON_GetObjectItem(root, "type");
                        if (!type_item || !cJSON_IsString(type_item) ||
                            strcmp(type_item->valuestring, "error") != 0)
                        {
                            ESP_LOGE(TAG, "不是 error 消息");
                            cJSON_Delete(root);
                            return false;
                        }

                        // 解析基本字段
                        cJSON* from_item = cJSON_GetObjectItem(root, "from");
                        if (cJSON_IsString(from_item))
                        {
                            from = from_item->valuestring;
                        }

                        cJSON* to_item = cJSON_GetObjectItem(root, "to");
                        if (cJSON_IsString(to_item))
                        {
                            to = to_item->valuestring;
                        }

                        cJSON* timestamp_item = cJSON_GetObjectItem(root, "timestamp");
                        if (cJSON_IsString(timestamp_item))
                        {
                            timestamp = timestamp_item->valuestring;
                        }

                        // 解析错误数据
                        cJSON* data_item = cJSON_GetObjectItem(root, "data");
                        if (data_item && cJSON_IsObject(data_item))
                        {
                            cJSON* code_item = cJSON_GetObjectItem(data_item, "code");
                            if (cJSON_IsNumber(code_item))
                            {
                                error_data.code = code_item->valueint;
                            }

                            cJSON* message_item = cJSON_GetObjectItem(data_item, "message");
                            if (cJSON_IsString(message_item))
                            {
                                error_data.message = message_item->valuestring;
                            }
                        }
                        else
                        {
                            ESP_LOGE(TAG, "缺少 data 字段");
                            cJSON_Delete(root);
                            return false;
                        }

                        cJSON_Delete(root);
                        return true;
                    }

                    MessageType getMessageType(const std::string& json)
                    {
                        cJSON* root = cJSON_Parse(json.c_str());
                        if (!root)
                        {
                            return MessageType::ERROR;
                        }

                        cJSON* type_item = cJSON_GetObjectItem(root, "type");
                        if (!type_item || !cJSON_IsString(type_item))
                        {
                            cJSON_Delete(root);
                            return MessageType::ERROR;
                        }

                        MessageType msg_type = MessageType::ERROR;
                        const char* type_str = type_item->valuestring;

                        if (strcmp(type_str, "hello") == 0)
                        {
                            msg_type = MessageType::HELLO;
                        }
                        else if (strcmp(type_str, "hello_ack") == 0)
                        {
                            msg_type = MessageType::HELLO_ACK;
                        }
                        else if (strcmp(type_str, "command") == 0)
                        {
                            msg_type = MessageType::COMMAND;
                        }
                        else if (strcmp(type_str, "res_sync") == 0)
                        {
                            msg_type = MessageType::RES_SYNC;
                        }
                        else if (strcmp(type_str, "error") == 0)
                        {
                            msg_type = MessageType::ERROR;
                        }

                        cJSON_Delete(root);
                        return msg_type;
                    }

                } // namespace message
            }     // namespace handle
        }         // namespace chatbot
    }             // namespace common
} // namespace app
