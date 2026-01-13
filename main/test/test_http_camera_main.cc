#include "app/config/config.hpp"
#include "app/i2c/i2c.hpp"
#include "app/media/camera/camera.hpp"
#include "app/network/bluetooth/bluetooth.hpp"
#include "app/network/bluetooth/gatt/gatt.hpp"
#include "app/network/wifi/wifi.hpp"
#include "app/system/event/event.hpp"
#include "app/system/task/task.hpp"
#include "app/media/camera/process/jpeg/encode/jpeg_enc.hpp"

#include <esp_http_server.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <nvs_flash.h>
#include <cstring>

namespace
{
    const char* TAG = "CameraWeb";

    // 全局对象指针
    app::media::camera::Camera* g_camera      = nullptr;
    httpd_handle_t              g_http_server = nullptr;

    // ============================================================================
    // HTTP 处理函数
    // ============================================================================

    // 主页处理器
    esp_err_t index_handler(httpd_req_t* req)
    {
        const char* html =
            "<!DOCTYPE html>"
            "<html>"
            "<head>"
            "<meta charset='utf-8'>"
            "<meta name='viewport' content='width=device-width,initial-scale=1'>"
            "<title>EmotiPet Camera</title>"
            "<style>"
            "body{font-family:Arial,sans-serif;text-align:center;margin:50px;background:#f5f5f5}"
            "h1{color:#333}"
            ".btn{background:#4CAF50;color:white;padding:15px 32px;text-decoration:none;"
            "display:inline-block;font-size:16px;margin:10px;cursor:pointer;border:none;"
            "border-radius:8px;transition:0.3s}"
            ".btn:hover{background:#45a049;transform:scale(1.05)}"
            ".btn-blue{background:#2196F3}"
            ".btn-blue:hover{background:#0b7dda}"
            ".container{background:white;padding:30px;border-radius:10px;box-shadow:0 2px 10px "
            "rgba(0,0,0,0.1);"
            "max-width:600px;margin:0 auto}"
            ".info{color:#666;font-size:14px;margin-top:20px}"
            "</style>"
            "</head>"
            "<body>"
            "<div class='container'>"
            "<h1>📷 EmotiPet Camera</h1>"
            "<p>选择你需要的功能：</p>"
            "<a href='/capture' class='btn'>📸 拍照</a>"
            "<a href='/stream' class='btn btn-blue'>🎥 实时视频</a>"
            "<div class='info'>"
            "<p><strong>说明：</strong></p>"
            "<p>• 拍照：捕获单张图片（JPEG）</p>"
            "<p>• 实时视频：MJPEG 流，适合浏览器直接观看</p>"
            "</div>"
            "</div>"
            "</body>"
            "</html>";

        httpd_resp_set_type(req, "text/html");
        httpd_resp_set_hdr(req, "Content-Encoding", "identity");
        return httpd_resp_send(req, html, strlen(html));
    }

    // 拍照处理器
    esp_err_t capture_handler(httpd_req_t* req)
    {
        if (!g_camera)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "拍照请求");

        // 预热摄像头
        for (int i = 0; i < 3; i++)
        {
            app::media::camera::FrameBuffer warmup_frame;
            g_camera->capture(warmup_frame, 1);
            app::sys::task::TaskManager::delayMs(100);
        }

        // 捕获最终图像
        app::media::camera::FrameBuffer frame;
        if (!g_camera->capture(frame, 2))
        {
            ESP_LOGE(TAG, "捕获失败");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        // 编码为 JPEG
        app::media::camera::process::jpeg::encode::EncodeConfig config;
        config.quality   = 85;
        config.use_psram = true;

        auto jpeg_data = app::media::camera::process::jpeg::encode::encodeYUV422ToJPEG(
            frame.data, frame.res.width, frame.res.height, &config);

        if (!jpeg_data)
        {
            ESP_LOGE(TAG, "JPEG 编码失败");
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        // 发送 JPEG 图像
        httpd_resp_set_type(req, "image/jpeg");
        httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
        esp_err_t res = httpd_resp_send(req, (const char*)jpeg_data.get(), jpeg_data.len());

        ESP_LOGI(TAG, "拍照成功: %dx%d %.1fKB", frame.res.width, frame.res.height,
                 jpeg_data.len() / 1024.0f);

        return res;
    }

    // 视频流处理器
    esp_err_t stream_handler(httpd_req_t* req)
    {
        if (!g_camera)
        {
            httpd_resp_send_500(req);
            return ESP_FAIL;
        }

        ESP_LOGI(TAG, "视频流开始");

        // 设置 MJPEG 流响应类型
        esp_err_t res = httpd_resp_set_type(req, "multipart/x-mixed-replace; boundary=frame");
        if (res != ESP_OK)
        {
            return res;
        }

        // 编码配置
        app::media::camera::process::jpeg::encode::EncodeConfig config;
        config.quality   = 75; // 视频流使用较低质量以提高帧率
        config.use_psram = true;

        int64_t last_frame  = 0;
        int     frame_count = 0;

        while (true)
        {
            // 捕获帧
            app::media::camera::FrameBuffer frame;
            if (!g_camera->capture(frame, 2))
            {
                ESP_LOGE(TAG, "捕获失败");
                break;
            }

            // 编码为 JPEG
            auto jpeg_data = app::media::camera::process::jpeg::encode::encodeYUV422ToJPEG(
                frame.data, frame.res.width, frame.res.height, &config);

            if (!jpeg_data)
            {
                ESP_LOGE(TAG, "JPEG 编码失败");
                break;
            }

            // 发送 MJPEG 帧
            char   part_buf[128];
            size_t hlen = snprintf(part_buf, sizeof(part_buf),
                                   "--frame\r\n"
                                   "Content-Type: image/jpeg\r\n"
                                   "Content-Length: %u\r\n"
                                   "\r\n",
                                   jpeg_data.len());

            res = httpd_resp_send_chunk(req, part_buf, hlen);
            if (res != ESP_OK)
            {
                break;
            }

            res = httpd_resp_send_chunk(req, (const char*)jpeg_data.get(), jpeg_data.len());
            if (res != ESP_OK)
            {
                break;
            }

            res = httpd_resp_send_chunk(req, "\r\n", 2);
            if (res != ESP_OK)
            {
                break;
            }

            // 统计帧率
            frame_count++;
            if (frame_count % 30 == 0)
            {
                int64_t now = esp_timer_get_time();
                if (last_frame > 0)
                {
                    float fps = 30000000.0f / (now - last_frame);
                    ESP_LOGI(TAG, "视频流: %.1fKB %.1ffps", jpeg_data.len() / 1024.0f, fps);
                }
                last_frame = now;
            }
        }

        ESP_LOGI(TAG, "视频流结束");
        return res;
    }

    // ============================================================================
    // HTTP 服务器管理
    // ============================================================================

    bool start_http_server()
    {
        httpd_config_t config   = HTTPD_DEFAULT_CONFIG();
        config.server_port      = 80;
        config.ctrl_port        = 32768;
        config.max_open_sockets = 7;
        config.lru_purge_enable = true;

        ESP_LOGI(TAG, "启动 HTTP 服务器 (端口 %d)...", config.server_port);

        if (httpd_start(&g_http_server, &config) != ESP_OK)
        {
            ESP_LOGE(TAG, "HTTP 服务器启动失败");
            return false;
        }

        // 注册 URI 处理器
        httpd_uri_t index_uri = {
            .uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(g_http_server, &index_uri);

        httpd_uri_t capture_uri = {
            .uri = "/capture", .method = HTTP_GET, .handler = capture_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(g_http_server, &capture_uri);

        httpd_uri_t stream_uri = {
            .uri = "/stream", .method = HTTP_GET, .handler = stream_handler, .user_ctx = nullptr};
        httpd_register_uri_handler(g_http_server, &stream_uri);

        ESP_LOGI(TAG, "HTTP 服务器启动成功");
        return true;
    }

    void stop_http_server()
    {
        if (g_http_server)
        {
            httpd_stop(g_http_server);
            g_http_server = nullptr;
            ESP_LOGI(TAG, "HTTP 服务器已停止");
        }
    }

    // ============================================================================
    // WiFi 和配网回调
    // ============================================================================

    void on_wifi_state_change(app::network::wifi::State         state,
                              app::network::wifi::FailureReason reason)
    {
        auto& provision = app::network::ble::gatt::ProvisionService::getInstance();

        switch (state)
        {
        case app::network::wifi::State::CONNECTED:
        {
            auto& wifi = app::network::wifi::WiFiManager::getInstance();
            auto  info = wifi.getInfo();
            ESP_LOGI(TAG, "WiFi 已连接: %s (%d.%d.%d.%d)", info.ssid, info.ip[0], info.ip[1],
                     info.ip[2], info.ip[3]);
            provision.updateStatus(app::network::ble::gatt::ProvisionStatus::CONNECTED);

            // 启动 HTTP 服务器
            start_http_server();
            break;
        }

        case app::network::wifi::State::DISCONNECTED:
            ESP_LOGI(TAG, "WiFi 已断开");
            provision.updateStatus(app::network::ble::gatt::ProvisionStatus::IDLE);
            stop_http_server();
            break;

        case app::network::wifi::State::FAILED:
            ESP_LOGE(TAG, "WiFi 连接失败");
            switch (reason)
            {
            case app::network::wifi::FailureReason::TIMEOUT:
                provision.updateStatus(app::network::ble::gatt::ProvisionStatus::FAILED_TIMEOUT);
                break;
            case app::network::wifi::FailureReason::WRONG_PASSWORD:
                provision.updateStatus(app::network::ble::gatt::ProvisionStatus::FAILED_WRONG_PWD);
                break;
            case app::network::wifi::FailureReason::NETWORK_NOT_FOUND:
                provision.updateStatus(app::network::ble::gatt::ProvisionStatus::FAILED_NOT_FOUND);
                break;
            default:
                provision.updateStatus(app::network::ble::gatt::ProvisionStatus::FAILED_UNKNOWN);
                break;
            }
            break;

        default:
            break;
        }
    }

    void on_provision_connect(const char* ssid, const char* password)
    {
        ESP_LOGI(TAG, "收到配网请求: %s", ssid);

        auto& provision = app::network::ble::gatt::ProvisionService::getInstance();
        provision.updateStatus(app::network::ble::gatt::ProvisionStatus::CONNECTING);

        auto& wifi = app::network::wifi::WiFiManager::getInstance();
        wifi.connect(ssid, password, 15000);
    }

    void on_provision_disconnect()
    {
        ESP_LOGI(TAG, "收到 WiFi 断开请求");
        auto& wifi = app::network::wifi::WiFiManager::getInstance();
        wifi.disconnect();
    }

} // namespace

// ============================================================================
// 主程序
// ============================================================================

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "    EmotiPet Camera Web Server");
    ESP_LOGI(TAG, "========================================");

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

    // 初始化 I2C
    app::i2c::I2c    i2c;
    app::i2c::Config i2c_cfg;
    i2c_cfg.sda_pin = app::config::I2C_SDA;
    i2c_cfg.scl_pin = app::config::I2C_SCL;
    i2c_cfg.port    = I2C_NUM_1;

    if (!i2c.init(&i2c_cfg))
    {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }

    // 初始化摄像头
    static app::media::camera::Camera camera;
    app::media::camera::Config        cam_cfg;
    cam_cfg.i2c_handle = i2c.getBusHandle();
    cam_cfg.xclk_freq  = app::config::CAM_XCLK_FREQ;

    if (!camera.init(&cam_cfg))
    {
        ESP_LOGE(TAG, "摄像头初始化失败");
        return;
    }

    g_camera = &camera;
    ESP_LOGI(TAG, "摄像头就绪: %s %dx%d", camera.getSensorName().c_str(),
             camera.getResolution().width, camera.getResolution().height);

    // 摄像头预热
    for (int i = 0; i < 3; i++)
    {
        app::media::camera::FrameBuffer frame;
        camera.capture(frame, 1);
        app::sys::task::TaskManager::delayMs(100);
    }

    // 初始化 WiFi
    auto& wifi = app::network::wifi::WiFiManager::getInstance();
    if (!wifi.init())
    {
        ESP_LOGE(TAG, "WiFi 初始化失败");
        return;
    }
    wifi.setStateCallback(on_wifi_state_change);

    // 初始化 BLE
    auto& ble = app::network::ble::Manager::getInstance();
    if (!ble.init("EmotiPet"))
    {
        ESP_LOGE(TAG, "BLE 初始化失败");
        return;
    }

    ble.setStateCallback([](app::network::ble::State state)
                         { ESP_LOGI(TAG, "BLE 状态变化: %d", (int)state); });

    ble.setDisconnectCallback(
        [](const app::network::ble::ConnectionInfo& info, int reason)
        {
            ESP_LOGI(TAG, "BLE 断开: %d", reason);
            auto& ble_mgr = app::network::ble::Manager::getInstance();
            if (!ble_mgr.isAdvertising() && !ble_mgr.isConnected())
            {
                ble_mgr.startAdvertising();
            }
        });

    // 创建 GATT 服务
    auto& device_info = app::network::ble::gatt::DeviceInfoService::getInstance();
    device_info.create("EmotiPet", "EP-CAM-001", "SN001", "1.0.0", "1.0", "1.0.0");

    auto& battery = app::network::ble::gatt::BatteryService::getInstance();
    battery.create();
    battery.updateLevel(100);

    auto& provision = app::network::ble::gatt::ProvisionService::getInstance();
    if (!provision.create())
    {
        ESP_LOGE(TAG, "配网服务创建失败");
        return;
    }
    provision.setConnectCallback(on_provision_connect);
    provision.setDisconnectCallback(on_provision_disconnect);

    // 启动 BLE 服务器
    if (!ble.startServer())
    {
        ESP_LOGE(TAG, "BLE 服务器启动失败");
        return;
    }

    // 开始广播
    app::network::ble::AdvertiseConfig adv_config;
    adv_config.device_name   = "EmotiPet";
    adv_config.min_interval  = 160;
    adv_config.max_interval  = 320;
    adv_config.scan_response = true;

    if (!ble.startAdvertising(adv_config))
    {
        ESP_LOGE(TAG, "BLE 广播启动失败");
        return;
    }

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "系统就绪！");
    ESP_LOGI(TAG, "1. 使用 BLE 工具连接 'EmotiPet'");
    ESP_LOGI(TAG, "2. 配置 WiFi 网络");
    ESP_LOGI(TAG, "3. 浏览器访问设备 IP 地址");
    ESP_LOGI(TAG, "========================================");

    // 主循环
    while (true)
    {
        app::sys::task::TaskManager::delayMs(10000);
    }
}
