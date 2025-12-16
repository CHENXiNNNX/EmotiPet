#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include <model_path.h>

namespace app
{
    namespace media
    {
        namespace audio
        {
            namespace wakeword
            {
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
                     * @brief 喂入音频数据进行检测
                     * @param data PCM 音频数据（int16_t）
                     */
                    virtual void feed(const std::vector<int16_t>& data) = 0;

                    /**
                     * @brief 设置唤醒词检测回调
                     * @param callback 检测到唤醒词时的回调函数，参数为唤醒词文本
                     */
                    virtual void setWakeWordDetected(
                        std::function<void(const std::string& wake_word)> callback) = 0;

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
        }     // namespace audio
    }         // namespace media
} // namespace app

