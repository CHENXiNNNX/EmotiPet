#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace app
{
    namespace sys
    {
        namespace task
        {

            /**
             * @brief 任务优先级枚举
             */
            enum class Priority : UBaseType_t
            {
                IDLE     = 0,  // 空闲任务优先级
                LOW      = 1,  // 低优先级
                NORMAL   = 5,  // 普通优先级
                HIGH     = 10, // 高优先级
                REALTIME = 15  // 实时优先级（最高）
            };

            /**
             * @brief 任务状态枚举
             */
            enum class State
            {
                RUNNING,   // 运行中
                READY,     // 就绪
                BLOCKED,   // 阻塞
                SUSPENDED, // 挂起
                DELETED    // 已删除
            };

            /**
             * @brief 任务配置结构体
             */
            struct Config
            {
                const char* name;       // 任务名称
                size_t      stack_size; // 栈大小（字节）
                Priority    priority;   // 优先级
                BaseType_t  core_id;    // 核心 ID（-1 表示不绑定核心，0 或 1 绑定到指定核心）
                uint32_t    delay_ms;   // 启动延迟（毫秒）

                /**
                 * @brief 默认构造函数
                 * @note 默认栈大小 4096 字节，适合中等复杂度的任务
                 *       对于简单任务，建议使用 createLightweight() 或手动设置更小的 stack_size
                 */
                Config()
                    : name("Task"), stack_size(4096), priority(Priority::NORMAL), core_id(-1),
                      delay_ms(0)
                {
                }

                /**
                 * @brief 创建轻量级任务配置（栈大小 2048 字节）
                 * @param name 任务名称
                 * @param priority 优先级（默认 NORMAL）
                 * @return 轻量级任务配置
                 * @note 适合简单任务，如定时器回调、简单数据处理等
                 */
                static Config createLightweight(const char* name,
                                                Priority    priority = Priority::NORMAL)
                {
                    Config config;
                    config.name       = name;
                    config.stack_size = 2048; // 轻量级任务使用 2KB 栈
                    config.priority   = priority;
                    return config;
                }

                /**
                 * @brief 创建小型任务配置（栈大小 1024 字节）
                 * @param name 任务名称
                 * @param priority 优先级（默认 LOW）
                 * @return 小型任务配置
                 * @note 适合非常简单的任务，如 LED 闪烁、简单状态机等
                 *       注意：栈空间较小，避免深度递归或大局部变量
                 */
                static Config createSmall(const char* name, Priority priority = Priority::LOW)
                {
                    Config config;
                    config.name       = name;
                    config.stack_size = 1024; // 小型任务使用 1KB 栈
                    config.priority   = priority;
                    return config;
                }

                /**
                 * @brief 创建大型任务配置（栈大小 8192 字节）
                 * @param name 任务名称
                 * @param priority 优先级（默认 NORMAL）
                 * @return 大型任务配置
                 * @note 适合复杂任务，如网络处理、音频处理、图像处理等
                 */
                static Config createLarge(const char* name, Priority priority = Priority::NORMAL)
                {
                    Config config;
                    config.name       = name;
                    config.stack_size = 8192; // 大型任务使用 8KB 栈
                    config.priority   = priority;
                    return config;
                }
            };

            /**
             * @brief 任务信息结构体
             */
            struct Info
            {
                const char* name;            // 任务名称
                State       state;           // 任务状态
                UBaseType_t priority;        // 优先级
                uint32_t    runtime_percent; // CPU 运行时间百分比（如果启用统计）

                Info() : name(nullptr), state(State::DELETED), priority(0), runtime_percent(0) {}
            };

            /**
             * @brief 任务类
             *
             * 封装 FreeRTOS 任务，提供任务生命周期管理
             */
            class Task
            {
            public:
                using TaskFunction = std::function<void(void*)>; // 任务函数类型

                /**
                 * @brief 构造函数
                 * @param function 任务函数
                 * @param config 任务配置
                 * @param param 传递给任务函数的参数
                 */
                Task(TaskFunction function, const Config& config, void* param = nullptr);

                /**
                 * @brief 析构函数
                 * @note 不会自动删除任务，如需删除任务请显式调用 destroy()
                 */
                ~Task();

                // 禁止拷贝和移动
                Task(const Task&)            = delete;
                Task& operator=(const Task&) = delete;
                Task(Task&&)                 = delete;
                Task& operator=(Task&&)      = delete;

                /**
                 * @brief 启动任务
                 * @return 成功返回 true，失败返回 false
                 * @note delay_ms 配置项表示调用 start() 的任务延迟，而非新任务的启动延迟
                 */
                bool start();

                /**
                 * @brief 删除任务
                 * @note 任务也可以在自己的函数中通过 return 或 vTaskDelete(nullptr) 自行结束
                 */
                void destroy();

                /**
                 * @brief 挂起任务
                 */
                void suspend();

                /**
                 * @brief 恢复任务
                 */
                void resume();

                /**
                 * @brief 设置任务优先级
                 * @param priority 新优先级
                 */
                void setPriority(Priority priority);

                /**
                 * @brief 获取任务优先级
                 * @return 任务优先级
                 */
                Priority getPriority() const;

                /**
                 * @brief 获取任务句柄
                 * @return FreeRTOS 任务句柄
                 */
                TaskHandle_t getHandle() const
                {
                    return handle_;
                }

                /**
                 * @brief 获取任务名称
                 * @return 任务名称
                 */
                const char* getName() const
                {
                    return config_.name;
                }

                /**
                 * @brief 获取任务信息
                 * @return 任务信息结构体
                 */
                Info getInfo() const;

                /**
                 * @brief 检查任务是否有效
                 * @return 有效返回 true
                 */
                bool isValid() const
                {
                    return handle_ != nullptr;
                }

            private:
                static void taskWrapper(void* param);

                TaskFunction function_;
                Config       config_;
                void*        param_;
                TaskHandle_t handle_;
                bool         started_;
            };

            /**
             * @brief 任务管理器类
             *
             * 提供任务查询、监控等功能
             */
            class TaskManager
            {
            public:
                /**
                 * @brief 获取任务管理器实例
                 * @return 任务管理器引用
                 */
                static TaskManager& getInstance();

                /**
                 * @brief 根据名称查找任务
                 * @param name 任务名称
                 * @return 任务句柄，未找到返回 nullptr
                 */
                TaskHandle_t findTask(const char* name);

                /**
                 * @brief 获取任务信息
                 * @param handle 任务句柄
                 * @return 任务信息，无效句柄返回空信息
                 */
                Info getTaskInfo(TaskHandle_t handle);

                /**
                 * @brief 获取当前任务信息
                 * @return 当前任务信息
                 */
                Info getCurrentTaskInfo();

                /**
                 * @brief 获取系统任务数量
                 * @return 任务数量
                 */
                uint32_t getTaskCount();

                /**
                 * @brief 延迟当前任务（毫秒）
                 * @param ms 延迟时间（毫秒）
                 */
                static void delayMs(uint32_t ms);

                /**
                 * @brief 延迟当前任务（微秒）
                 * @param us 延迟时间（微秒）
                 */
                static void delayUs(uint32_t us);

                /**
                 * @brief 获取系统 tick 计数
                 * @return 当前系统 tick 计数
                 */
                static uint32_t getTickCount();

            private:
                TaskManager()                              = default;
                ~TaskManager()                             = default;
                TaskManager(const TaskManager&)            = delete;
                TaskManager& operator=(const TaskManager&) = delete;
            };

        } // namespace task
    } // namespace sys
} // namespace app
