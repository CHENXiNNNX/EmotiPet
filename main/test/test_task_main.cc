#include "system/info/info.hpp"
#include "system/task/task.hpp"

#include "esp_log.h"

static const char* const TAG = "Main";

// 测试任务函数
void testTask1(void* param)
{
    ESP_LOGI(TAG, "Task1 开始运行");
    for (int i = 0; i < 5; i++)
    {
        ESP_LOGI(TAG, "Task1: %d", i);
        app::sys::task::TaskManager::delayMs(1000); // 延迟 1 秒
    }
    ESP_LOGI(TAG, "Task1 完成");
}

void testTask2(void* param)
{
    ESP_LOGI(TAG, "Task2 开始运行");
    for (int i = 0; i < 10; i++)
    {
        ESP_LOGI(TAG, "Task2: %d", i);
        app::sys::task::TaskManager::delayMs(500); // 延迟 0.5 秒
    }
    ESP_LOGI(TAG, "Task2 完成");
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== Task Module Test ===");

    // 测试 TaskManager
    auto& task_mgr = app::sys::task::TaskManager::getInstance();
    ESP_LOGI(TAG, "系统任务数量: %lu", task_mgr.getTaskCount());

    // 获取当前任务信息
    auto current_info = task_mgr.getCurrentTaskInfo();
    ESP_LOGI(TAG, "当前任务: %s, 优先级: %u", current_info.name ? current_info.name : "Unknown",
             current_info.priority);

    // 创建任务 1
    app::sys::task::Config config1;
    config1.name       = "TestTask1";
    config1.stack_size = 4096;
    config1.priority   = app::sys::task::Priority::NORMAL;
    config1.core_id    = -1; // 不绑定核心

    app::sys::task::Task task1(testTask1, config1);
    ESP_LOGI(TAG, "创建任务: %s", task1.getName());

    // 启动任务 1
    if (task1.start())
    {
        ESP_LOGI(TAG, "任务 1 启动成功");

        // 获取任务信息
        auto info1 = task1.getInfo();
        ESP_LOGI(TAG, "任务 1 信息: 名称=%s, 状态=%d, 优先级=%u", info1.name,
                 static_cast<int>(info1.state), info1.priority);
    }
    else
    {
        ESP_LOGE(TAG, "任务 1 启动失败");
    }

    // 延迟一下
    app::sys::task::TaskManager::delayMs(500);

    // 创建任务 2
    app::sys::task::Config config2;
    config2.name       = "TestTask2";
    config2.stack_size = 4096;
    config2.priority   = app::sys::task::Priority::HIGH;
    config2.core_id    = -1;

    app::sys::task::Task task2(testTask2, config2);
    ESP_LOGI(TAG, "创建任务: %s", task2.getName());

    // 启动任务 2
    if (task2.start())
    {
        ESP_LOGI(TAG, "任务 2 启动成功");
    }

    // 测试任务挂起和恢复
    app::sys::task::TaskManager::delayMs(2000);
    ESP_LOGI(TAG, "挂起任务 1");
    task1.suspend();
    app::sys::task::TaskManager::delayMs(2000);
    ESP_LOGI(TAG, "恢复任务 1");
    task1.resume();

    // 测试 delayUs
    ESP_LOGI(TAG, "测试 delayUs(1000000) - 延迟 1 秒");
    app::sys::task::TaskManager::delayUs(1000000);
    ESP_LOGI(TAG, "delayUs 完成");

    // 等待任务完成
    app::sys::task::TaskManager::delayMs(10000);

    // 测试任务查找
    auto* found_handle = task_mgr.findTask("TestTask1");
    if (found_handle != nullptr)
    {
        ESP_LOGI(TAG, "找到任务 TestTask1");
        auto found_info = task_mgr.getTaskInfo(found_handle);
        ESP_LOGI(TAG, "找到的任务信息: 名称=%s, 优先级=%u", found_info.name, found_info.priority);
    }

    ESP_LOGI(TAG, "最终系统任务数量: %lu", task_mgr.getTaskCount());
    ESP_LOGI(TAG, "测试完成");
}
