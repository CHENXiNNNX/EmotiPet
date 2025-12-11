#include "network/bluetooth/bluetooth.hpp"
#include "network/bluetooth/gatt/gatt.hpp"
#include "network/wifi/wifi.hpp"
#include "system/event/event.hpp"

#include "esp_log.h"
#include "nvs_flash.h"
#include "system/task/task.hpp"

static const char* const TAG = "BLE_Test";

// WiFi 连接回调（通过蓝牙配网触发）
static void onProvisionConnect(const char* ssid, const char* password)
{
    ESP_LOGI(TAG, "收到配网请求: SSID=%s", ssid);

    auto& provision = app::network::ble::gatt::ProvisionService::getInstance();
    provision.updateStatus(app::network::ble::gatt::ProvisionStatus::CONNECTING);

    // 尝试连接 WiFi
    auto& wifi = app::network::wifi::WiFiManager::getInstance();
    if (wifi.connect(ssid, password, 15000))
    {
        ESP_LOGI(TAG, "WiFi 连接请求已发送");
    }
    else
    {
        ESP_LOGE(TAG, "WiFi 连接请求失败");
        provision.updateStatus(app::network::ble::gatt::ProvisionStatus::FAILED_UNKNOWN);
    }
}

// WiFi 状态回调
static void onWiFiStateChange(app::network::wifi::State         state,
                              app::network::wifi::FailureReason reason)
{
    auto& provision = app::network::ble::gatt::ProvisionService::getInstance();

    switch (state)
    {
    case app::network::wifi::State::CONNECTED:
        ESP_LOGI(TAG, "WiFi 已连接");
        provision.updateStatus(app::network::ble::gatt::ProvisionStatus::CONNECTED);
        break;

    case app::network::wifi::State::FAILED:
        ESP_LOGE(TAG, "WiFi 连接失败: %d", static_cast<int>(reason));
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

    case app::network::wifi::State::DISCONNECTED:
        ESP_LOGI(TAG, "WiFi 已断开");
        provision.updateStatus(app::network::ble::gatt::ProvisionStatus::IDLE);
        break;

    default:
        break;
    }
}

// BLE 状态回调
static void onBLEStateChange(app::network::ble::State state)
{
    const char* state_str = "Unknown";
    switch (state)
    {
    case app::network::ble::State::UNINITIALIZED:
        state_str = "UNINITIALIZED";
        break;
    case app::network::ble::State::IDLE:
        state_str = "IDLE";
        break;
    case app::network::ble::State::ADVERTISING:
        state_str = "ADVERTISING";
        break;
    case app::network::ble::State::CONNECTED:
        state_str = "CONNECTED";
        break;
    }
    ESP_LOGI(TAG, "BLE 状态: %s", state_str);
}

// BLE 连接回调
static void onBLEConnect(const app::network::ble::ConnectionInfo& info)
{
    ESP_LOGI(TAG, "BLE 设备连接: handle=%d, addr=%02X:%02X:%02X:%02X:%02X:%02X", info.conn_handle,
             info.address[5], info.address[4], info.address[3], info.address[2], info.address[1],
             info.address[0]);
}

// BLE 断开回调
static void onBLEDisconnect(const app::network::ble::ConnectionInfo& info, int reason)
{
    ESP_LOGI(TAG, "BLE 设备断开: handle=%d, reason=%d", info.conn_handle, reason);

    // 断开后重新开始广播
    auto& ble = app::network::ble::Manager::getInstance();
    if (!ble.isAdvertising() && !ble.isConnected())
    {
        ble.startAdvertising();
    }
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========== BLE 测试开始 ==========");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS 初始化完成");

    // 初始化事件系统
    auto& event_mgr = app::sys::event::EventManager::getInstance();
    if (!event_mgr.init())
    {
        ESP_LOGE(TAG, "事件系统初始化失败");
        return;
    }
    ESP_LOGI(TAG, "事件系统初始化完成");

    // 初始化 WiFi（用于配网）
    auto& wifi = app::network::wifi::WiFiManager::getInstance();
    if (!wifi.init())
    {
        ESP_LOGE(TAG, "WiFi 初始化失败");
        return;
    }
    wifi.setStateCallback(onWiFiStateChange);
    ESP_LOGI(TAG, "WiFi 初始化完成");

    // 初始化 BLE
    auto& ble = app::network::ble::Manager::getInstance();
    if (!ble.init("EmotiPet"))
    {
        ESP_LOGE(TAG, "BLE 初始化失败");
        return;
    }

    // 设置 BLE 回调
    ble.setStateCallback(onBLEStateChange);
    ble.setConnectCallback(onBLEConnect);
    ble.setDisconnectCallback(onBLEDisconnect);
    ESP_LOGI(TAG, "BLE 初始化完成, 地址: %s", ble.getAddressString().c_str());

    // 创建 GATT 服务
    // 5.1 设备信息服务
    auto& device_info = app::network::ble::gatt::DeviceInfoService::getInstance();
    if (!device_info.create("EmotiPet", "EP-001", "SN001", "1.0.0", "1.0", "1.0.0"))
    {
        ESP_LOGE(TAG, "设备信息服务创建失败");
    }
    else
    {
        ESP_LOGI(TAG, "设备信息服务创建完成");
    }

    // 5.2 电池服务
    auto& battery = app::network::ble::gatt::BatteryService::getInstance();
    if (!battery.create())
    {
        ESP_LOGE(TAG, "电池服务创建失败");
    }
    else
    {
        ESP_LOGI(TAG, "电池服务创建完成");
        battery.updateLevel(85); // 设置初始电量 85%
    }

    // 5.3 配网服务
    auto& provision = app::network::ble::gatt::ProvisionService::getInstance();
    if (!provision.create())
    {
        ESP_LOGE(TAG, "配网服务创建失败");
    }
    else
    {
        provision.setConnectCallback(onProvisionConnect);
        provision.setDisconnectCallback(
            []()
            {
                ESP_LOGI(TAG, "收到断开 WiFi 请求");
                app::network::wifi::WiFiManager::getInstance().disconnect();
            });
        ESP_LOGI(TAG, "配网服务创建完成");
    }

    //  启动 BLE 服务器
    if (!ble.startServer())
    {
        ESP_LOGE(TAG, "BLE 服务器启动失败");
        return;
    }
    ESP_LOGI(TAG, "BLE 服务器启动完成");

    //  开始广播
    app::network::ble::AdvertiseConfig adv_config;
    adv_config.device_name   = "EmotiPet";
    adv_config.min_interval  = 160; // 100ms
    adv_config.max_interval  = 320; // 200ms
    adv_config.scan_response = true;

    if (!ble.startAdvertising(adv_config))
    {
        ESP_LOGE(TAG, "BLE 广播启动失败");
        return;
    }
    ESP_LOGI(TAG, "BLE 广播已启动");

    ESP_LOGI(TAG, "========== BLE 测试就绪 ==========");
    ESP_LOGI(TAG, "请使用手机 BLE 调试工具连接设备 'EmotiPet'");
    ESP_LOGI(TAG, "配网服务 UUID: %s", app::network::ble::gatt::UUID::PROVISION_SERVICE);

    // 主循环 - 模拟电池电量变化
    uint8_t battery_level = 85;
    int     counter       = 0;

    while (true)
    {
        sys::task::TaskManager::delayMs(pdMS_TO_TICKS(5000)); // 5秒更新一次

        counter++;

        // 每 30 秒打印状态
        if (counter % 6 == 0)
        {
            ESP_LOGI(TAG, "状态: BLE=%s, WiFi=%s, 连接数=%d",
                     ble.isConnected() ? "已连接" : (ble.isAdvertising() ? "广播中" : "空闲"),
                     wifi.isConnected() ? "已连接" : "未连接", ble.getConnectedCount());
        }

        // 模拟电池电量下降
        if (battery_level > 10)
        {
            battery_level--;
            battery.updateLevel(battery_level);
        }
        else
        {
            battery_level = 100; // 重置
        }
    }
}
