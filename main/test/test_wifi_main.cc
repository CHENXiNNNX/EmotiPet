#include "network/wifi/wifi.hpp"
#include "event.hpp"
#include "task.hpp"

#include "esp_log.h"
#include "nvs_flash.h"

static const char* const TAG = "WiFiTest";

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

    // 获取 WiFi 管理器实例
    auto& wifi_mgr = app::network::wifi::WiFiManager::getInstance();

    // 设置状态回调
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

            const char* reason_str = "NONE";
            switch (reason)
            {
            case app::network::wifi::FailureReason::TIMEOUT:
                reason_str = "TIMEOUT";
                break;
            case app::network::wifi::FailureReason::WRONG_PASSWORD:
                reason_str = "WRONG_PASSWORD";
                break;
            case app::network::wifi::FailureReason::NETWORK_NOT_FOUND:
                reason_str = "NETWORK_NOT_FOUND";
                break;
            case app::network::wifi::FailureReason::CONNECTION_FAILED:
                reason_str = "CONNECTION_FAILED";
                break;
            case app::network::wifi::FailureReason::UNKNOWN:
                reason_str = "UNKNOWN";
                break;
            default:
                break;
            }

            ESP_LOGI(TAG, "WiFi 状态变化: %s, 原因: %s", state_str, reason_str);
        });

    // 初始化 WiFi
    ESP_LOGI(TAG, "=== 初始化 WiFi ===");
    if (!wifi_mgr.init())
    {
        ESP_LOGE(TAG, "WiFi 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "WiFi 初始化成功");
    app::sys::task::TaskManager::delayMs(1000);

    // 测试 1: 扫描 WiFi
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 1: 扫描 WiFi ===");
    if (wifi_mgr.scan(
            [](const std::vector<app::network::wifi::ApInfo>& aps)
            {
                ESP_LOGI(TAG, "扫描完成，找到 %lu 个 AP", (unsigned long)aps.size());
                for (size_t i = 0; i < aps.size() && i < 10; i++)
                {
                    const auto& ap = aps[i];
                    ESP_LOGI(TAG, "  [%lu] SSID: %s, RSSI: %d dBm, 加密: %s", (unsigned long)i,
                             ap.ssid, ap.rssi, ap.is_encrypted ? "是" : "否");
                }
            }))
    {
        ESP_LOGI(TAG, "开始扫描 WiFi...");
        app::sys::task::TaskManager::delayMs(5000);
    }
    else
    {
        ESP_LOGE(TAG, "扫描启动失败");
    }

    app::sys::task::TaskManager::delayMs(2000);

    // 测试 2: 清除已保存的凭证（确保从干净状态开始）
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 2: 清除已保存的凭证 ===");
    if (wifi_mgr.clearCredentials())
    {
        ESP_LOGI(TAG, "已清除保存的凭证");
    }
    else
    {
        ESP_LOGI(TAG, "没有已保存的凭证或清除失败");
    }
    app::sys::task::TaskManager::delayMs(500);

    // 测试 3: 检查是否有已保存的凭证
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 3: 检查是否有已保存的凭证 ===");
    if (wifi_mgr.hasSavedCredentials())
    {
        ESP_LOGI(TAG, "有已保存的凭证");
    }
    else
    {
        ESP_LOGI(TAG, "没有已保存的凭证");
    }
    app::sys::task::TaskManager::delayMs(500);

    // 测试 4: 使用指定的 SSID 和密码连接
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 4: 使用指定凭证连接 WiFi ===");
    if (wifi_mgr.connect("yf", "qwer1234", 30000))
    {
        ESP_LOGI(TAG, "连接请求已发送，等待连接结果...");
        app::sys::task::TaskManager::delayMs(10000);
    }
    else
    {
        ESP_LOGE(TAG, "连接请求发送失败");
    }

    // 测试 5: 获取当前 WiFi 信息
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 5: 获取当前 WiFi 信息 ===");
    auto info = wifi_mgr.getInfo();
    ESP_LOGI(TAG, "状态: %d", static_cast<int>(info.state));
    ESP_LOGI(TAG, "SSID: %s", info.ssid);
    ESP_LOGI(TAG, "IP: %d.%d.%d.%d", info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
    ESP_LOGI(TAG, "RSSI: %d dBm", info.rssi);
    ESP_LOGI(TAG, "是否已连接: %s", wifi_mgr.isConnected() ? "是" : "否");
    app::sys::task::TaskManager::delayMs(2000);

    // 测试 6: 保存凭证
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 6: 保存 WiFi 凭证 ===");
    app::network::wifi::Credentials creds("yf", "qwer1234");
    if (wifi_mgr.saveCredentials(creds))
    {
        ESP_LOGI(TAG, "凭证保存成功");
    }
    else
    {
        ESP_LOGE(TAG, "凭证保存失败");
    }
    app::sys::task::TaskManager::delayMs(500);

    // 测试 7: 获取所有已保存的凭证
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 7: 获取所有已保存的凭证 ===");
    std::vector<app::network::wifi::Credentials> loaded_creds;
    if (wifi_mgr.getCredentials(loaded_creds))
    {
        ESP_LOGI(TAG, "凭证获取成功，共 %u 个网络:", (unsigned int)loaded_creds.size());
        for (size_t i = 0; i < loaded_creds.size(); i++)
        {
            ESP_LOGI(TAG, "  [%u] SSID=%s, Password=%s", (unsigned int)i,
                     loaded_creds[i].ssid, loaded_creds[i].password);
        }
    }
    else
    {
        ESP_LOGE(TAG, "凭证获取失败");
    }
    app::sys::task::TaskManager::delayMs(500);

    // 测试 8: 断开连接
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 8: 断开 WiFi 连接 ===");
    wifi_mgr.disconnect();
    app::sys::task::TaskManager::delayMs(2000);
    ESP_LOGI(TAG, "当前状态: %d", static_cast<int>(wifi_mgr.getState()));
    app::sys::task::TaskManager::delayMs(1000);

    // 测试 9: 使用已保存的凭证连接
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 9: 使用已保存的凭证连接 WiFi ===");
    if (wifi_mgr.connect(nullptr, nullptr, 30000))
    {
        ESP_LOGI(TAG, "连接请求已发送（使用已保存的凭证），等待连接结果...");
        app::sys::task::TaskManager::delayMs(10000);
    }
    else
    {
        ESP_LOGE(TAG, "连接请求发送失败");
    }

    // 测试 10: 再次获取 WiFi 信息
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 10: 再次获取 WiFi 信息 ===");
    info = wifi_mgr.getInfo();
    ESP_LOGI(TAG, "状态: %d", static_cast<int>(info.state));
    ESP_LOGI(TAG, "SSID: %s", info.ssid);
    ESP_LOGI(TAG, "IP: %d.%d.%d.%d", info.ip[0], info.ip[1], info.ip[2], info.ip[3]);
    ESP_LOGI(TAG, "RSSI: %d dBm", info.rssi);
    ESP_LOGI(TAG, "是否已连接: %s", wifi_mgr.isConnected() ? "是" : "否");
    app::sys::task::TaskManager::delayMs(2000);

    // 测试 11: 忘记 WiFi（清除凭证）
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 11: 忘记 WiFi（清除凭证）===");
    if (wifi_mgr.clearCredentials())
    {
        ESP_LOGI(TAG, "凭证已清除");
    }
    else
    {
        ESP_LOGE(TAG, "凭证清除失败");
    }
    app::sys::task::TaskManager::delayMs(500);

    // 测试 12: 验证凭证已清除
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试 12: 验证凭证已清除 ===");
    if (wifi_mgr.hasSavedCredentials())
    {
        ESP_LOGI(TAG, "仍有已保存的凭证");
    }
    else
    {
        ESP_LOGI(TAG, "凭证已成功清除");
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 所有测试完成 ===");
}
