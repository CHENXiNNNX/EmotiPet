#include "task.hpp"

#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>

#include "esp_cpu.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "info.hpp"

static const char* const TAG = "TASK";

namespace app
{
    namespace sys
    {
        namespace task
        {

            struct TaskStartParam
            {
                Task::TaskFunction function;
                void*              user_param;
            };

            static std::unordered_map<TaskHandle_t, std::unique_ptr<TaskStartParam>> s_task_params;
            static std::mutex s_task_params_mutex;

            Task::Task(TaskFunction function, const Config& config, void* param)
                : function_(function), config_(config), param_(param), handle_(nullptr),
                  started_(false)
            {
            }

            Task::~Task() = default;

            void Task::taskWrapper(void* param)
            {
                auto* p = static_cast<TaskStartParam*>(param);
                if (p && p->function)
                {
                    p->function(p->user_param);
                }

                TaskHandle_t self = xTaskGetCurrentTaskHandle();
                {
                    std::lock_guard<std::mutex> lock(s_task_params_mutex);
                    s_task_params.erase(self);
                }
                vTaskDelete(nullptr);
            }

            bool Task::start()
            {
                if (started_ || handle_ != nullptr)
                {
                    ESP_LOGW(TAG, "任务 %s 已启动", config_.name);
                    return false;
                }

                if (!function_)
                {
                    ESP_LOGE(TAG, "任务函数为空");
                    return false;
                }

                auto start_param =
                    std::make_unique<TaskStartParam>(TaskStartParam{function_, param_});

                if (config_.delay_ms > 0)
                {
                    vTaskDelay(pdMS_TO_TICKS(config_.delay_ms));
                }

                // 使用 .get() 获取原始指针，因为 FreeRTOS API 需要原始指针
                BaseType_t result;
                if (config_.core_id == -1)
                {
                    result = xTaskCreate(
                        taskWrapper, config_.name, config_.stack_size / sizeof(StackType_t),
                        start_param.get(), static_cast<UBaseType_t>(config_.priority), &handle_);
                }
                else
                {
                    result = xTaskCreatePinnedToCore(
                        taskWrapper, config_.name, config_.stack_size / sizeof(StackType_t),
                        start_param.get(), static_cast<UBaseType_t>(config_.priority), &handle_,
                        config_.core_id);
                }

                if (result != pdPASS)
                {
                    ESP_LOGE(TAG, "创建任务 %s 失败", config_.name);
                    return false;
                }

                vTaskSuspend(handle_);

                {
                    std::lock_guard<std::mutex> lock(s_task_params_mutex);
                    // 转移所有权到 map
                    s_task_params[handle_] = std::move(start_param);
                }

                vTaskResume(handle_);

                started_ = true;
                return true;
            }

            void Task::destroy()
            {
                if (handle_ != nullptr)
                {
                    vTaskDelete(handle_);

                    {
                        std::lock_guard<std::mutex> lock(s_task_params_mutex);
                        s_task_params.erase(handle_);
                    }

                    handle_  = nullptr;
                    started_ = false;
                }
            }

            void Task::suspend()
            {
                if (handle_ != nullptr)
                {
                    vTaskSuspend(handle_);
                }
            }

            void Task::resume()
            {
                if (handle_ != nullptr)
                {
                    vTaskResume(handle_);
                }
            }

            void Task::setPriority(Priority priority)
            {
                if (handle_ != nullptr)
                {
                    vTaskPrioritySet(handle_, static_cast<UBaseType_t>(priority));
                    config_.priority = priority;
                }
            }

            Priority Task::getPriority() const
            {
                if (handle_ != nullptr)
                {
                    return static_cast<Priority>(uxTaskPriorityGet(handle_));
                }
                return config_.priority;
            }

            Info Task::getInfo() const
            {
                Info info;
                if (handle_ != nullptr)
                {
                    info.name     = config_.name;
                    info.priority = uxTaskPriorityGet(handle_);

                    eTaskState state = eTaskGetState(handle_);
                    switch (state)
                    {
                    case eRunning:
                        info.state = State::RUNNING;
                        break;
                    case eReady:
                        info.state = State::READY;
                        break;
                    case eBlocked:
                        info.state = State::BLOCKED;
                        break;
                    case eSuspended:
                        info.state = State::SUSPENDED;
                        break;
                    case eDeleted:
                        info.state = State::DELETED;
                        break;
                    default:
                        info.state = State::DELETED;
                        break;
                    }
                }
                else
                {
                    info.name     = config_.name;
                    info.priority = static_cast<UBaseType_t>(config_.priority);
                    info.state    = State::DELETED;
                }
                return info;
            }

            TaskManager& TaskManager::getInstance()
            {
                static TaskManager instance;
                return instance;
            }

            TaskHandle_t TaskManager::findTask(const char* name)
            {
                return xTaskGetHandle(name);
            }

            Info TaskManager::getTaskInfo(TaskHandle_t handle)
            {
                Info info;
                if (handle == nullptr)
                {
                    return info;
                }

                info.name     = pcTaskGetName(handle);
                info.priority = uxTaskPriorityGet(handle);

                eTaskState state = eTaskGetState(handle);
                switch (state)
                {
                case eRunning:
                    info.state = State::RUNNING;
                    break;
                case eReady:
                    info.state = State::READY;
                    break;
                case eBlocked:
                    info.state = State::BLOCKED;
                    break;
                case eSuspended:
                    info.state = State::SUSPENDED;
                    break;
                case eDeleted:
                    info.state = State::DELETED;
                    break;
                default:
                    info.state = State::DELETED;
                    break;
                }

                return info;
            }

            Info TaskManager::getCurrentTaskInfo()
            {
                TaskHandle_t current = xTaskGetCurrentTaskHandle();
                return getTaskInfo(current);
            }

            uint32_t TaskManager::getTaskCount()
            {
                return uxTaskGetNumberOfTasks();
            }

            void TaskManager::delayMs(uint32_t ms)
            {
                vTaskDelay(pdMS_TO_TICKS(ms));
            }

            void TaskManager::delayUs(uint32_t us)
            {
                if (us == 0)
                {
                    return;
                }

                auto     cpu_info = app::sys::info::CpuInfo::getCpuInfo();
                uint32_t freq_hz  = cpu_info.getCpuFrequency();

                constexpr uint64_t US_PER_SECOND = 1000000ULL;
                uint64_t           cycles = static_cast<uint64_t>(us) * freq_hz / US_PER_SECOND;
                uint64_t           start_cycles = esp_cpu_get_cycle_count();

                while ((esp_cpu_get_cycle_count() - start_cycles) < cycles)
                {
                }
            }

            uint32_t TaskManager::getTickCount()
            {
                return xTaskGetTickCount();
            }

        } // namespace task
    } // namespace sys
} // namespace app
