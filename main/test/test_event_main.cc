#include "system/event/event.hpp"
#include "system/task/task.hpp"

#include <cstring>

#include "esp_event_base.h"
#include "esp_log.h"

static const char* const TAG = "Main";

// 定义自定义事件基和事件 ID
ESP_EVENT_DECLARE_BASE(TEST_EVENT_BASE);
ESP_EVENT_DEFINE_BASE(TEST_EVENT_BASE);

enum TestEventId
{
    TEST_EVENT_ID_1 = 1,
    TEST_EVENT_ID_2 = 2,
};

// 测试事件数据结构
struct TestEventData
{
    int32_t value;
    char    message[32];
};

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== 事件管理器测试开始 ===");

    // 初始化事件系统
    auto& event_mgr = app::sys::event::EventManager::getInstance();
    if (!event_mgr.init())
    {
        ESP_LOGE(TAG, "事件系统初始化失败");
        return;
    }
    ESP_LOGI(TAG, "事件系统初始化成功");

    // 创建自定义事件基（测试）
    const char* custom_base = event_mgr.createBase("CUSTOM_BASE");
    if (!custom_base)
    {
        ESP_LOGE(TAG, "创建自定义事件基失败");
    }
    else
    {
        ESP_LOGI(TAG, "创建自定义事件基成功: %s", custom_base);
    }

    // 注册事件处理器
    bool registered = event_mgr.registerHandler(
        TEST_EVENT_BASE, TEST_EVENT_ID_1,
        [](esp_event_base_t base, app::sys::event::EventId id,
           const app::sys::event::EventData& data)
        {
            ESP_LOGI(TAG, ">>> 事件处理器1被调用: base=%s, id=%d", base, static_cast<int>(id));
            if (data.data)
            {
                auto* event_data = static_cast<TestEventData*>(data.data);
                ESP_LOGI(TAG, "    数据: value=%d, message=%s", event_data->value,
                         event_data->message);
            }
        });

    if (!registered)
    {
        ESP_LOGE(TAG, "注册事件处理器1失败");
    }
    else
    {
        ESP_LOGI(TAG, "事件处理器1注册成功");
    }

    // 注册第二个处理器（测试 ESP_EVENT_ANY_ID）
    registered = event_mgr.registerHandler(
        TEST_EVENT_BASE, ESP_EVENT_ANY_ID,
        [](esp_event_base_t base, app::sys::event::EventId id,
           const app::sys::event::EventData& data)
        {
            ESP_LOGI(TAG, ">>> 通用事件处理器被调用: base=%s, id=%d", base, static_cast<int>(id));
            if (data.data && id == TEST_EVENT_ID_2)
            {
                auto* event_data = static_cast<TestEventData*>(data.data);
                ESP_LOGI(TAG, "    事件2数据: value=%d, message=%s", event_data->value,
                         event_data->message);
            }
        });

    if (!registered)
    {
        ESP_LOGE(TAG, "注册通用事件处理器失败");
    }
    else
    {
        ESP_LOGI(TAG, "通用事件处理器注册成功");
    }

    // 发送事件
    ESP_LOGI(TAG, "--- 发送事件 ---");
    TestEventData              event_data1{42, "Hello Event!"};
    app::sys::event::EventData data1{&event_data1, sizeof(TestEventData)};

    if (event_mgr.post(TEST_EVENT_BASE, TEST_EVENT_ID_1, data1))
    {
        ESP_LOGI(TAG, "事件发送成功");
    }
    else
    {
        ESP_LOGE(TAG, "事件发送失败");
    }

    // 延迟一下，让事件处理完成
    app::sys::task::TaskManager::delayMs(100);

    // 发送第二个事件
    ESP_LOGI(TAG, "准备发送事件2...");
    TestEventData              event_data2{100, "Second Event"};
    app::sys::event::EventData data2{&event_data2, sizeof(TestEventData)};

    if (event_mgr.post(TEST_EVENT_BASE, TEST_EVENT_ID_2, data2))
    {
        ESP_LOGI(TAG, "事件2发送成功，等待处理...");
    }
    else
    {
        ESP_LOGE(TAG, "事件2发送失败");
    }

    // 延迟一下，让事件处理完成（增加延迟确保所有日志都输出）
    app::sys::task::TaskManager::delayMs(150);
    ESP_LOGI(TAG, "事件2处理完成检查");

    // 测试重复注册（应该覆盖）
    ESP_LOGI(TAG, "--- 测试重复注册 ---");
    registered = event_mgr.registerHandler(
        TEST_EVENT_BASE, TEST_EVENT_ID_1,
        [](esp_event_base_t base, app::sys::event::EventId id,
           const app::sys::event::EventData& data)
        {
            ESP_LOGI(TAG, ">>> 新的事件处理器1被调用（覆盖后）: base=%s, id=%d", base,
                     static_cast<int>(id));
        });

    if (registered)
    {
        ESP_LOGI(TAG, "重复注册成功（覆盖旧处理器）");
        // 再次发送事件，应该使用新的处理器
        if (event_mgr.post(TEST_EVENT_BASE, TEST_EVENT_ID_1, data1))
        {
            app::sys::task::TaskManager::delayMs(100);
        }
    }

    //  注销事件处理器
    ESP_LOGI(TAG, "--- 注销事件处理器 ---");
    if (event_mgr.unregisterHandler(TEST_EVENT_BASE, TEST_EVENT_ID_1))
    {
        ESP_LOGI(TAG, "事件处理器1注销成功");
    }
    else
    {
        ESP_LOGE(TAG, "事件处理器1注销失败");
    }

    // 尝试注销不存在的处理器
    if (!event_mgr.unregisterHandler(TEST_EVENT_BASE, 999))
    {
        ESP_LOGI(TAG, "注销不存在的处理器返回 false（预期行为）");
    }

    //  发送事件到已注销的处理器（应该不会触发）
    ESP_LOGI(TAG, "--- 发送事件到已注销的处理器 ---");
    if (event_mgr.post(TEST_EVENT_BASE, TEST_EVENT_ID_1, data1))
    {
        ESP_LOGI(TAG, "事件发送成功（但处理器已注销，不会触发）");
        app::sys::task::TaskManager::delayMs(100);
    }

    //  反初始化
    ESP_LOGI(TAG, "--- 反初始化事件系统 ---");
    event_mgr.deinit();
    ESP_LOGI(TAG, "事件系统反初始化完成");

    ESP_LOGI(TAG, "=== 事件管理器测试完成 ===");
}
