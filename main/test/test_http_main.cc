#include "app/app.hpp"
#include "app/network/wifi/wifi.hpp"
#include "app/protocol/http/http.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "esp_netif.h"
#include "nvs_flash.h"

static const char* const TAG = "HTTP_Test";

// WiFi 配置（请修改为您的 WiFi 信息）
#define WIFI_SSID "yf"
#define WIFI_PASSWORD "qwer1234"

extern "C" void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(esp_netif_init());

    auto& wifi_manager = app::network::wifi::WiFiManager::getInstance();
    if (!wifi_manager.init())
    {
        ESP_LOGE(TAG, "WiFi 管理器初始化失败");
        return;
    }

    wifi_manager.setStateCallback(
        [](app::network::wifi::State state, app::network::wifi::FailureReason reason)
        {
            if (state == app::network::wifi::State::FAILED)
            {
                const char* reason_str = "未知错误";
                switch (reason)
                {
                case app::network::wifi::FailureReason::TIMEOUT:
                    reason_str = "连接超时";
                    break;
                case app::network::wifi::FailureReason::WRONG_PASSWORD:
                    reason_str = "密码错误";
                    break;
                case app::network::wifi::FailureReason::NETWORK_NOT_FOUND:
                    reason_str = "网络未找到";
                    break;
                case app::network::wifi::FailureReason::CONNECTION_FAILED:
                    reason_str = "连接失败";
                    break;
                default:
                    break;
                }
                ESP_LOGE(TAG, "WiFi 连接失败: %s", reason_str);
            }
        });

    bool connected = false;
    if (wifi_manager.hasSavedCredentials())
    {
        connected = wifi_manager.connect(nullptr, nullptr, 30000);
    }

    if (!connected)
    {
        connected = wifi_manager.connect(WIFI_SSID, WIFI_PASSWORD, 30000);
    }

    if (!connected)
    {
        ESP_LOGE(TAG, "WiFi 连接启动失败");
        return;
    }

    int       retry_count = 0;
    const int max_retries = 60;
    while (!wifi_manager.isConnected() && retry_count < max_retries)
    {
        app::sys::task::TaskManager::delayMs(1000);
        retry_count++;
    }

    if (!wifi_manager.isConnected())
    {
        ESP_LOGE(TAG, "WiFi 连接超时");
        return;
    }

    const auto& wifi_info = wifi_manager.getInfo();
    ESP_LOGI(TAG, "WiFi 已连接: %s (%d.%d.%d.%d)", wifi_info.ssid, wifi_info.ip[0], wifi_info.ip[1],
             wifi_info.ip[2], wifi_info.ip[3]);

    auto& http_client = app::protocol::http::HttpClient::getInstance();
    if (!http_client.init())
    {
        ESP_LOGE(TAG, "HTTP 客户端初始化失败");
        return;
    }

    app::sys::task::TaskManager::delayMs(1000);

    app::protocol::http::HttpResponse response;
    if (http_client.get("http://httpbin.org/get", response, 10000))
    {
        ESP_LOGI(TAG, "GET 请求成功，状态码: %d", response.status_code_int);
    }
    else
    {
        ESP_LOGE(TAG, "GET 请求失败");
    }

    app::sys::task::TaskManager::delayMs(2000);

    std::string json_data = R"({"test": "data", "value": 123})";
    response              = app::protocol::http::HttpResponse();
    if (http_client.post("http://httpbin.org/post", json_data, response, 10000))
    {
        ESP_LOGI(TAG, "POST 请求成功，状态码: %d", response.status_code_int);
    }
    else
    {
        ESP_LOGE(TAG, "POST 请求失败");
    }

    app::sys::task::TaskManager::delayMs(2000);

    app::protocol::http::HttpRequest request;
    request.url                        = "http://httpbin.org/headers";
    request.method                     = app::protocol::http::HttpMethod::GET;
    request.timeout_ms                 = 10000;
    request.headers["User-Agent"]      = "EmotiPet/1.0";
    request.headers["X-Custom-Header"] = "test-value";

    response = app::protocol::http::HttpResponse();
    if (http_client.perform(request, response))
    {
        ESP_LOGI(TAG, "完整请求成功，状态码: %d", response.status_code_int);
    }
    else
    {
        ESP_LOGE(TAG, "完整请求失败");
    }

    app::sys::task::TaskManager::delayMs(2000);

    request.url    = "http://httpbin.org/get";
    request.method = app::protocol::http::HttpMethod::GET;

    bool callback_success = false;
    http_client.perform(
        request,
        [&callback_success](const app::protocol::http::HttpResponse& resp) -> bool
        {
            callback_success = (resp.status_code == app::protocol::http::HttpStatus::OK);
            return true;
        },
        nullptr);

    if (callback_success)
    {
        ESP_LOGI(TAG, "回调方式测试成功");
    }
    else
    {
        ESP_LOGE(TAG, "回调方式测试失败");
    }

    while (true)
    {
        app::sys::task::TaskManager::delayMs(10000);
    }
}
