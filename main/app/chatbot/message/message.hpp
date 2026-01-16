#pragma once

#include <array>
#include <map>
#include <memory>
#include <string>

#include "cJSON.h"
#include "tool/time/time.hpp"

namespace app
{
    namespace chatbot
    {
        namespace message
        {

            /**
             * @brief 消息类型枚举
             */
            enum class MessageType
            {
                TRANSPORT_INFO, // 数据上传
                BLUETOOTH_INFO, // 蓝牙信息
                RECV_INFO,      // 数据接收
                MOV_INFO,       // 运动数据接收
                LISTEN,         // 音频监听
                PLAY,           // 音频播放
                EMOTION,        // 情绪反馈
                ERROR,          // 错误
                UNKNOWN         // 未知类型
            };

            /**
             * @brief 消息类型字符串映射
             */
            inline const char* messageTypeToString(MessageType type)
            {
                switch (type)
                {
                case MessageType::TRANSPORT_INFO:
                    return "transport_info";
                case MessageType::BLUETOOTH_INFO:
                    return "bluetooth_info";
                case MessageType::RECV_INFO:
                    return "recv_info";
                case MessageType::MOV_INFO:
                    return "mov_info";
                case MessageType::LISTEN:
                    return "listen";
                case MessageType::PLAY:
                    return "play";
                case MessageType::EMOTION:
                    return "emotion";
                case MessageType::ERROR:
                    return "error";
                default:
                    return "unknown";
                }
            }

            /**
             * @brief 从字符串获取消息类型
             */
            inline MessageType stringToMessageType(const std::string& type_str)
            {
                if (type_str == "transport_info")
                    return MessageType::TRANSPORT_INFO;
                if (type_str == "bluetooth_info")
                    return MessageType::BLUETOOTH_INFO;
                if (type_str == "recv_info")
                    return MessageType::RECV_INFO;
                if (type_str == "mov_info")
                    return MessageType::MOV_INFO;
                if (type_str == "listen")
                    return MessageType::LISTEN;
                if (type_str == "play")
                    return MessageType::PLAY;
                if (type_str == "emotion")
                    return MessageType::EMOTION;
                if (type_str == "error")
                    return MessageType::ERROR;
                return MessageType::UNKNOWN;
            }

            /**
             * @brief 基础消息结构
             */
            struct BaseMessage
            {
                MessageType type;      // 消息类型
                std::string from;      // 发送方（设备MAC地址或"server"）
                std::string to;        // 接收方（设备MAC地址或"server"）
                std::string timestamp; // ISO 8601时间戳

                BaseMessage() : type(MessageType::UNKNOWN) {}
                BaseMessage(MessageType t, const std::string& f, const std::string& t_to)
                    : type(t), from(f), to(t_to)
                {
                    timestamp = app::tool::time::iso8601Timestamp();
                }
            };

            /**
             * @brief 陀螺仪数据结构
             */
            struct GyroscopeData
            {
                double x; // X轴角速度，单位：deg/s
                double y; // Y轴角速度，单位：deg/s
                double z; // Z轴角速度，单位：deg/s

                GyroscopeData() : x(0.0), y(0.0), z(0.0) {}
                GyroscopeData(double x_val, double y_val, double z_val)
                    : x(x_val), y(y_val), z(z_val)
                {
                }
            };

            /**
             * @brief 传感器数据结构（用于transport_info）
             */
            struct SensorData
            {
                int                 touch;          // 触摸状态，0=未触摸，1=触摸
                std::array<int, 16> pressure;       // 压力传感器阵列，16个点位(0-15)，单位：Pa
                GyroscopeData       gyroscope;      // 陀螺仪数据
                float               photosensitive; // 光敏值，单位：lux

                SensorData() : touch(0), photosensitive(0)
                {
                    pressure.fill(0); // 初始化为全0
                }
            };

            /**
             * @brief 数据上传控制（用于command字段）
             *
             * 控制哪些传感器的数据需要上传到服务器，而不是控制传感器本身的使能/禁用
             */
            struct SensorEnable
            {
                bool touch;          // 是否上传触摸传感器数据
                bool pressure;       // 是否上传压力传感器数据
                bool gyroscope;      // 是否上传陀螺仪传感器数据
                bool photosensitive; // 是否上传光敏传感器数据
                bool camera;         // 是否上传摄像头数据

                SensorEnable()
                    : touch(false), pressure(false), gyroscope(false), photosensitive(false),
                      camera(false)
                {
                }

                /**
                 * @brief 将数据上传控制状态转换为5位字符串命令
                 * @return 5位字符串，如"00001"表示仅上传摄像头数据
                 */
                std::string toCommandString() const
                {
                    std::string cmd(5, '0');
                    if (touch)
                        cmd[0] = '1';
                    if (pressure)
                        cmd[1] = '1';
                    if (gyroscope)
                        cmd[2] = '1';
                    if (photosensitive)
                        cmd[3] = '1';
                    if (camera)
                        cmd[4] = '1';
                    return cmd;
                }

                /**
                 * @brief 从5位字符串命令解析数据上传控制状态
                 * @param cmd 5位字符串命令
                 * @return 是否解析成功
                 */
                bool fromCommandString(const std::string& cmd)
                {
                    if (cmd.length() != 5)
                    {
                        return false;
                    }
                    touch          = (cmd[0] == '1');
                    pressure       = (cmd[1] == '1');
                    gyroscope      = (cmd[2] == '1');
                    photosensitive = (cmd[3] == '1');
                    camera         = (cmd[4] == '1');
                    return true;
                }
            };

            /**
             * @brief 蓝牙信息数据
             */
            struct BluetoothData
            {
                int         rssi;         // 信号强度，单位：dBm
                std::string opposite_mac; // 对方设备的MAC地址

                BluetoothData() : rssi(0) {}
                BluetoothData(int rssi_val, const std::string& mac)
                    : rssi(rssi_val), opposite_mac(mac)
                {
                }
            };

            /**
             * @brief 舵机控制数据
             */
            struct ServoControl
            {
                std::string move_part; // 运动部位（h1头1, h2头2, b1身体1, b2尾巴）
                std::string
                    start_time; // 运动的起始时间，按照第一个动作开始为0时间时间轴开始，单位ms
                int angle;      // 舵机变换到指定角度，整数，范围：0-180
                int duration;   // 运动持续时间，整数，单位：毫秒

                ServoControl() : angle(0), duration(0) {}
                ServoControl(const std::string& part, const std::string& time, int a, int d)
                    : move_part(part), start_time(time), angle(a), duration(d)
                {
                }
            };

            /**
             * @brief 运动数据（舵机控制映射）
             */
            using MovementData = std::map<std::string, ServoControl>;

            /**
             * @brief 错误码枚举
             */
            enum class ErrorCode
            {
                UNKNOWN = 1000, // 未知错误
                // TODO: 后续可以扩展更多错误码
            };

            /**
             * @brief 错误数据
             */
            struct ErrorData
            {
                int         code;    // 错误码
                std::string message; // 错误描述信息

                ErrorData() : code(0) {}
                ErrorData(int c, const std::string& msg) : code(c), message(msg) {}
            };

            /**
             * @brief 情绪数据
             */
            struct EmotionData
            {
                std::string code; // 情绪代码：0开心、1伤心、2生气、3平淡、4恐惧、5惊讶、6未知

                EmotionData() : code("0") {}
                explicit EmotionData(const std::string& c) : code(c) {}
            };

            /**
             * @brief 消息基类（抽象接口）
             */
            class Message
            {
            public:
                virtual ~Message() = default;

                /**
                 * @brief 获取消息类型
                 */
                virtual MessageType getType() const = 0;

                /**
                 * @brief 序列化为JSON字符串
                 * @return JSON字符串，失败返回空字符串
                 */
                virtual std::string toJson() const = 0;

                /**
                 * @brief 从JSON字符串反序列化
                 * @param json_str JSON字符串
                 * @return 是否解析成功
                 */
                virtual bool fromJson(const std::string& json_str) = 0;

                /**
                 * @brief 获取基础消息信息
                 */
                virtual BaseMessage getBase() const = 0;

                /**
                 * @brief 设置基础消息信息
                 */
                virtual void setBase(const BaseMessage& base) = 0;
            };

            /**
             * @brief 数据上传消息 (transport_info)
             */
            class TransportInfoMessage : public Message
            {
            public:
                BaseMessage base;
                std::string command; // 5位字符串命令
                SensorData  data;    // 传感器数据

                TransportInfoMessage() {}
                TransportInfoMessage(const BaseMessage& b, const std::string& cmd,
                                     const SensorData& sensor_data)
                    : base(b), command(cmd), data(sensor_data)
                {
                }

                MessageType getType() const override
                {
                    return MessageType::TRANSPORT_INFO;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 蓝牙信息消息 (bluetooth_info)
             */
            class BluetoothInfoMessage : public Message
            {
            public:
                BaseMessage   base;
                BluetoothData data;

                BluetoothInfoMessage() {}
                BluetoothInfoMessage(const BaseMessage& b, const BluetoothData& bt_data)
                    : base(b), data(bt_data)
                {
                }

                MessageType getType() const override
                {
                    return MessageType::BLUETOOTH_INFO;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 数据接收消息 (recv_info)
             */
            class RecvInfoMessage : public Message
            {
            public:
                BaseMessage base;
                std::string command; // 5位字符串命令

                RecvInfoMessage() {}
                RecvInfoMessage(const BaseMessage& b, const std::string& cmd)
                    : base(b), command(cmd)
                {
                }

                MessageType getType() const override
                {
                    return MessageType::RECV_INFO;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 运动数据消息 (mov_info)
             */
            class MovInfoMessage : public Message
            {
            public:
                BaseMessage  base;
                MovementData data; // 舵机控制数据映射

                MovInfoMessage() {}
                MovInfoMessage(const BaseMessage& b, const MovementData& mov_data)
                    : base(b), data(mov_data)
                {
                }

                MessageType getType() const override
                {
                    return MessageType::MOV_INFO;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 音频监听消息 (listen)
             */
            class ListenMessage : public Message
            {
            public:
                BaseMessage base;

                ListenMessage() {}
                explicit ListenMessage(const BaseMessage& b) : base(b) {}

                MessageType getType() const override
                {
                    return MessageType::LISTEN;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 音频播放消息 (play)
             */
            class PlayMessage : public Message
            {
            public:
                BaseMessage base;

                PlayMessage() {}
                explicit PlayMessage(const BaseMessage& b) : base(b) {}

                MessageType getType() const override
                {
                    return MessageType::PLAY;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 错误消息 (error)
             */
            class ErrorMessage : public Message
            {
            public:
                BaseMessage base;
                ErrorData   data;

                ErrorMessage() {}
                ErrorMessage(const BaseMessage& b, const ErrorData& err_data)
                    : base(b), data(err_data)
                {
                }

                MessageType getType() const override
                {
                    return MessageType::ERROR;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 情绪反馈消息
             */
            class EmotionMessage : public Message
            {
            public:
                BaseMessage base;
                EmotionData data;

                EmotionMessage() {}
                EmotionMessage(const BaseMessage& b, const EmotionData& emotion_data)
                    : base(b), data(emotion_data)
                {
                }

                MessageType getType() const override
                {
                    return MessageType::EMOTION;
                }

                std::string toJson() const override;
                bool        fromJson(const std::string& json_str) override;

                BaseMessage getBase() const override
                {
                    return base;
                }

                void setBase(const BaseMessage& b) override
                {
                    base = b;
                }
            };

            /**
             * @brief 消息工厂类（支持可扩展的消息创建和解析）
             */
            class MessageFactory
            {
            public:
                /**
                 * @brief 从JSON字符串创建消息对象
                 * @param json_str JSON字符串
                 * @return 消息对象指针，失败返回nullptr
                 */
                static std::unique_ptr<Message> createFromJson(const std::string& json_str);

                /**
                 * @brief 创建指定类型的空消息对象
                 * @param type 消息类型
                 * @return 消息对象指针
                 */
                static std::unique_ptr<Message> create(MessageType type);

                /**
                 * @brief 获取消息类型（从JSON字符串）
                 * @param json_str JSON字符串
                 * @return 消息类型
                 */
                static MessageType getMessageType(const std::string& json_str);

                /**
                 * @brief 解析基础消息字段
                 * @param root JSON根对象
                 * @param base 输出的基础消息结构
                 * @return 是否解析成功
                 */
                static bool parseBase(cJSON* root, BaseMessage& base);
            };

            /**
             * @brief 获取设备MAC地址（格式：xx:xx:xx:xx:xx:xx）
             * @return MAC地址字符串
             */
            std::string getDeviceMacAddress();

        } // namespace message
    } // namespace chatbot
} // namespace app
