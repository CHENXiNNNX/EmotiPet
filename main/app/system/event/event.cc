#include "event.hpp"

#include <mutex>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "esp_event.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char* const TAG = "EVENT";

namespace app
{
    namespace sys
    {
        namespace event
        {

            struct HandlerKey
            {
                esp_event_base_t base;
                EventId          id;

                bool operator==(const HandlerKey& other) const
                {
                    return base == other.base && id == other.id;
                }
            };

            struct HandlerKeyHash
            {
                size_t operator()(const HandlerKey& key) const
                {
                    return std::hash<const char*>{}(key.base) ^ (std::hash<EventId>{}(key.id) << 1);
                }
            };

            static std::unordered_map<HandlerKey, EventHandler, HandlerKeyHash> g_handlers;
            static std::mutex                                                   g_handlers_mutex;
            static std::unordered_set<esp_event_base_t>                         g_registered_bases;
            static std::unordered_map<esp_event_base_t, size_t> g_base_handler_count;

            EventManager::EventManager() : initialized_(false) {}

            EventManager& EventManager::getInstance()
            {
                static EventManager instance;
                return instance;
            }

            bool EventManager::init()
            {
                if (initialized_)
                {
                    return true;
                }

                esp_err_t ret = esp_event_loop_create_default();
                if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
                {
                    return false;
                }

                initialized_ = true;
                return true;
            }

            void EventManager::deinit()
            {
                if (!initialized_)
                {
                    return;
                }

                std::vector<esp_event_base_t> bases;
                {
                    std::lock_guard<std::mutex> lock(g_handlers_mutex);
                    bases.reserve(g_registered_bases.size());
                    for (const auto& base : g_registered_bases)
                    {
                        bases.push_back(base);
                    }
                    g_handlers.clear();
                    g_registered_bases.clear();
                    g_base_handler_count.clear();
                }

                for (auto base : bases)
                {
                    esp_event_handler_unregister(base, ESP_EVENT_ANY_ID, &cEventHandler);
                }

                initialized_ = false;
            }

            esp_event_base_t EventManager::createBase(const char* name)
            {
                if (!name)
                {
                    return nullptr;
                }

                return name;
            }

            bool EventManager::registerHandler(esp_event_base_t event_base, EventId event_id,
                                               EventHandler handler)
            {
                if (!initialized_ || !event_base || !handler)
                {
                    return false;
                }

                HandlerKey key{event_base, event_id};

                std::lock_guard<std::mutex> lock(g_handlers_mutex);

                bool is_new_handler = (g_handlers.find(key) == g_handlers.end());
                bool need_register_esp_handler =
                    (g_registered_bases.find(event_base) == g_registered_bases.end());

                if (need_register_esp_handler)
                {
                    esp_err_t ret = esp_event_handler_register(event_base, ESP_EVENT_ANY_ID,
                                                               &cEventHandler, nullptr);
                    if (ret != ESP_OK)
                    {
                        ESP_LOGE(TAG, "注册事件处理器失败: %s", esp_err_to_name(ret));
                        return false;
                    }

                    g_registered_bases.insert(event_base);
                }

                g_handlers[key] = handler;

                if (is_new_handler)
                {
                    g_base_handler_count[event_base]++;
                }

                return true;
            }

            bool EventManager::unregisterHandler(esp_event_base_t event_base,
                                                 EventId          event_id) const
            {
                if (!initialized_)
                {
                    return false;
                }

                HandlerKey key{event_base, event_id};

                bool need_unregister_esp_handler = false;
                {
                    std::lock_guard<std::mutex> lock(g_handlers_mutex);

                    auto it = g_handlers.find(key);
                    if (it == g_handlers.end())
                    {
                        return false;
                    }

                    g_handlers.erase(it);

                    auto count_it = g_base_handler_count.find(event_base);
                    if (count_it != g_base_handler_count.end())
                    {
                        if (count_it->second > 1)
                        {
                            count_it->second--;
                        }
                        else
                        {
                            g_base_handler_count.erase(count_it);
                            if (g_registered_bases.find(event_base) != g_registered_bases.end())
                            {
                                need_unregister_esp_handler = true;
                                g_registered_bases.erase(event_base);
                            }
                        }
                    }
                }

                if (need_unregister_esp_handler)
                {
                    esp_event_handler_unregister(event_base, ESP_EVENT_ANY_ID, &cEventHandler);
                }

                return true;
            }

            bool EventManager::post(esp_event_base_t event_base, EventId event_id,
                                    const EventData& event_data, uint32_t timeout_ms) const
            {
                if (!initialized_ || !event_base)
                {
                    return false;
                }

                TickType_t timeout_ticks = timeout_ms > 0 ? pdMS_TO_TICKS(timeout_ms) : 0;

                esp_err_t ret = esp_event_post(event_base, event_id, event_data.data,
                                               event_data.size, timeout_ticks);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "发送事件失败: %s", esp_err_to_name(ret));
                    return false;
                }

                return true;
            }

            bool EventManager::postFromISR(esp_event_base_t event_base, EventId event_id,
                                           const EventData& event_data) const
            {
                if (!initialized_)
                {
                    return false;
                }

                if (!event_base)
                {
                    return false;
                }

                BaseType_t task_unblocked = pdFALSE;
                esp_err_t  ret = esp_event_isr_post(event_base, event_id, event_data.data,
                                                    event_data.size, &task_unblocked);
                if (ret != ESP_OK)
                {
                    return false;
                }

                if (task_unblocked == pdTRUE)
                {
                    portYIELD_FROM_ISR();
                }

                return true;
            }

            void EventManager::cEventHandler(void* /*arg*/, esp_event_base_t event_base,
                                             int32_t event_id, void* event_data)
            {
                HandlerKey key{event_base, static_cast<EventId>(event_id)};

                EventHandler specific_handler;
                EventHandler any_handler;
                {
                    std::lock_guard<std::mutex> lock(g_handlers_mutex);

                    auto it = g_handlers.find(key);
                    if (it != g_handlers.end())
                    {
                        specific_handler = it->second;
                    }

                    HandlerKey any_key{event_base, ESP_EVENT_ANY_ID};
                    auto       any_it = g_handlers.find(any_key);
                    if (any_it != g_handlers.end())
                    {
                        any_handler = any_it->second;
                    }
                }

                EventData data{event_data, 0};

                if (specific_handler)
                {
                    specific_handler(event_base, static_cast<EventId>(event_id), data);
                }

                if (any_handler)
                {
                    any_handler(event_base, static_cast<EventId>(event_id), data);
                }
            }

        } // namespace event
    }     // namespace sys
} // namespace app
