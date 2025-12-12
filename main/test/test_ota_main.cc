#include "protocol/http/http.hpp"
#include "protocol/ntp/ntp.hpp"
#include "tool/ota/ota.hpp"
#include "network/network.hpp"
#include "system/event/event.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

static const char* const TAG = "Main";

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    auto& event_mgr = app::sys::event::EventManager::getInstance();
    if (!event_mgr.init())
    {
        ESP_LOGE(TAG, "事件系统初始化失败");
        return;
    }

    auto& http_client = app::protocol::http::HttpClient::getInstance();
    if (!http_client.init())
    {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return;
    }

    auto& ota = app::tool::ota::OtaManager::getInstance();
    if (!ota.init("device_01", "1.0.0"))
    {
        ESP_LOGE(TAG, "OTA 管理器初始化失败");
        return;
    }

    ota.setProgressCallback(
        [](size_t received, size_t total, float percent)
        {
            ESP_LOGI(TAG, "OTA 进度: %u / %u 字节 (%.1f%%)", (unsigned int)received,
                     (unsigned int)total, percent);
        });

    ota.setStatusCallback(
        [](app::tool::ota::OtaStatus status)
        {
            const char* status_str = "Unknown";
            switch (status)
            {
            case app::tool::ota::OtaStatus::IDLE:
                status_str = "空闲";
                break;
            case app::tool::ota::OtaStatus::CHECKING:
                status_str = "检查更新中";
                break;
            case app::tool::ota::OtaStatus::DOWNLOADING:
                status_str = "下载中";
                break;
            case app::tool::ota::OtaStatus::VERIFYING:
                status_str = "验证中";
                break;
            case app::tool::ota::OtaStatus::COMPLETED:
                status_str = "完成";
                break;
            case app::tool::ota::OtaStatus::FAILED:
                status_str = "失败";
                break;
            }
            ESP_LOGI(TAG, "OTA 状态: %s", status_str);
        });

    ota.setCompleteCallback(
        [](bool success, const std::string& error)
        {
            if (success)
            {
                ESP_LOGI(TAG, "OTA 升级成功");
            }
            else
            {
                ESP_LOGE(TAG, "OTA 升级失败: %s", error.c_str());
            }
        });

    auto& provision = app::network::ProvisionManager::getInstance();
    if (!provision.init("EmotiPet"))
    {
        ESP_LOGE(TAG, "配网管理器初始化失败");
        return;
    }

    if (!provision.start())
    {
        ESP_LOGE(TAG, "启动配网失败");
        return;
    }

    int wait_count = 0;
    while (wait_count < 60)
    {
        app::sys::task::TaskManager::delayMs(1000);
        auto status = provision.getStatus();

        if (status == app::network::ProvisionStatus::CONNECTED)
        {
            std::string ssid = provision.getCurrentSsid();
            std::string ip   = provision.getCurrentIp();
            ESP_LOGI(TAG, "WiFi 已连接: %s (%s)", ssid.c_str(), ip.c_str());
            break;
        }

        wait_count++;
    }

    if (provision.getStatus() != app::network::ProvisionStatus::CONNECTED)
    {
        ESP_LOGE(TAG, "WiFi 连接超时");
        return;
    }

    auto& ntp_mgr = app::protocol::ntp::NTPManager::getInstance();
    if (!ntp_mgr.init())
    {
        ESP_LOGE(TAG, "NTP 管理器初始化失败");
        return;
    }

    ntp_mgr.setTimezone("CST-8");
    ntp_mgr.setSyncCallback(
        [](app::protocol::ntp::SyncStatus status)
        {
            if (status == app::protocol::ntp::SyncStatus::COMPLETED)
            {
                ESP_LOGI(TAG, "NTP 时间同步成功");
            }
            else if (status == app::protocol::ntp::SyncStatus::FAILED)
            {
                ESP_LOGE(TAG, "NTP 时间同步失败");
            }
        });

    std::vector<std::string> ntp_servers = {"pool.ntp.org", "cn.pool.ntp.org", "time.nist.gov"};
    if (!ntp_mgr.configure(ntp_servers, app::protocol::ntp::SyncMode::IMMEDIATE))
    {
        ESP_LOGE(TAG, "NTP 配置失败");
        return;
    }

    if (!ntp_mgr.start())
    {
        ESP_LOGE(TAG, "NTP 启动失败");
        return;
    }

    ntp_mgr.waitSync(10000);

    const std::string server_url = "http://10.93.1.49:5000";
    app::sys::task::TaskManager::delayMs(2000);

    if (ota.checkUpdate(server_url))
    {
        app::tool::ota::FirmwareInfo firmware_info;
        if (ota.getFirmwareInfo(server_url, firmware_info))
        {
            ESP_LOGI(TAG, "固件版本: %s, 大小: %u 字节", firmware_info.version.c_str(),
                     (unsigned int)firmware_info.size);
            app::sys::task::TaskManager::delayMs(3000);
            if (ota.startUpdate(server_url, firmware_info))
            {
                ESP_LOGI(TAG, "OTA 升级已启动，等待下载完成...");
            }
            else
            {
                ESP_LOGE(TAG, "OTA 升级启动失败");
            }
        }
        else
        {
            ESP_LOGE(TAG, "获取固件信息失败");
        }
    }

    while (true)
    {
        app::sys::task::TaskManager::delayMs(10000);
    }
}
