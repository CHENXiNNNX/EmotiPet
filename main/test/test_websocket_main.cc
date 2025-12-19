#include "common/chatbot/handle/message/message.hpp"
#include "network/wifi/wifi.hpp"
#include "protocol/websocket/websocket.hpp"
#include "system/event/event.hpp"
#include "system/task/task.hpp"
#include "tool/uuid/uuid.hpp"

#include "esp_log.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char* const TAG = "WebSocketTest";

// WiFi 配置（如果没有保存的凭证，将使用这些）
#define WIFI_SSID     "yf"      // WiFi SSID
#define WIFI_PASSWORD "qwer1234"  // WiFi 密码

// WebSocket 服务器配置
#define WS_SERVER_URI "ws://10.93.1.49:8080"  // 服务器地址和端口

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== WebSocket 连接服务器测试开始 ===\n");

    // ========== 1. 初始化 NVS ==========
    ESP_LOGI(TAG, "[1] 初始化 NVS...");
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化成功");

    // ========== 2. 初始化事件系统 ==========
    ESP_LOGI(TAG, "[2] 初始化事件系统...");
    auto& event_mgr = app::sys::event::EventManager::getInstance();
    if (!event_mgr.init())
    {
        ESP_LOGE(TAG, "事件系统初始化失败");
        return;
    }
    ESP_LOGI(TAG, "事件系统初始化成功");

    // ========== 3. 初始化 WiFi ==========
    ESP_LOGI(TAG, "[3] 初始化 WiFi...");
    auto& wifi_mgr = app::network::wifi::WiFiManager::getInstance();

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

    if (!wifi_mgr.init())
    {
        ESP_LOGE(TAG, "WiFi 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "WiFi 初始化成功");

    // ========== 4. 连接 WiFi ==========
    ESP_LOGI(TAG, "[4] 连接 WiFi...");
    bool wifi_connected = false;

    // 尝试使用已保存的凭证连接
    if (wifi_mgr.hasSavedCredentials())
    {
        ESP_LOGI(TAG, "使用已保存的 WiFi 凭证连接...");
        wifi_connected = wifi_mgr.connect(nullptr, nullptr, 30000);
    }

    // 如果没有保存的凭证或连接失败，使用指定的 SSID 和密码
    if (!wifi_connected)
    {
        ESP_LOGI(TAG, "使用指定的 WiFi 凭证连接...");
        ESP_LOGI(TAG, "SSID: %s", WIFI_SSID);
        wifi_connected = wifi_mgr.connect(WIFI_SSID, WIFI_PASSWORD, 30000);
    }

    if (!wifi_connected)
    {
        ESP_LOGE(TAG, "WiFi 连接请求发送失败");
        return;
    }

    // 等待 WiFi 连接（最多 30 秒）
    ESP_LOGI(TAG, "等待 WiFi 连接...");
    int wait_count = 0;
    while (!wifi_mgr.isConnected() && wait_count < 300)
    {
        app::sys::task::TaskManager::delayMs(100);
        wait_count++;
    }

    if (!wifi_mgr.isConnected())
    {
        ESP_LOGE(TAG, "WiFi 连接超时");
        return;
    }

    // 显示 WiFi 信息
    auto wifi_info = wifi_mgr.getInfo();
    ESP_LOGI(TAG, "WiFi 连接成功!");
    ESP_LOGI(TAG, "  SSID: %s", wifi_info.ssid);
    ESP_LOGI(TAG, "  IP: %d.%d.%d.%d", wifi_info.ip[0], wifi_info.ip[1], wifi_info.ip[2],
             wifi_info.ip[3]);
    ESP_LOGI(TAG, "  RSSI: %d dBm", wifi_info.rssi);

    app::sys::task::TaskManager::delayMs(2000);

    // ========== 5. 初始化 WebSocket 客户端 ==========
    ESP_LOGI(TAG, "[5] 初始化 WebSocket 客户端...");
    using namespace app::protocol::websocket;
    using namespace app::common::chatbot::handle::message;

    Config ws_config;
    ws_config.uri                  = WS_SERVER_URI;
    ws_config.ping_interval_sec    = 10;
    ws_config.pingpong_timeout_sec = 10;
    ws_config.reconnect_timeout_ms = 10000;
    ws_config.network_timeout_ms   = 10000;
    ws_config.disable_auto_reconnect = false;

    auto& ws_client = WebSocketClient::getInstance();

    if (!ws_client.init(ws_config))
    {
        ESP_LOGE(TAG, "WebSocket 客户端初始化失败");
        return;
    }
    ESP_LOGI(TAG, "WebSocket 客户端初始化成功");

    // ========== 6. 设置 WebSocket 回调 ==========
    ESP_LOGI(TAG, "[6] 设置 WebSocket 回调...");

    // 连接成功回调
    ws_client.setConnectedCallback([]() { ESP_LOGI(TAG, "WebSocket 已连接"); });

    // 断开连接回调
    ws_client.setDisconnectedCallback([]() { ESP_LOGW(TAG, "WebSocket 已断开"); });

    // 数据接收回调
    ws_client.setDataCallback(
        [](const DataEvent& event)
        {
            if (event.is_text)
            {
                std::string json_str(reinterpret_cast<const char*>(event.data), event.length);
                ESP_LOGI(TAG, "收到文本消息:");
                ESP_LOGI(TAG, "%s", json_str.c_str());

                // 解析消息类型
                MessageType msg_type = getMessageType(json_str);
                ESP_LOGI(TAG, "消息类型: %d", static_cast<int>(msg_type));

                switch (msg_type)
                {
                case MessageType::HELLO_ACK:
                {
                    Features features;
                    if (parseHelloAck(json_str, features))
                    {
                        ESP_LOGI(TAG, "HelloAck 解析成功:");
                        ESP_LOGI(TAG, "  aec: %s", features.aec ? "true" : "false");
                        ESP_LOGI(TAG, "  mcp: %s", features.mcp ? "true" : "false");
                    }
                    break;
                }
                case MessageType::COMMAND:
                {
                    std::string from, to, timestamp;
                    CommandData cmd_data;
                    if (parseCommand(json_str, from, to, timestamp, cmd_data))
                    {
                        ESP_LOGI(TAG, "Command 解析成功:");
                        ESP_LOGI(TAG, "  from: %s", from.c_str());
                        ESP_LOGI(TAG, "  to: %s", to.c_str());
                        ESP_LOGI(TAG, "  cmd: %s", cmd_data.cmd.c_str());
                        ESP_LOGI(TAG, "  sound_id: %s", cmd_data.sound_id.c_str());
                        ESP_LOGI(TAG, "  reason: %s", cmd_data.reason.c_str());
                    }
                    break;
                }
                case MessageType::RES_SYNC:
                {
                    std::string from, to, timestamp, data;
                    if (parseResSync(json_str, from, to, timestamp, data))
                    {
                        ESP_LOGI(TAG, "ResSync 解析成功:");
                        ESP_LOGI(TAG, "  from: %s", from.c_str());
                        ESP_LOGI(TAG, "  to: %s", to.c_str());
                        ESP_LOGI(TAG, "  data: %s", data.c_str());
                    }
                    break;
                }
                case MessageType::ERROR:
                {
                    std::string from, to, timestamp;
                    ErrorData error_data;
                    if (parseError(json_str, from, to, timestamp, error_data))
                    {
                        ESP_LOGE(TAG, "Error 消息:");
                        ESP_LOGE(TAG, "  code: %d", error_data.code);
                        ESP_LOGE(TAG, "  message: %s", error_data.message.c_str());
                    }
                    break;
                }
                default:
                    ESP_LOGW(TAG, "未知消息类型或无法解析");
                    break;
                }
            }
            else
            {
                ESP_LOGI(TAG, "收到二进制数据，长度: %d", event.length);
            }
        });

    // 错误回调
    ws_client.setErrorCallback(
        [](const ErrorEvent& event) { ESP_LOGE(TAG, "WebSocket 错误: %s", event.message.c_str()); });

    // ========== 7. 连接到 WebSocket 服务器 ==========
    ESP_LOGI(TAG, "[7] 连接到 WebSocket 服务器: %s", WS_SERVER_URI);
    if (!ws_client.connect())
    {
        ESP_LOGE(TAG, "启动 WebSocket 连接失败");
        return;
    }

    // 等待连接建立（最多 10 秒）
    ESP_LOGI(TAG, "等待 WebSocket 连接...");
    wait_count = 0;
    while (!ws_client.isConnected() && wait_count < 100)
    {
        app::sys::task::TaskManager::delayMs(100);
        wait_count++;
    }

    if (!ws_client.isConnected())
    {
        ESP_LOGE(TAG, "WebSocket 连接超时");
        return;
    }

    ESP_LOGI(TAG, "WebSocket 连接成功!");

    app::sys::task::TaskManager::delayMs(1000);

    // ========== 8. 构建并发送 hello 消息 ==========
    ESP_LOGI(TAG, "[8] 构建并发送 hello 消息...");

    // 准备设备信息（实际使用时应该从系统获取）
    std::string device_id = "aa:bb:cc:dd:ee:ff";  // TODO: 使用实际的 MAC 地址
    std::string client_id = "test-uuid-12345";    // TODO: 使用实际的 UUID

    // 可以生成一个 UUID
    auto uuid = app::tool::uuid::generate();
    char uuid_str[37];
    if (app::tool::uuid::toString(uuid, uuid_str, sizeof(uuid_str)))
    {
        client_id = uuid_str;
        ESP_LOGI(TAG, "生成的 UUID: %s", client_id.c_str());
    }

    // 构建 hello 消息
    AudioParams audio_params;
    audio_params.format         = "opus";
    audio_params.sample_rate    = 16000;
    audio_params.channels       = 1;
    audio_params.frame_duration = 60;

    Features features;
    features.aec = false;
    features.mcp = false;

    std::string hello_msg = buildHelloMessage(device_id, client_id, features, audio_params);
    if (hello_msg.empty())
    {
        ESP_LOGE(TAG, "构建 hello 消息失败");
        return;
    }

    ESP_LOGI(TAG, "发送 hello 消息:");
    ESP_LOGI(TAG, "%s", hello_msg.c_str());

    int sent = ws_client.sendText(hello_msg);
    if (sent > 0)
    {
        ESP_LOGI(TAG, "Hello 消息发送成功，长度: %d 字节", sent);
    }
    else
    {
        ESP_LOGE(TAG, "Hello 消息发送失败");
    }

    // ========== 9. 等待并接收响应 ==========
    ESP_LOGI(TAG, "[9] 等待服务器响应...");
    app::sys::task::TaskManager::delayMs(5000);

    // ========== 10. 保持连接一段时间 ==========
    ESP_LOGI(TAG, "[10] 保持连接 10 秒...");
    app::sys::task::TaskManager::delayMs(10000);

    // ========== 11. 断开连接 ==========
    ESP_LOGI(TAG, "[11] 断开 WebSocket 连接...");
    ws_client.disconnect();
    app::sys::task::TaskManager::delayMs(1000);

    // ========== 12. 清理 ==========
    ESP_LOGI(TAG, "[12] 清理资源...");
    ws_client.deinit();

    ESP_LOGI(TAG, "\n=== WebSocket 连接服务器测试完成 ===");
}
