#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// ESP-IDF 事件系统前向声明
extern "C"
{
    typedef const char* esp_event_base_t;
}

namespace app
{
    namespace sys
    {
        namespace event
        {

            /**
             * @brief 事件 ID 类型
             *
             * @note EventId 必须能够容纳 ESP_EVENT_ANY_ID 的值（通常为 -1）
             *       如果使用 enum class，需要确保有一个枚举值等于 ESP_EVENT_ANY_ID
             */
            using EventId = int32_t;

            /**
             * @brief 事件数据
             *
             * @note handler 需要根据 event_base 和 event_id 自行进行类型转换
             * @note size 字段在 post 时用于指定数据大小，回调中由 ESP-IDF 管理
             */
            struct EventData
            {
                void*  data; // 事件数据指针
                size_t size; // 事件数据大小

                EventData() : data(nullptr), size(0) {}

                EventData(void* d, size_t s) : data(d), size(s) {}
            };

            /**
             * @brief 事件处理器函数类型
             *
             * @param event_base 事件基
             * @param event_id 事件 ID
             * @param event_data 事件数据
             */
            using EventHandler = std::function<void(esp_event_base_t event_base, EventId event_id,
                                                    const EventData& event_data)>;

            /**
             * @brief 事件管理器类
             *
             * 提供统一的事件循环管理、事件注册、事件发送等功能
             */
            class EventManager
            {
            public:
                /**
                 * @brief 获取事件管理器实例
                 * @return 事件管理器引用
                 */
                static EventManager& getInstance();

                /**
                 * @brief 初始化事件系统
                 * @return 成功返回 true，失败返回 false
                 * @note 如果事件循环已创建，则直接返回成功
                 */
                bool init();

                /**
                 * @brief 反初始化事件系统
                 * @note 会注销所有已注册的事件处理器，但不销毁默认事件循环
                 */
                void deinit();

                /**
                 * @brief 创建自定义事件基
                 * @param name 事件基名称
                 * @return 事件基指针，失败返回 nullptr
                 * @note 事件基名称必须是全局唯一的字符串常量（建议使用静态字符串）
                 */
                esp_event_base_t createBase(const char* name);

                /**
                 * @brief 注册事件处理器
                 * @param event_base 事件基（如 WIFI_EVENT, IP_EVENT 或自定义事件基）
                 * @param event_id 事件 ID（ESP_EVENT_ANY_ID 表示匹配该 event_base 的所有事件）
                 * @param handler 事件处理函数
                 * @return 成功返回 true，失败返回 false
                 * @note 线程安全：支持多线程并发注册
                 */
                bool registerHandler(esp_event_base_t event_base, EventId event_id,
                                     EventHandler handler);

                /**
                 * @brief 注销事件处理器
                 * @param event_base 事件基
                 * @param event_id 事件 ID
                 * @return 成功返回 true，失败返回 false
                 */
                bool unregisterHandler(esp_event_base_t event_base, EventId event_id) const;

                /**
                 * @brief 发送事件到事件循环
                 * @param event_base 事件基
                 * @param event_id 事件 ID
                 * @param event_data 事件数据（可选）
                 * @param timeout_ms 超时时间（毫秒），0 表示不阻塞
                 * @return 成功返回 true，失败返回 false
                 */
                bool post(esp_event_base_t event_base, EventId event_id,
                          const EventData& event_data = EventData(), uint32_t timeout_ms = 0) const;

                /**
                 * @brief 从 ISR 发送事件到事件循环
                 * @param event_base 事件基
                 * @param event_id 事件 ID
                 * @param event_data 事件数据（可选，最大 4 字节）
                 * @return 成功返回 true，失败返回 false
                 * @note 需要配置 CONFIG_ESP_EVENT_POST_FROM_ISR=y
                 */
                bool postFromISR(esp_event_base_t event_base, EventId event_id,
                                 const EventData& event_data = EventData()) const;

                /**
                 * @brief 检查事件系统是否已初始化
                 * @return 已初始化返回 true
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

            private:
                EventManager();
                ~EventManager();
                EventManager(const EventManager&)            = delete;
                EventManager& operator=(const EventManager&) = delete;

                // 事件处理器包装函数
                static void cEventHandler(void* arg, esp_event_base_t event_base, int32_t event_id,
                                          void* event_data);

                bool initialized_;
            };

        } // namespace event
    }     // namespace sys
} // namespace app
