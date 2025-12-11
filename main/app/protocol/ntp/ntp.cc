#include "ntp.hpp"

#include <cstring>
#include <ctime>

#include "esp_log.h"
#include "esp_netif_sntp.h"
#include "esp_sntp.h"
#include "freertos/FreeRTOS.h"

static const char* const TAG = "NTP";

namespace app
{
    namespace protocol
    {
        namespace ntp
        {

            NTPManager& NTPManager::getInstance()
            {
                static NTPManager instance;
                return instance;
            }

            bool NTPManager::init()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (initialized_)
                {
                    return true;
                }

                initialized_ = true;
                started_     = false;
                sync_status_ = SyncStatus::RESET;
                servers_.clear();
                sync_callback_ = nullptr;
                sync_mode_     = SyncMode::IMMEDIATE;

                return true;
            }

            void NTPManager::deinit()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    return;
                }

                stop();

                initialized_ = false;
                sync_status_ = SyncStatus::RESET;
                servers_.clear();
                sync_callback_ = nullptr;
                sync_mode_     = SyncMode::IMMEDIATE;
            }

            bool NTPManager::configure(const std::vector<std::string>& servers, SyncMode sync_mode)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    ESP_LOGE(TAG, "NTP 管理器未初始化");
                    return false;
                }

                if (servers.empty() || servers.size() > 3)
                {
                    ESP_LOGE(TAG, "NTP 服务器数量无效（1-3 个）");
                    return false;
                }

                if (started_)
                {
                    ESP_LOGE(TAG, "SNTP 已启动，请先停止");
                    return false;
                }

                servers_   = servers;
                sync_mode_ = sync_mode;

                return true;
            }

            bool NTPManager::start()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    ESP_LOGE(TAG, "NTP 管理器未初始化");
                    return false;
                }

                if (servers_.empty())
                {
                    ESP_LOGE(TAG, "未配置 NTP 服务器");
                    return false;
                }

                if (started_)
                {
                    return true;
                }

                esp_sntp_config_t config = ESP_NETIF_SNTP_DEFAULT_CONFIG(servers_[0].c_str());

                if (sync_mode_ == SyncMode::SMOOTH)
                {
                    config.smooth_sync = true;
                }
                else
                {
                    config.smooth_sync = false;
                }

                config.sync_cb = sntpSyncCallback;

                esp_err_t ret = esp_netif_sntp_init(&config);
                if (ret != ESP_OK)
                {
                    ESP_LOGE(TAG, "SNTP 初始化失败: %s", esp_err_to_name(ret));
                    sync_status_ = SyncStatus::FAILED;
                    return false;
                }

                if (servers_.size() > 1)
                {
                    esp_sntp_setservername(1, servers_[1].c_str());
                }
                if (servers_.size() > 2)
                {
                    esp_sntp_setservername(2, servers_[2].c_str());
                }

                started_     = true;
                sync_status_ = SyncStatus::IN_PROGRESS;

                ESP_LOGI(TAG, "SNTP 服务已启动");

                return true;
            }

            void NTPManager::stop()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!started_)
                {
                    return;
                }

                esp_netif_sntp_deinit();

                started_     = false;
                sync_status_ = SyncStatus::RESET;

                ESP_LOGI(TAG, "SNTP 服务已停止");
            }

            bool NTPManager::waitSync(uint32_t timeout_ms)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!started_)
                    {
                        ESP_LOGE(TAG, "SNTP 服务未启动");
                        return false;
                    }
                }

                esp_err_t ret = esp_netif_sntp_sync_wait(pdMS_TO_TICKS(timeout_ms));

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (ret == ESP_OK)
                    {
                        sync_status_ = SyncStatus::COMPLETED;
                    }
                    else
                    {
                        sync_status_ = SyncStatus::FAILED;
                    }
                }

                return ret == ESP_OK;
            }

            void NTPManager::setSyncCallback(SyncCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                sync_callback_ = callback;
            }

            bool NTPManager::setTimezone(const char* tz)
            {
                if (!tz)
                {
                    return false;
                }

                setenv("TZ", tz, 1);
                tzset();

                ESP_LOGI(TAG, "时区已设置为: %s", tz);

                return true;
            }

            SyncStatus NTPManager::getSyncStatus() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return sync_status_;
            }

            bool NTPManager::isInitialized() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return initialized_;
            }

            bool NTPManager::isStarted() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return started_;
            }

            void NTPManager::sntpSyncCallback(struct timeval* tv)
            {
                auto& instance = getInstance();

                SyncCallback cb = nullptr;
                {
                    std::lock_guard<std::mutex> lock(instance.mutex_);
                    instance.sync_status_ = SyncStatus::COMPLETED;
                    cb                    = instance.sync_callback_;
                }

                if (cb)
                {
                    cb(SyncStatus::COMPLETED);
                }

                ESP_LOGI(TAG, "时间同步完成");
            }

        } // namespace ntp
    }     // namespace protocol
} // namespace app
