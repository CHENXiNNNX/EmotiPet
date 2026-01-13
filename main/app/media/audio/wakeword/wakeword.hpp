#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <model_path.h>

#include "system/event/event.hpp"

namespace app
{
    namespace media
    {
        namespace audio
        {
            namespace wakeword
            {
                // 事件定义
                extern const char* WAKEWORD_EVENT_BASE;

                enum WakeWordEventId : int32_t
                {
                    WAKEWORD_EVENT_DETECTED = 0,
                    WAKEWORD_EVENT_STARTED,
                    WAKEWORD_EVENT_STOPPED,
                };

                struct WakeWordEventData
                {
                    char  text[64];
                    char  command[64];
                    char  action[32];
                    float probability;
                };

                /**
                 * @brief 唤醒词检测抽象接口
                 */
                class WakeWord
                {
                public:
                    virtual ~WakeWord() = default;

                    /**
                     * @brief 初始化唤醒词检测器
                     * @param models_list SR 模型列表（从 Assets 加载）
                     * @param sample_rate 音频采样率
                     * @param channels 音频通道数
                     * @return true 成功, false 失败
                     */
                    virtual bool init(srmodel_list_t* models_list, int sample_rate,
                                      int channels) = 0;

                    /**
                     * @brief 动态添加命令词（唤醒词）
                     * @param command 命令词拼音（如 "ni hao xiao zhi"）
                     * @param text 显示文本（如 "你好小智"）
                     * @param action 动作类型（默认 "wake"）
                     * @return true 成功, false 失败
                     */
                    virtual bool addCommand(const std::string& command, const std::string& text,
                                            const std::string& action = "wake") = 0;

                    /**
                     * @brief 删除命令词
                     * @param text 显示文本（如 "你好小智"）
                     * @return true 成功, false 未找到
                     */
                    virtual bool removeCommand(const std::string& text) = 0;

                    /**
                     * @brief 清除当前语言的所有命令词
                     */
                    virtual void clearCommands() = 0;

                    /**
                     * @brief 切换语言模型
                     * @param language 语言代码（"cn" 或 "en"）
                     * @return true 成功, false 失败
                     */
                    virtual bool switchModel(const std::string& language) = 0;

                    /**
                     * @brief 喂入音频数据进行检测
                     * @param data PCM 音频数据（int16_t）
                     */
                    virtual void feed(const std::vector<int16_t>& data) = 0;

                    /**
                     * @brief 启动唤醒词检测
                     */
                    virtual void start() = 0;

                    /**
                     * @brief 停止唤醒词检测
                     */
                    virtual void stop() = 0;

                    /**
                     * @brief 检查是否正在运行
                     */
                    virtual bool isRunning() const = 0;

                    /**
                     * @brief 获取每次 feed 所需的数据大小（采样点数）
                     * @return 采样点数
                     */
                    virtual size_t getFeedSize() const = 0;

                    /**
                     * @brief 获取最后检测到的唤醒词
                     * @return 唤醒词文本
                     */
                    virtual const std::string& getLastDetectedWakeWord() const = 0;
                };

            } // namespace wakeword
        } // namespace audio
    } // namespace media
} // namespace app
