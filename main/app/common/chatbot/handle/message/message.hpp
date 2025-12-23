#pragma once

#include <cstdint>
#include <string>

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

                    /**
                     * @brief 消息类型
                     */
                    enum class MessageType
                    {
                        HELLO,     // hello 消息（设备端发送）
                        HELLO_ACK, // hello 应答（服务器发送）
                        COMMAND,   // 命令消息（服务器发送）
                        RES_SYNC,  // 资源同步（双向）
                        ERROR      // 错误消息（双向）
                    };

                    /**
                     * @brief 音频参数配置
                     */
                    struct AudioParams
                    {
                        std::string format         = "opus"; // 音频格式
                        int         sample_rate    = 16000;  // 采样率
                        int         channels       = 1;      // 声道数
                        int         frame_duration = 60;     // 帧持续时间（毫秒）
                    };

                    /**
                     * @brief 功能特性配置
                     */
                    struct Features
                    {
                        bool aec = false; // 云端 AEC
                        bool mcp = false; // MCP 协议支持
                    };

                    /**
                     * @brief 命令数据
                     */
                    struct CommandData
                    {
                        std::string cmd;      // 命令类型，如 "play_sound"
                        std::string sound_id; // 音频 ID
                        std::string reason;   // 使用原因
                    };

                    /**
                     * @brief 错误数据
                     */
                    struct ErrorData
                    {
                        int         code;    // 错误码
                        std::string message; // 错误描述
                    };

                    /**
                     * @brief 构建 hello 消息（设备端发送）
                     * @param device_id 设备 MAC 地址
                     * @param client_id 设备 UUID
                     * @param features 功能特性
                     * @param audio_params 音频参数
                     * @return JSON 字符串
                     */
                    std::string buildHelloMessage(const std::string& device_id,
                                                  const std::string& client_id,
                                                  const Features&    features,
                                                  const AudioParams& audio_params);

                    /**
                     * @brief 构建资源同步消息（设备端发送）
                     * @param device_id 设备 MAC 地址
                     * @param data 同步数据（JSON 对象字符串，可选）
                     * @return JSON 字符串
                     */
                    std::string buildResSyncMessage(const std::string& device_id,
                                                    const std::string& data = "{}");

                    /**
                     * @brief 构建错误消息（设备端发送）
                     * @param device_id 设备 MAC 地址
                     * @param error_code 错误码
                     * @param error_message 错误描述
                     * @return JSON 字符串
                     */
                    std::string buildErrorMessage(const std::string& device_id, int error_code,
                                                  const std::string& error_message);

                    /**
                     * @brief 解析 hello_ack 消息（服务器发送）
                     * @param json JSON 字符串
                     * @param features 输出功能特性
                     * @return 是否解析成功
                     */
                    bool parseHelloAck(const std::string& json, Features& features);

                    /**
                     * @brief 解析命令消息（服务器发送）
                     * @param json JSON 字符串
                     * @param from 输出发送者
                     * @param to 输出接收者
                     * @param timestamp 输出时间戳
                     * @param cmd_data 输出命令数据
                     * @return 是否解析成功
                     */
                    bool parseCommand(const std::string& json, std::string& from, std::string& to,
                                      std::string& timestamp, CommandData& cmd_data);

                    /**
                     * @brief 解析资源同步消息（服务器发送）
                     * @param json JSON 字符串
                     * @param from 输出发送者
                     * @param to 输出接收者
                     * @param timestamp 输出时间戳
                     * @param data 输出同步数据（JSON 对象字符串）
                     * @return 是否解析成功
                     */
                    bool parseResSync(const std::string& json, std::string& from, std::string& to,
                                      std::string& timestamp, std::string& data);

                    /**
                     * @brief 解析错误消息（服务器发送）
                     * @param json JSON 字符串
                     * @param from 输出发送者
                     * @param to 输出接收者
                     * @param timestamp 输出时间戳
                     * @param error_data 输出错误数据
                     * @return 是否解析成功
                     */
                    bool parseError(const std::string& json, std::string& from, std::string& to,
                                    std::string& timestamp, ErrorData& error_data);

                    /**
                     * @brief 获取消息类型
                     * @param json JSON 字符串
                     * @return 消息类型，解析失败返回错误消息类型
                     */
                    MessageType getMessageType(const std::string& json);

                } // namespace message
            }     // namespace handle
        }         // namespace chatbot
    }             // namespace common
} // namespace app
