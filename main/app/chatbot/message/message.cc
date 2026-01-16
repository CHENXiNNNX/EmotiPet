#include "message.hpp"

#include <cstring>

#include "esp_log.h"
#include "esp_wifi.h"
#include "tool/ota/ota.hpp"

static const char* const TAG = "Message";

namespace app
{
    namespace chatbot
    {
        namespace message
        {

            // ========== TransportInfoMessage 实现 ==========

            std::string TransportInfoMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());
                cJSON_AddStringToObject(json.get(), "command", command.c_str());

                // data 对象
                cJSON* data_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(data_obj, "touch", data.touch);

                // pressure 数组（16个点位）
                cJSON* pressure_array = cJSON_CreateArray();
                for (size_t i = 0; i < 16; i++)
                {
                    cJSON_AddItemToArray(pressure_array, cJSON_CreateNumber(data.pressure[i]));
                }
                cJSON_AddItemToObject(data_obj, "pressure", pressure_array);

                // gyroscope 对象
                cJSON* gyro_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(gyro_obj, "x", data.gyroscope.x);
                cJSON_AddNumberToObject(gyro_obj, "y", data.gyroscope.y);
                cJSON_AddNumberToObject(gyro_obj, "z", data.gyroscope.z);
                cJSON_AddItemToObject(data_obj, "gyroscope", gyro_obj);

                cJSON_AddNumberToObject(data_obj, "photosensitive", data.photosensitive);
                cJSON_AddItemToObject(json.get(), "data", data_obj);

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 transport_info 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool TransportInfoMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::TRANSPORT_INFO)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                // 解析 command
                cJSON* command_item = cJSON_GetObjectItem(root.get(), "command");
                if (!command_item || !cJSON_IsString(command_item))
                {
                    ESP_LOGE(TAG, "缺少 command 字段或类型错误");
                    return false;
                }
                command = cJSON_GetStringValue(command_item);

                // 解析 data
                cJSON* data_item = cJSON_GetObjectItem(root.get(), "data");
                if (!data_item || !cJSON_IsObject(data_item))
                {
                    ESP_LOGE(TAG, "缺少 data 字段或类型错误");
                    return false;
                }

                // 解析传感器数据
                cJSON* touch_item = cJSON_GetObjectItem(data_item, "touch");
                if (touch_item && cJSON_IsNumber(touch_item))
                {
                    data.touch = (int)cJSON_GetNumberValue(touch_item);
                }

                cJSON* pressure_item = cJSON_GetObjectItem(data_item, "pressure");
                if (pressure_item && cJSON_IsArray(pressure_item))
                {
                    int array_size = cJSON_GetArraySize(pressure_item);
                    int copy_size  = (array_size < 16) ? array_size : 16;
                    for (int i = 0; i < copy_size; i++)
                    {
                        cJSON* elem = cJSON_GetArrayItem(pressure_item, i);
                        if (elem && cJSON_IsNumber(elem))
                        {
                            data.pressure[i] = (int)cJSON_GetNumberValue(elem);
                        }
                    }
                    // 如果数组长度不足16，剩余元素保持为0（已在构造函数中初始化）
                }

                cJSON* gyro_item = cJSON_GetObjectItem(data_item, "gyroscope");
                if (gyro_item && cJSON_IsObject(gyro_item))
                {
                    cJSON* x_item = cJSON_GetObjectItem(gyro_item, "x");
                    if (x_item && cJSON_IsNumber(x_item))
                    {
                        data.gyroscope.x = cJSON_GetNumberValue(x_item);
                    }

                    cJSON* y_item = cJSON_GetObjectItem(gyro_item, "y");
                    if (y_item && cJSON_IsNumber(y_item))
                    {
                        data.gyroscope.y = cJSON_GetNumberValue(y_item);
                    }

                    cJSON* z_item = cJSON_GetObjectItem(gyro_item, "z");
                    if (z_item && cJSON_IsNumber(z_item))
                    {
                        data.gyroscope.z = cJSON_GetNumberValue(z_item);
                    }
                }

                cJSON* photosensitive_item = cJSON_GetObjectItem(data_item, "photosensitive");
                if (photosensitive_item && cJSON_IsNumber(photosensitive_item))
                {
                    data.photosensitive = (int)cJSON_GetNumberValue(photosensitive_item);
                }

                return true;
            }

            // ========== BluetoothInfoMessage 实现 ==========

            std::string BluetoothInfoMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());

                // data 对象
                cJSON* data_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(data_obj, "rssi", data.rssi);
                cJSON_AddStringToObject(data_obj, "opposite_mac", data.opposite_mac.c_str());
                cJSON_AddItemToObject(json.get(), "data", data_obj);

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 bluetooth_info 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool BluetoothInfoMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::BLUETOOTH_INFO)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                // 解析 data
                cJSON* data_item = cJSON_GetObjectItem(root.get(), "data");
                if (!data_item || !cJSON_IsObject(data_item))
                {
                    ESP_LOGE(TAG, "缺少 data 字段或类型错误");
                    return false;
                }

                cJSON* rssi_item = cJSON_GetObjectItem(data_item, "rssi");
                if (rssi_item && cJSON_IsNumber(rssi_item))
                {
                    data.rssi = (int)cJSON_GetNumberValue(rssi_item);
                }

                cJSON* mac_item = cJSON_GetObjectItem(data_item, "opposite_mac");
                if (mac_item && cJSON_IsString(mac_item))
                {
                    data.opposite_mac = cJSON_GetStringValue(mac_item);
                }

                return true;
            }

            // ========== RecvInfoMessage 实现 ==========

            std::string RecvInfoMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());
                cJSON_AddStringToObject(json.get(), "command", command.c_str());

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 recv_info 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool RecvInfoMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::RECV_INFO)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                // 解析 command
                cJSON* command_item = cJSON_GetObjectItem(root.get(), "command");
                if (!command_item || !cJSON_IsString(command_item))
                {
                    ESP_LOGE(TAG, "缺少 command 字段或类型错误");
                    return false;
                }
                command = cJSON_GetStringValue(command_item);

                return true;
            }

            // ========== MovInfoMessage 实现 ==========

            std::string MovInfoMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());

                // data 对象（舵机控制映射）
                cJSON* data_obj = cJSON_CreateObject();
                for (const auto& pair : data)
                {
                    const std::string&  servo_name = pair.first;
                    const ServoControl& servo_ctrl = pair.second;

                    cJSON* servo_obj = cJSON_CreateObject();
                    cJSON_AddStringToObject(servo_obj, "move_part", servo_ctrl.move_part.c_str());
                    cJSON_AddStringToObject(servo_obj, "start_time", servo_ctrl.start_time.c_str());
                    cJSON_AddNumberToObject(servo_obj, "angle", servo_ctrl.angle);
                    cJSON_AddNumberToObject(servo_obj, "duration", servo_ctrl.duration);
                    cJSON_AddItemToObject(data_obj, servo_name.c_str(), servo_obj);
                }
                cJSON_AddItemToObject(json.get(), "data", data_obj);

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 mov_info 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool MovInfoMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::MOV_INFO)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                // 解析 data
                cJSON* data_item = cJSON_GetObjectItem(root.get(), "data");
                if (!data_item || !cJSON_IsObject(data_item))
                {
                    ESP_LOGE(TAG, "缺少 data 字段或类型错误");
                    return false;
                }

                // 清空现有数据
                data.clear();

                // 遍历所有舵机
                cJSON* servo_item = nullptr;
                cJSON_ArrayForEach(servo_item, data_item)
                {
                    if (!cJSON_IsObject(servo_item))
                    {
                        continue;
                    }

                    const char* servo_name = servo_item->string;
                    if (!servo_name)
                    {
                        continue;
                    }

                    ServoControl servo_ctrl;

                    cJSON* move_part_item = cJSON_GetObjectItem(servo_item, "move_part");
                    if (!move_part_item || !cJSON_IsString(move_part_item))
                    {
                        ESP_LOGE(TAG, "舵机 %s 缺少 move_part 字段或类型错误", servo_name);
                        continue;
                    }
                    servo_ctrl.move_part = cJSON_GetStringValue(move_part_item);

                    cJSON* start_time_item = cJSON_GetObjectItem(servo_item, "start_time");
                    if (!start_time_item || !cJSON_IsString(start_time_item))
                    {
                        ESP_LOGE(TAG, "舵机 %s 缺少 start_time 字段或类型错误", servo_name);
                        continue;
                    }
                    servo_ctrl.start_time = cJSON_GetStringValue(start_time_item);

                    cJSON* angle_item = cJSON_GetObjectItem(servo_item, "angle");
                    if (!angle_item || !cJSON_IsNumber(angle_item))
                    {
                        ESP_LOGE(TAG, "舵机 %s 缺少 angle 字段或类型错误", servo_name);
                        continue;
                    }
                    servo_ctrl.angle = (int)cJSON_GetNumberValue(angle_item);

                    cJSON* duration_item = cJSON_GetObjectItem(servo_item, "duration");
                    if (!duration_item || !cJSON_IsNumber(duration_item))
                    {
                        ESP_LOGE(TAG, "舵机 %s 缺少 duration 字段或类型错误", servo_name);
                        continue;
                    }
                    servo_ctrl.duration = (int)cJSON_GetNumberValue(duration_item);

                    data[servo_name] = servo_ctrl;
                }

                if (data.empty())
                {
                    ESP_LOGE(TAG, "没有有效的舵机控制数据");
                    return false;
                }

                return true;
            }

            // ========== ListenMessage 实现 ==========

            std::string ListenMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 listen 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool ListenMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::LISTEN)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                return true;
            }

            // ========== PlayMessage 实现 ==========

            std::string PlayMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 play 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool PlayMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::PLAY)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                return true;
            }

            // ========== ErrorMessage 实现 ==========

            std::string ErrorMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());

                // data 对象
                cJSON* data_obj = cJSON_CreateObject();
                cJSON_AddNumberToObject(data_obj, "code", data.code);
                cJSON_AddStringToObject(data_obj, "message", data.message.c_str());
                cJSON_AddItemToObject(json.get(), "data", data_obj);

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 error 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool ErrorMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::ERROR)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                // 解析 data
                cJSON* data_item = cJSON_GetObjectItem(root.get(), "data");
                if (!data_item || !cJSON_IsObject(data_item))
                {
                    ESP_LOGE(TAG, "缺少 data 字段或类型错误，必须为对象格式");
                    return false;
                }

                cJSON* code_item = cJSON_GetObjectItem(data_item, "code");
                if (!code_item || !cJSON_IsNumber(code_item))
                {
                    ESP_LOGE(TAG, "缺少 code 字段或类型错误，必须为数字");
                    return false;
                }
                data.code = (int)cJSON_GetNumberValue(code_item);

                cJSON* message_item = cJSON_GetObjectItem(data_item, "message");
                if (!message_item || !cJSON_IsString(message_item))
                {
                    ESP_LOGE(TAG, "缺少 message 字段或类型错误，必须为字符串");
                    return false;
                }
                data.message = cJSON_GetStringValue(message_item);

                return true;
            }

            std::string EmotionMessage::toJson() const
            {
                using namespace app::tool::ota;
                JsonRAII json;

                // 基础字段
                cJSON_AddStringToObject(json.get(), "type", messageTypeToString(base.type));
                cJSON_AddStringToObject(json.get(), "from", base.from.c_str());
                cJSON_AddStringToObject(json.get(), "to", base.to.c_str());
                cJSON_AddStringToObject(json.get(), "timestamp", base.timestamp.c_str());

                // code 字段
                cJSON_AddStringToObject(json.get(), "code", data.code.c_str());

                JsonStringRAII json_str(cJSON_Print(json.get()));
                if (!json_str.get())
                {
                    ESP_LOGE(TAG, "构建 emotion 消息失败");
                    return "";
                }

                return std::string(json_str.get());
            }

            bool EmotionMessage::fromJson(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                // 解析基础字段
                if (!MessageFactory::parseBase(root.get(), base))
                {
                    return false;
                }

                // 验证类型
                if (base.type != MessageType::EMOTION)
                {
                    ESP_LOGE(TAG, "消息类型不匹配");
                    return false;
                }

                // 解析 code 字段
                cJSON* code_item = cJSON_GetObjectItem(root.get(), "code");
                if (code_item && cJSON_IsString(code_item))
                {
                    data.code = cJSON_GetStringValue(code_item);
                }
                else
                {
                    // 设置默认值
                    data.code = "0";
                }

                return true;
            }

            // ========== MessageFactory 实现 ==========

            std::unique_ptr<Message> MessageFactory::createFromJson(const std::string& json_str)
            {
                MessageType type = getMessageType(json_str);
                if (type == MessageType::UNKNOWN)
                {
                    ESP_LOGE(TAG, "无法识别消息类型");
                    return nullptr;
                }

                std::unique_ptr<Message> msg = create(type);
                if (!msg)
                {
                    return nullptr;
                }

                if (!msg->fromJson(json_str))
                {
                    ESP_LOGE(TAG, "消息解析失败");
                    return nullptr;
                }

                return msg;
            }

            std::unique_ptr<Message> MessageFactory::create(MessageType type)
            {
                switch (type)
                {
                case MessageType::TRANSPORT_INFO:
                    return std::make_unique<TransportInfoMessage>();
                case MessageType::BLUETOOTH_INFO:
                    return std::make_unique<BluetoothInfoMessage>();
                case MessageType::RECV_INFO:
                    return std::make_unique<RecvInfoMessage>();
                case MessageType::MOV_INFO:
                    return std::make_unique<MovInfoMessage>();
                case MessageType::LISTEN:
                    return std::make_unique<ListenMessage>();
                case MessageType::PLAY:
                    return std::make_unique<PlayMessage>();
                case MessageType::EMOTION:
                    return std::make_unique<EmotionMessage>();
                case MessageType::ERROR:
                    return std::make_unique<ErrorMessage>();
                default:
                    return nullptr;
                }
            }

            MessageType MessageFactory::getMessageType(const std::string& json_str)
            {
                using namespace app::tool::ota;
                JsonRAII root(json_str.c_str());
                if (!root.get())
                {
                    return MessageType::UNKNOWN;
                }

                cJSON* type_item = cJSON_GetObjectItem(root.get(), "type");
                if (!type_item || !cJSON_IsString(type_item))
                {
                    return MessageType::UNKNOWN;
                }

                const char* type_str = cJSON_GetStringValue(type_item);
                return stringToMessageType(type_str);
            }

            bool MessageFactory::parseBase(cJSON* root, BaseMessage& base)
            {
                if (!root)
                {
                    return false;
                }

                // 解析 type
                cJSON* type_item = cJSON_GetObjectItem(root, "type");
                if (!type_item || !cJSON_IsString(type_item))
                {
                    ESP_LOGE(TAG, "缺少 type 字段或类型错误");
                    return false;
                }
                base.type = stringToMessageType(cJSON_GetStringValue(type_item));

                // 解析 from
                cJSON* from_item = cJSON_GetObjectItem(root, "from");
                if (from_item && cJSON_IsString(from_item))
                {
                    base.from = cJSON_GetStringValue(from_item);
                }

                // 解析 to
                cJSON* to_item = cJSON_GetObjectItem(root, "to");
                if (to_item && cJSON_IsString(to_item))
                {
                    base.to = cJSON_GetStringValue(to_item);
                }

                // 解析 timestamp
                cJSON* timestamp_item = cJSON_GetObjectItem(root, "timestamp");
                if (timestamp_item && cJSON_IsString(timestamp_item))
                {
                    base.timestamp = cJSON_GetStringValue(timestamp_item);
                }
                else
                {
                    // 如果没有提供timestamp，使用当前时间
                    base.timestamp = app::tool::time::iso8601Timestamp();
                }

                return true;
            }

            // ========== 工具函数 ==========

            std::string getDeviceMacAddress()
            {
                uint8_t   mac[6];
                esp_err_t ret = esp_wifi_get_mac(WIFI_IF_STA, mac);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "获取MAC地址失败");
                    return "";
                }

                char mac_str[18];
                snprintf(mac_str, sizeof(mac_str), "%02x:%02x:%02x:%02x:%02x:%02x", mac[0], mac[1],
                         mac[2], mac[3], mac[4], mac[5]);
                return std::string(mac_str);
            }

        } // namespace message
    } // namespace chatbot
} // namespace app
