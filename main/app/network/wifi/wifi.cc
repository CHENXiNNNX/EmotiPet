#include "wifi.hpp"

#include <cstring>
#include <memory>
#include <vector>

#include "esp_netif.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "nvs.h"
#include "event.hpp"
#include "system/task/task.hpp"

static const char* const TAG = "WIFI";

namespace app
{
    namespace network
    {
        namespace wifi
        {

            static const char* const NVS_NAMESPACE = "wifi";
            static const char* const NVS_KEY_SSID  = "ssid";
            static const char* const NVS_KEY_PASS  = "password";

            WiFiManager::WiFiManager()
                : initialized_(false), connect_timeout_ms_(0), connect_timeout_set_(false),
                  connect_start_tick_(0), disconnecting_(false), scanning_(false)
            {
            }

            WiFiManager& WiFiManager::getInstance()
            {
                static WiFiManager instance;
                return instance;
            }

            bool WiFiManager::init()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return true;
                }

                nvs_handle_t check_handle;
                esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &check_handle);
                if (ret == ESP_ERR_NVS_NOT_INITIALIZED)
                {
                    return false;
                }
                if (ret == ESP_OK)
                {
                    nvs_close(check_handle);
                }

                auto& event_mgr = app::sys::event::EventManager::getInstance();
                if (!event_mgr.isInitialized())
                {
                    if (!event_mgr.init())
                    {
                        return false;
                    }
                }

                ESP_ERROR_CHECK(esp_netif_init());

                esp_netif_create_default_wifi_sta();

                wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
                ESP_ERROR_CHECK(esp_wifi_init(&cfg));

                event_mgr.registerHandler(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                          [this](esp_event_base_t base, app::sys::event::EventId id,
                                                 const app::sys::event::EventData& data)
                                          { handleWiFiEvent(base, id, data); });

                event_mgr.registerHandler(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                          [this](esp_event_base_t base, app::sys::event::EventId id,
                                                 const app::sys::event::EventData& data)
                                          { handleIPEvent(base, id, data); });

                ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
                ESP_ERROR_CHECK(esp_wifi_start());

                initialized_         = true;
                info_.state          = State::DISCONNECTED;
                connect_timeout_set_ = false;

                return true;
            }

            void WiFiManager::deinit()
            {
                std::unique_lock<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    return;
                }

                lock.unlock();
                disconnect();
                app::sys::task::TaskManager::delayMs(50);
                lock.lock();

                esp_wifi_stop();
                esp_wifi_deinit();

                auto& event_mgr = app::sys::event::EventManager::getInstance();
                event_mgr.unregisterHandler(WIFI_EVENT, ESP_EVENT_ANY_ID);
                event_mgr.unregisterHandler(IP_EVENT, IP_EVENT_STA_GOT_IP);

                initialized_ = false;
            }

            bool WiFiManager::scan(ScanCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    return false;
                }

                if (scanning_)
                {
                    return false;
                }

                scan_callback_ = callback;
                scanning_      = true;

                wifi_scan_config_t scan_config   = {};
                scan_config.ssid                 = nullptr;
                scan_config.bssid                = nullptr;
                scan_config.channel              = 0;
                scan_config.show_hidden          = false;
                scan_config.scan_type            = WIFI_SCAN_TYPE_ACTIVE;
                scan_config.scan_time.active.min = 100;
                scan_config.scan_time.active.max = 300;

                esp_err_t ret = esp_wifi_scan_start(&scan_config, false);
                if (ret != ESP_OK)
                {
                    scanning_ = false;
                    return false;
                }

                return true;
            }

            bool WiFiManager::connect(const char* ssid, const char* password, uint32_t timeout_ms)
            {
                std::unique_lock<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    return false;
                }

                Credentials creds;

                if (ssid && ssid[0] != '\0')
                {
                    creds.setSsid(ssid);
                    if (password)
                    {
                        creds.setPassword(password);
                    }
                }
                else
                {
                    lock.unlock();
                    if (!loadCredentials(creds))
                    {
                        return false;
                    }
                    lock.lock();
                }

                if (!creds.isValid())
                {
                    return false;
                }

                if (info_.state == State::CONNECTING || info_.state == State::CONNECTED)
                {
                    if (strcmp(info_.ssid, creds.ssid) == 0)
                    {
                        return true;
                    }
                    lock.unlock();
                    disconnect();
                    app::sys::task::TaskManager::delayMs(50);
                    lock.lock();
                }

                wifi_config_t wifi_config = {};
                strncpy(reinterpret_cast<char*>(wifi_config.sta.ssid), creds.ssid,
                        sizeof(wifi_config.sta.ssid) - 1);
                strncpy(reinterpret_cast<char*>(wifi_config.sta.password), creds.password,
                        sizeof(wifi_config.sta.password) - 1);
                wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
                wifi_config.sta.pmf_cfg.capable    = true;
                wifi_config.sta.pmf_cfg.required   = false;

                ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

                connect_timeout_ms_  = timeout_ms;
                connect_timeout_set_ = true;
                connect_start_tick_  = app::sys::task::TaskManager::getTickCount();

                updateState(State::CONNECTING);

                if (timeout_ms > 0 && !timeout_task_)
                {
                    app::sys::task::Config config;
                    config.name       = "wifi_timeout";
                    config.stack_size = 2048;
                    config.priority   = app::sys::task::Priority::NORMAL;
                    config.core_id    = -1;

                    timeout_task_ =
                        std::make_unique<app::sys::task::Task>(timeoutTaskWrapper, config, this);
                    if (!timeout_task_->start())
                    {
                        ESP_LOGE(TAG, "创建超时任务失败");
                        timeout_task_.reset();
                    }
                }

                esp_err_t ret = esp_wifi_connect();
                if (ret != ESP_OK)
                {
                    updateState(State::FAILED, FailureReason::CONNECTION_FAILED);
                    clearTimeoutTask();
                    return false;
                }

                return true;
            }

            void WiFiManager::disconnect()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_ || info_.state == State::DISCONNECTED)
                {
                    return;
                }

                disconnecting_       = true;
                connect_timeout_set_ = false;
                clearTimeoutTask();

                esp_wifi_disconnect();
            }

            const Info& WiFiManager::getInfo() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return info_;
            }

            State WiFiManager::getState() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return info_.state;
            }

            bool WiFiManager::isConnected() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return info_.state == State::CONNECTED;
            }

            void WiFiManager::setStateCallback(StateCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_callback_ = callback;
            }

            bool WiFiManager::saveCredentials(const Credentials& credentials)
            {
                if (!credentials.isValid())
                {
                    return false;
                }

                nvs_handle_t nvs_handle;
                esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
                if (ret != ESP_OK)
                {
                    return false;
                }

                ret = nvs_set_str(nvs_handle, NVS_KEY_SSID, credentials.ssid);
                if (ret != ESP_OK)
                {
                    nvs_close(nvs_handle);
                    return false;
                }

                ret = nvs_set_str(nvs_handle, NVS_KEY_PASS, credentials.password);
                if (ret != ESP_OK)
                {
                    nvs_close(nvs_handle);
                    return false;
                }

                ret = nvs_commit(nvs_handle);
                nvs_close(nvs_handle);

                return ret == ESP_OK;
            }

            bool WiFiManager::loadCredentials(Credentials& credentials)
            {
                nvs_handle_t nvs_handle;
                esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
                if (ret != ESP_OK)
                {
                    return false;
                }

                size_t required_size = sizeof(credentials.ssid);
                ret = nvs_get_str(nvs_handle, NVS_KEY_SSID, credentials.ssid, &required_size);
                if (ret != ESP_OK)
                {
                    nvs_close(nvs_handle);
                    return false;
                }

                required_size = sizeof(credentials.password);
                ret = nvs_get_str(nvs_handle, NVS_KEY_PASS, credentials.password, &required_size);
                if (ret != ESP_OK)
                {
                    nvs_close(nvs_handle);
                    return false;
                }

                nvs_close(nvs_handle);

                return credentials.isValid();
            }

            bool WiFiManager::clearCredentials()
            {
                nvs_handle_t nvs_handle;
                esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
                if (ret != ESP_OK)
                {
                    return false;
                }

                nvs_erase_key(nvs_handle, NVS_KEY_SSID);
                nvs_erase_key(nvs_handle, NVS_KEY_PASS);

                ret = nvs_commit(nvs_handle);
                nvs_close(nvs_handle);

                return ret == ESP_OK;
            }

            bool WiFiManager::hasSavedCredentials()
            {
                nvs_handle_t nvs_handle;
                esp_err_t    ret = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
                if (ret != ESP_OK)
                {
                    return false;
                }

                size_t required_size = 0;
                ret = nvs_get_str(nvs_handle, NVS_KEY_SSID, nullptr, &required_size);
                nvs_close(nvs_handle);

                return ret == ESP_OK;
            }

            void WiFiManager::handleWiFiEvent(esp_event_base_t                  event_base,
                                              app::sys::event::EventId          event_id,
                                              const app::sys::event::EventData& event_data)
            {
                std::unique_lock<std::mutex> lock(mutex_);

                switch (event_id)
                {
                case WIFI_EVENT_STA_START:
                    break;

                case WIFI_EVENT_STA_CONNECTED:
                {
                    wifi_event_sta_connected_t* event =
                        static_cast<wifi_event_sta_connected_t*>(event_data.data);
                    strncpy(info_.ssid, reinterpret_cast<const char*>(event->ssid),
                            sizeof(info_.ssid) - 1);
                    info_.ssid[sizeof(info_.ssid) - 1] = '\0';

                    lock.unlock();
                    wifi_ap_record_t ap_info;
                    esp_err_t        ret = esp_wifi_sta_get_ap_info(&ap_info);
                    lock.lock();

                    if (ret == ESP_OK)
                    {
                        info_.rssi = ap_info.rssi;
                    }
                    else
                    {
                        info_.rssi = 0;
                    }
                    break;
                }

                case WIFI_EVENT_STA_DISCONNECTED:
                {
                    wifi_event_sta_disconnected_t* event =
                        static_cast<wifi_event_sta_disconnected_t*>(event_data.data);

                    connect_timeout_set_ = false;
                    clearTimeoutTask();

                    if (disconnecting_)
                    {
                        disconnecting_ = false;
                        updateState(State::DISCONNECTED);
                        break;
                    }

                    FailureReason reason = FailureReason::UNKNOWN;
                    if (event->reason == WIFI_REASON_AUTH_EXPIRE ||
                        event->reason == WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT ||
                        event->reason == WIFI_REASON_AUTH_FAIL)
                    {
                        reason = FailureReason::WRONG_PASSWORD;
                    }
                    else if (event->reason == WIFI_REASON_NO_AP_FOUND)
                    {
                        reason = FailureReason::NETWORK_NOT_FOUND;
                    }
                    else if (event->reason == WIFI_REASON_ASSOC_FAIL)
                    {
                        reason = FailureReason::CONNECTION_FAILED;
                    }

                    if (info_.state == State::CONNECTING)
                    {
                        updateState(State::FAILED, reason);
                    }
                    else
                    {
                        updateState(State::DISCONNECTED);
                    }
                    break;
                }

                case WIFI_EVENT_SCAN_DONE:
                {
                    scanning_ = false;

                    uint16_t ap_count = 0;
                    esp_wifi_scan_get_ap_num(&ap_count);

                    ScanCallback        callback = scan_callback_;
                    std::vector<ApInfo> aps;

                    if (ap_count > 0)
                    {
                        aps.reserve(ap_count);

                        wifi_ap_record_t* ap_records =
                            static_cast<wifi_ap_record_t*>(heap_caps_malloc(
                                ap_count * sizeof(wifi_ap_record_t), MALLOC_CAP_DEFAULT));
                        if (ap_records != nullptr)
                        {
                            esp_wifi_scan_get_ap_records(&ap_count, ap_records);

                            for (uint16_t i = 0; i < ap_count; i++)
                            {
                                ApInfo      info;
                                const auto& record = ap_records[i];
                                strncpy(info.ssid, reinterpret_cast<const char*>(record.ssid),
                                        sizeof(info.ssid) - 1);
                                info.ssid[sizeof(info.ssid) - 1] = '\0';
                                memcpy(info.bssid, record.bssid, sizeof(info.bssid));
                                info.rssi         = record.rssi;
                                info.authmode     = record.authmode;
                                info.is_encrypted = (record.authmode != WIFI_AUTH_OPEN);

                                aps.push_back(info);
                            }

                            heap_caps_free(ap_records);
                        }
                    }

                    lock.unlock();
                    if (callback)
                    {
                        callback(aps);
                    }
                    lock.lock();
                    break;
                }

                default:
                    break;
                }
            }

            void WiFiManager::handleIPEvent(esp_event_base_t                  event_base,
                                            app::sys::event::EventId          event_id,
                                            const app::sys::event::EventData& event_data)
            {
                std::unique_lock<std::mutex> lock(mutex_);

                if (event_id == IP_EVENT_STA_GOT_IP)
                {
                    ip_event_got_ip_t* event = static_cast<ip_event_got_ip_t*>(event_data.data);
                    memcpy(info_.ip, &event->ip_info.ip.addr, 4);
                    connect_timeout_set_ = false;
                    clearTimeoutTask();

                    updateState(State::CONNECTED);
                }
            }

            void WiFiManager::clearTimeoutTask()
            {
                if (timeout_task_)
                {
                    timeout_task_->destroy();
                    timeout_task_.reset();
                }
            }

            void WiFiManager::updateState(State state, FailureReason reason)
            {
                if (info_.state != state || info_.failure_reason != reason)
                {
                    info_.state              = state;
                    info_.failure_reason     = reason;
                    StateCallback callback   = state_callback_;
                    State         state_val  = state;
                    FailureReason reason_val = reason;

                    if (callback)
                    {
                        mutex_.unlock();
                        callback(state_val, reason_val);
                        mutex_.lock();
                    }
                }
            }

            void WiFiManager::checkTimeout()
            {
                std::unique_lock<std::mutex> lock(mutex_);

                if (!connect_timeout_set_ || info_.state != State::CONNECTING)
                {
                    return;
                }

                uint32_t elapsed =
                    (app::sys::task::TaskManager::getTickCount() - connect_start_tick_) *
                    portTICK_PERIOD_MS;
                if (elapsed >= connect_timeout_ms_)
                {
                    connect_timeout_set_     = false;
                    StateCallback callback   = state_callback_;
                    State         state_val  = State::FAILED;
                    FailureReason reason_val = FailureReason::TIMEOUT;

                    info_.state          = State::FAILED;
                    info_.failure_reason = FailureReason::TIMEOUT;

                    lock.unlock();
                    if (callback)
                    {
                        callback(state_val, reason_val);
                    }
                    esp_wifi_disconnect();
                }
            }

            void WiFiManager::timeoutTaskWrapper(void* param)
            {
                WiFiManager* manager = static_cast<WiFiManager*>(param);
                if (!manager)
                {
                    return;
                }

                while (true)
                {
                    app::sys::task::TaskManager::delayMs(1000);
                    manager->checkTimeout();

                    std::lock_guard<std::mutex> lock(manager->mutex_);
                    if (!manager->connect_timeout_set_ || manager->info_.state != State::CONNECTING)
                    {
                        // 任务会自动结束，不需要手动删除
                        break;
                    }
                }
            }

        } // namespace wifi
    }     // namespace network
} // namespace app
