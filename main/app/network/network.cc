#include "network.hpp"

#include "bluetooth/bluetooth.hpp"
#include "bluetooth/gatt/gatt.hpp"
#include "wifi/wifi.hpp"
#include "system/task/task.hpp"

#include "esp_log.h"
#include <string>
#include <vector>

static const char* const TAG = "Provision";

namespace app
{
    namespace network
    {

        // ==================== 单例实现 ====================

        ProvisionManager& ProvisionManager::getInstance()
        {
            static ProvisionManager instance;
            return instance;
        }

        ProvisionManager::ProvisionManager()
            : initialized_(false), provisioning_(false), trying_saved_(false),
              status_(ProvisionStatus::IDLE)
        {
        }

        ProvisionManager::~ProvisionManager()
        {
            deinit();
        }

        // ==================== 初始化和反初始化 ====================

        bool ProvisionManager::init(const char* ble_device_name)
        {
            if (initialized_)
            {
                ESP_LOGW(TAG, "已初始化");
                return true;
            }

            // 初始化 WiFi
            auto& wifi_mgr = wifi::WiFiManager::getInstance();
            if (!wifi_mgr.init())
            {
                ESP_LOGE(TAG, "WiFi 初始化失败");
                return false;
            }

            // 设置 WiFi 状态回调
            wifi_mgr.setStateCallback(
                [this](wifi::State state, wifi::FailureReason reason)
                { onWiFiStateChanged(static_cast<int>(state), static_cast<int>(reason)); });

            // 初始化 BLE
            auto& ble_mgr = ble::Manager::getInstance();
            if (!ble_mgr.init(ble_device_name))
            {
                ESP_LOGE(TAG, "BLE 初始化失败");
                return false;
            }

            // 设置 BLE 断开回调：断开后自动恢复广播（如果还在配网状态）
            ble_mgr.setDisconnectCallback(
                [this](const ble::ConnectionInfo& info, int reason)
                {
                    if (provisioning_)
                    {
                        auto& ble = ble::Manager::getInstance();
                        if (!ble.isAdvertising() && !ble.isConnected())
                        {
                            // 延迟 500ms 后恢复广播，避免立即重连
                            app::sys::task::TaskManager::delayMs(500);

                            // 再次检查状态（可能在延迟期间状态已改变）
                            if (provisioning_ && !ble.isAdvertising() && !ble.isConnected())
                            {
                                ESP_LOGI(TAG, "设备断开，自动恢复广播");
                                ble::AdvertiseConfig config;
                                config.device_name   = "EmotiPet";
                                config.min_interval  = 160; // 100ms
                                config.max_interval  = 320; // 200ms
                                config.connectable   = true;
                                config.scan_response = true;
                                ble.startAdvertising(config);
                            }
                        }
                    }
                });

            // 创建配网服务
            auto& provision_svc = ble::gatt::ProvisionService::getInstance();
            if (!provision_svc.create())
            {
                ESP_LOGE(TAG, "创建配网服务失败");
                return false;
            }

            // 设置配网服务回调
            provision_svc.setConnectCallback([this](const char* ssid, const char* password)
                                             { onProvisionConnect(ssid, password); });
            provision_svc.setDisconnectCallback([this]() { onProvisionDisconnect(); });
            provision_svc.setCommandCallback([this](ble::gatt::ProvisionCommand cmd)
                                             { onProvisionCommand(static_cast<uint8_t>(cmd)); });

            // 创建设备信息服务
            auto& device_info = ble::gatt::DeviceInfoService::getInstance();
            device_info.create();

            // 创建电池服务
            auto& battery = ble::gatt::BatteryService::getInstance();
            battery.create();

            // 启动 BLE 服务器
            if (!ble_mgr.startServer())
            {
                ESP_LOGE(TAG, "启动 BLE 服务器失败");
                return false;
            }

            initialized_ = true;
            status_      = ProvisionStatus::IDLE;
            return true;
        }

        void ProvisionManager::deinit()
        {
            if (!initialized_)
            {
                return;
            }

            stop();

            auto& ble_mgr = ble::Manager::getInstance();
            ble_mgr.deinit();

            auto& wifi_mgr = wifi::WiFiManager::getInstance();
            wifi_mgr.deinit();

            initialized_ = false;
            status_      = ProvisionStatus::IDLE;
            current_ssid_.clear();
            current_ip_.clear();
        }

        // ==================== 配网控制 ====================

        bool ProvisionManager::start()
        {
            if (!initialized_)
            {
                ESP_LOGE(TAG, "未初始化");
                return false;
            }

            if (provisioning_)
            {
                ESP_LOGW(TAG, "已在配网中");
                return true;
            }

            auto& wifi_mgr = wifi::WiFiManager::getInstance();

            // 先检查是否有保存的网络凭证
            if (wifi_mgr.hasSavedCredentials())
            {
                trying_saved_ = true;
                updateStatus(ProvisionStatus::CONNECTING);

                // 尝试连接已保存的网络（30秒超时）
                if (wifi_mgr.connect(nullptr, nullptr, 30000))
                {
                    // 连接是异步的，等待 WiFi 状态回调处理结果
                    return true;
                }
                else
                {
                    trying_saved_ = false;
                }
            }

            return startProvisioning();
        }

        bool ProvisionManager::startProvisioning()
        {
            auto& ble_mgr = ble::Manager::getInstance();

            // 配置广播参数
            ble::AdvertiseConfig config;
            config.device_name   = "EmotiPet";
            config.min_interval  = 160; // 100ms
            config.max_interval  = 320; // 200ms
            config.connectable   = true;
            config.scan_response = true;

            if (!ble_mgr.startAdvertising(config))
            {
                ESP_LOGE(TAG, "启动广播失败");
                return false;
            }

            provisioning_ = true;
            updateStatus(ProvisionStatus::IDLE);
            return true;
        }

        bool ProvisionManager::stop()
        {
            if (!initialized_ || !provisioning_)
            {
                return true;
            }

            auto& ble_mgr = ble::Manager::getInstance();
            if (!ble_mgr.stopAdvertising())
            {
                ESP_LOGE(TAG, "停止广播失败");
                return false;
            }

            provisioning_ = false;
            updateStatus(ProvisionStatus::IDLE);
            return true;
        }

        // ==================== 状态查询 ====================

        bool ProvisionManager::isProvisioning() const
        {
            return provisioning_;
        }

        ProvisionStatus ProvisionManager::getStatus() const
        {
            return status_;
        }

        std::string ProvisionManager::getCurrentSsid() const
        {
            return current_ssid_;
        }

        std::string ProvisionManager::getCurrentIp() const
        {
            return current_ip_;
        }

        // ==================== 回调设置 ====================

        void ProvisionManager::setStatusCallback(ProvisionStatusCallback callback)
        {
            status_callback_ = std::move(callback);
        }

        void ProvisionManager::setCompleteCallback(ProvisionCompleteCallback callback)
        {
            complete_callback_ = std::move(callback);
        }

        // ==================== WiFi 操作 ====================

        bool ProvisionManager::scanWiFi() const
        {
            if (!initialized_)
            {
                return false;
            }

            auto& wifi_mgr = wifi::WiFiManager::getInstance();
            return wifi_mgr.scan();
        }

        // ==================== BLE 配网服务回调 ====================

        void ProvisionManager::onProvisionConnect(const char* ssid, const char* password)
        {
            if (!ssid || strlen(ssid) == 0)
            {
                ESP_LOGW(TAG, "SSID 为空，无法连接");
                return;
            }

            auto& wifi_mgr      = wifi::WiFiManager::getInstance();
            auto& provision_svc = ble::gatt::ProvisionService::getInstance();

            updateStatus(ProvisionStatus::CONNECTING);
            provision_svc.updateStatus(ble::gatt::ProvisionStatus::CONNECTING);

            // 连接 WiFi（30秒超时）
            bool success = wifi_mgr.connect(ssid, password, 30000);

            if (!success)
            {
                updateStatus(ProvisionStatus::FAILED_UNKNOWN);
                provision_svc.updateStatus(ble::gatt::ProvisionStatus::FAILED_UNKNOWN);
                ESP_LOGE(TAG, "WiFi 连接失败: SSID=%s", ssid);
            }
        }

        void ProvisionManager::onProvisionDisconnect()
        {
            auto& wifi_mgr = wifi::WiFiManager::getInstance();
            wifi_mgr.disconnect();

            updateStatus(ProvisionStatus::IDLE);
            auto& provision_svc = ble::gatt::ProvisionService::getInstance();
            provision_svc.updateStatus(ble::gatt::ProvisionStatus::IDLE);
        }

        void ProvisionManager::onProvisionCommand(uint8_t cmd)
        {
            auto& wifi_mgr      = wifi::WiFiManager::getInstance();
            auto& provision_svc = ble::gatt::ProvisionService::getInstance();

            switch (cmd)
            {
            case 0x03: // SCAN
            {
                wifi_mgr.scan();
                break;
            }

            case 0x04: // SAVE
            {
                auto&       provision_svc_ref = ble::gatt::ProvisionService::getInstance();
                std::string ssid              = provision_svc_ref.getSsid();
                std::string password          = provision_svc_ref.getPassword();

                if (!ssid.empty())
                {
                    wifi::Credentials creds(ssid.c_str(), password.c_str());
                    if (!wifi_mgr.saveCredentials(creds))
                    {
                        ESP_LOGE(TAG, "保存凭证失败");
                    }
                }
                break;
            }

            case 0x05: // CLEAR
            {
                wifi_mgr.clearCredentials();
                ESP_LOGI(TAG, "WiFi 凭证已清除");
                break;
            }

            case 0x10: // GET_STATUS
                // 状态已通过通知自动发送，无需额外处理
                break;

            case 0x11: // GET_IP
            {
                auto info = wifi_mgr.getInfo();
                if (info.state == wifi::State::CONNECTED)
                {
                    char ip_str[16];
                    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", info.ip[0], info.ip[1],
                             info.ip[2], info.ip[3]);
                    provision_svc.sendStatusData(reinterpret_cast<const uint8_t*>(ip_str),
                                                 strlen(ip_str));
                }
                break;
            }

            default:
                ESP_LOGW(TAG, "未知命令: 0x%02X", cmd);
                break;
            }
        }

        // ==================== WiFi 状态回调 ====================

        void ProvisionManager::onWiFiStateChanged(int state, int reason)
        {
            auto  wifi_state    = static_cast<wifi::State>(state);
            auto& wifi_mgr      = wifi::WiFiManager::getInstance();
            auto& provision_svc = ble::gatt::ProvisionService::getInstance();

            auto info = wifi_mgr.getInfo();

            // 如果正在尝试连接已保存的网络
            if (trying_saved_)
            {
                if (wifi_state == wifi::State::CONNECTED)
                {
                    trying_saved_              = false;
                    ProvisionStatus new_status = ProvisionStatus::CONNECTED;
                    updateStatus(new_status);

                    current_ssid_ = info.ssid;
                    char ip_str[16];
                    snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", info.ip[0], info.ip[1],
                             info.ip[2], info.ip[3]);
                    current_ip_ = ip_str;

                    if (complete_callback_)
                    {
                        complete_callback_(true, current_ssid_.c_str());
                    }

                    provision_svc.updateStatus(ble::gatt::ProvisionStatus::CONNECTED);
                    return;
                }
                else if (wifi_state == wifi::State::FAILED)
                {
                    trying_saved_ = false;
                    startProvisioning();
                }
                // 其他状态（CONNECTING）继续等待
                ProvisionStatus new_status = wifiStateToProvisionStatus(state, reason);
                updateStatus(new_status);
                return;
            }

            ProvisionStatus new_status = wifiStateToProvisionStatus(state, reason);
            updateStatus(new_status);

            // 同步 BLE 配网服务状态
            ble::gatt::ProvisionStatus ble_status;
            switch (new_status)
            {
            case ProvisionStatus::IDLE:
                ble_status = ble::gatt::ProvisionStatus::IDLE;
                break;
            case ProvisionStatus::CONNECTING:
                ble_status = ble::gatt::ProvisionStatus::CONNECTING;
                break;
            case ProvisionStatus::CONNECTED:
                ble_status = ble::gatt::ProvisionStatus::CONNECTED;
                break;
            case ProvisionStatus::FAILED_TIMEOUT:
                ble_status = ble::gatt::ProvisionStatus::FAILED_TIMEOUT;
                break;
            case ProvisionStatus::FAILED_WRONG_PWD:
                ble_status = ble::gatt::ProvisionStatus::FAILED_WRONG_PWD;
                break;
            case ProvisionStatus::FAILED_NOT_FOUND:
                ble_status = ble::gatt::ProvisionStatus::FAILED_NOT_FOUND;
                break;
            default:
                ble_status = ble::gatt::ProvisionStatus::FAILED_UNKNOWN;
                break;
            }
            provision_svc.updateStatus(ble_status);

            // 更新当前 SSID 和 IP
            if (wifi_state == wifi::State::CONNECTED)
            {
                current_ssid_ = info.ssid;
                char ip_str[16];
                snprintf(ip_str, sizeof(ip_str), "%d.%d.%d.%d", info.ip[0], info.ip[1], info.ip[2],
                         info.ip[3]);
                current_ip_ = ip_str;

                // 如果是通过配网连接的，自动保存凭证
                if (provisioning_ && !trying_saved_)
                {
                    std::string ssid     = provision_svc.getSsid();
                    std::string password = provision_svc.getPassword();

                    if (!ssid.empty())
                    {
                        wifi::Credentials creds(ssid.c_str(), password.c_str());
                        wifi_mgr.saveCredentials(creds);
                    }
                }

                if (complete_callback_)
                {
                    complete_callback_(true, current_ssid_.c_str());
                }
            }
            else if (wifi_state == wifi::State::DISCONNECTED)
            {
                current_ssid_.clear();
                current_ip_.clear();
            }
        }

        // ==================== 内部方法 ====================

        void ProvisionManager::updateStatus(ProvisionStatus status)
        {
            if (status_ == status)
            {
                return;
            }

            status_ = status;

            if (status_callback_)
            {
                status_callback_(status);
            }
        }

        ProvisionStatus ProvisionManager::wifiStateToProvisionStatus(int wifi_state,
                                                                     int failure_reason)
        {
            auto state  = static_cast<wifi::State>(wifi_state);
            auto reason = static_cast<wifi::FailureReason>(failure_reason);

            switch (state)
            {
            case wifi::State::DISCONNECTED:
                return ProvisionStatus::IDLE;

            case wifi::State::CONNECTING:
                return ProvisionStatus::CONNECTING;

            case wifi::State::CONNECTED:
                return ProvisionStatus::CONNECTED;

            case wifi::State::FAILED:
                switch (reason)
                {
                case wifi::FailureReason::TIMEOUT:
                    return ProvisionStatus::FAILED_TIMEOUT;
                case wifi::FailureReason::WRONG_PASSWORD:
                    return ProvisionStatus::FAILED_WRONG_PWD;
                case wifi::FailureReason::NETWORK_NOT_FOUND:
                    return ProvisionStatus::FAILED_NOT_FOUND;
                default:
                    return ProvisionStatus::FAILED_UNKNOWN;
                }

            default:
                return ProvisionStatus::FAILED_UNKNOWN;
            }
        }

    } // namespace network
} // namespace app
