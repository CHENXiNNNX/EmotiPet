#include "network/wifi/wifi.hpp"
#include "protocol/ntp/ntp.hpp"
#include "system/event/event.hpp"
#include "system/task/task.hpp"
#include "tool/time/time.hpp"

#include <ctime>

#include "esp_log.h"
#include "nvs_flash.h"

static const char* const TAG = "NTPTest";

extern "C" void app_main(void)
{
    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 初始化事件系统
    auto& event_mgr = app::sys::event::EventManager::getInstance();
    if (!event_mgr.init())
    {
        ESP_LOGE(TAG, "事件系统初始化失败");
        return;
    }

    // 初始化 WiFi
    auto& wifi_mgr = app::network::wifi::WiFiManager::getInstance();
    if (!wifi_mgr.init())
    {
        ESP_LOGE(TAG, "WiFi 初始化失败");
        return;
    }

    // 设置 WiFi 状态回调
    wifi_mgr.setStateCallback(
        [](app::network::wifi::State state, app::network::wifi::FailureReason reason)
        {
            const char* state_str = "UNKNOWN";
            switch (state)
            {
            case app::network::wifi::State::DISCONNECTED:
                state_str = "DISCONNECTED";
                break;
            case app::network::wifi::State::CONNECTING:
                state_str = "CONNECTING";
                break;
            case app::network::wifi::State::CONNECTED:
                state_str = "CONNECTED";
                break;
            case app::network::wifi::State::FAILED:
                state_str = "FAILED";
                break;
            }
            ESP_LOGI(TAG, "WiFi 状态: %s", state_str);
        });

    // 连接 WiFi（使用之前保存的凭证或手动指定）
    ESP_LOGI(TAG, "开始连接 WiFi...");
    if (!wifi_mgr.connect("yf", "qwer1234", 30000))
    {
        ESP_LOGE(TAG, "WiFi 连接失败");
        return;
    }

    // 等待 WiFi 连接
    int retry_count = 0;
    while (!wifi_mgr.isConnected() && retry_count < 30)
    {
        app::sys::task::TaskManager::delayMs(1000);
        retry_count++;
    }

    if (!wifi_mgr.isConnected())
    {
        ESP_LOGE(TAG, "WiFi 连接超时");
        return;
    }

    ESP_LOGI(TAG, "WiFi 连接成功");

    // 初始化 NTP 管理器
    auto& ntp_mgr = app::protocol::ntp::NTPManager::getInstance();
    if (!ntp_mgr.init())
    {
        ESP_LOGE(TAG, "NTP 管理器初始化失败");
        return;
    }

    // 设置时区（中国标准时间）
    ntp_mgr.setTimezone("CST-8");

    // 设置同步回调
    ntp_mgr.setSyncCallback(
        [](app::protocol::ntp::SyncStatus status)
        {
            const char* status_str = "UNKNOWN";
            switch (status)
            {
            case app::protocol::ntp::SyncStatus::RESET:
                status_str = "RESET";
                break;
            case app::protocol::ntp::SyncStatus::IN_PROGRESS:
                status_str = "IN_PROGRESS";
                break;
            case app::protocol::ntp::SyncStatus::COMPLETED:
                status_str = "COMPLETED";
                break;
            case app::protocol::ntp::SyncStatus::FAILED:
                status_str = "FAILED";
                break;
            }
            ESP_LOGI(TAG, "NTP 同步状态: %s", status_str);
        });

    // 配置 NTP 服务器
    std::vector<std::string> servers = {"pool.ntp.org", "cn.pool.ntp.org", "time.nist.gov"};
    if (!ntp_mgr.configure(servers, app::protocol::ntp::SyncMode::IMMEDIATE))
    {
        ESP_LOGE(TAG, "NTP 配置失败");
        return;
    }

    // 启动 NTP 服务
    if (!ntp_mgr.start())
    {
        ESP_LOGE(TAG, "NTP 启动失败");
        return;
    }

    ESP_LOGI(TAG, "等待 NTP 时间同步...");

    // 等待时间同步（最多 10 秒）
    if (ntp_mgr.waitSync(10000))
    {
        ESP_LOGI(TAG, "NTP 时间同步成功");

        // 显示当前时间
        time_t    now;
        struct tm timeinfo;
        char      strftime_buf[64];

        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "当前时间: %s", strftime_buf);

        // 显示系统启动时间
        int64_t uptime_ms = app::tool::time::uptimeMs();
        ESP_LOGI(TAG, "系统运行时间: %lu 毫秒", (unsigned long)uptime_ms);
    }
    else
    {
        ESP_LOGE(TAG, "NTP 时间同步超时");
    }

    // 持续显示时间（每 5 秒）
    while (true)
    {
        app::sys::task::TaskManager::delayMs(5000);

        time_t    now;
        struct tm timeinfo;
        char      strftime_buf[64];

        time(&now);
        localtime_r(&now, &timeinfo);
        strftime(strftime_buf, sizeof(strftime_buf), "%Y-%m-%d %H:%M:%S", &timeinfo);
        ESP_LOGI(TAG, "当前时间: %s", strftime_buf);
    }
}
