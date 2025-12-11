#include "gatt.hpp"

#include "NimBLEDevice.h"
#include "esp_log.h"

#include <cstring>

static const char* const TAG = "BLE_GATT";

namespace app
{
    namespace network
    {
        namespace ble
        {
            namespace gatt
            {

                // ==================== ProvisionService ====================

                ProvisionService& ProvisionService::getInstance()
                {
                    static ProvisionService instance;
                    return instance;
                }

                ProvisionService::ProvisionService()
                    : created_(false), service_(nullptr), ssid_char_(nullptr),
                      password_char_(nullptr), status_char_(nullptr), command_char_(nullptr),
                      status_(ProvisionStatus::IDLE)
                {
                }

                bool ProvisionService::create()
                {
                    if (created_)
                    {
                        return true;
                    }

                    auto& mgr = Manager::getInstance();
                    if (!mgr.isInitialized())
                    {
                        ESP_LOGE(TAG, "BLE 未初始化");
                        return false;
                    }

                    service_ = mgr.createService(UUID::PROVISION_SERVICE);
                    if (!service_)
                    {
                        ESP_LOGE(TAG, "创建配网服务失败");
                        return false;
                    }

                    ssid_char_ = mgr.createCharacteristic(
                        service_, UUID::WIFI_SSID_CHAR,
                        NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::READ, 32);

                    password_char_ = mgr.createCharacteristic(service_, UUID::WIFI_PASSWORD_CHAR,
                                                              NIMBLE_PROPERTY::WRITE, 64);

                    status_char_ = mgr.createCharacteristic(
                        service_, UUID::WIFI_STATUS_CHAR,
                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 32);

                    command_char_ = mgr.createCharacteristic(service_, UUID::WIFI_COMMAND_CHAR,
                                                             NIMBLE_PROPERTY::WRITE, 16);

                    if (!ssid_char_ || !password_char_ || !status_char_ || !command_char_)
                    {
                        ESP_LOGE(TAG, "创建特征失败");
                        return false;
                    }

                    // 设置 SSID 写入回调
                    CharCallbacks ssid_cbs;
                    ssid_cbs.on_write = [this](NimBLECharacteristic* chr,
                                               const ConnectionInfo& conn, const uint8_t* data,
                                               size_t len) { onSsidWrite(chr, conn, data, len); };
                    mgr.setCharacteristicCallbacks(ssid_char_, ssid_cbs);

                    // 设置 Password 写入回调
                    CharCallbacks pwd_cbs;
                    pwd_cbs.on_write = [this](NimBLECharacteristic* chr, const ConnectionInfo& conn,
                                              const uint8_t* data, size_t len)
                    { onPasswordWrite(chr, conn, data, len); };
                    mgr.setCharacteristicCallbacks(password_char_, pwd_cbs);

                    // 设置 Command 写入回调
                    CharCallbacks cmd_cbs;
                    cmd_cbs.on_write = [this](NimBLECharacteristic* chr, const ConnectionInfo& conn,
                                              const uint8_t* data, size_t len)
                    { onCommandWrite(chr, conn, data, len); };
                    mgr.setCharacteristicCallbacks(command_char_, cmd_cbs);

                    // 设置初始状态
                    uint8_t initial_status = static_cast<uint8_t>(ProvisionStatus::IDLE);
                    status_char_->setValue(&initial_status, 1);

                    // 启动服务
                    mgr.startService(service_);

                    created_ = true;
                    ESP_LOGI(TAG, "配网服务创建完成");
                    return true;
                }

                void ProvisionService::setConnectCallback(ProvisionConnectCallback callback)
                {
                    connect_callback_ = std::move(callback);
                }

                void ProvisionService::setDisconnectCallback(ProvisionDisconnectCallback callback)
                {
                    disconnect_callback_ = std::move(callback);
                }

                void ProvisionService::setCommandCallback(ProvisionCommandCallback callback)
                {
                    command_callback_ = std::move(callback);
                }

                void ProvisionService::updateStatus(ProvisionStatus status)
                {
                    status_ = status;
                    if (status_char_)
                    {
                        uint8_t val = static_cast<uint8_t>(status);
                        status_char_->setValue(&val, 1);
                        status_char_->notify();
                    }
                }

                void ProvisionService::sendStatusData(const uint8_t* data, size_t len)
                {
                    if (status_char_ && data && len > 0)
                    {
                        status_char_->notify(data, len);
                    }
                }

                void ProvisionService::onSsidWrite(NimBLECharacteristic* chr,
                                                   const ConnectionInfo& conn, const uint8_t* data,
                                                   size_t len)
                {
                    if (data && len > 0 && len < 32)
                    {
                        ssid_.assign(reinterpret_cast<const char*>(data), len);
                        ESP_LOGI(TAG, "SSID 已设置: %s", ssid_.c_str());
                    }
                }

                void ProvisionService::onPasswordWrite(NimBLECharacteristic* chr,
                                                       const ConnectionInfo& conn,
                                                       const uint8_t* data, size_t len)
                {
                    if (data && len > 0 && len < 64)
                    {
                        password_.assign(reinterpret_cast<const char*>(data), len);
                        ESP_LOGI(TAG, "密码已设置 (长度=%d)", (int)len);
                    }
                }

                void ProvisionService::onCommandWrite(NimBLECharacteristic* chr,
                                                      const ConnectionInfo& conn,
                                                      const uint8_t* data, size_t len)
                {
                    if (!data || len < 1)
                    {
                        return;
                    }

                    auto cmd = static_cast<ProvisionCommand>(data[0]);
                    ESP_LOGI(TAG, "收到命令: 0x%02X", data[0]);

                    switch (cmd)
                    {
                    case ProvisionCommand::CONNECT:
                        if (!ssid_.empty() && connect_callback_)
                        {
                            connect_callback_(ssid_.c_str(), password_.c_str());
                        }
                        break;

                    case ProvisionCommand::DISCONNECT:
                        if (disconnect_callback_)
                        {
                            disconnect_callback_();
                        }
                        break;

                    default:
                        if (command_callback_)
                        {
                            command_callback_(cmd);
                        }
                        break;
                    }
                }

                // ==================== DeviceInfoService ====================

                DeviceInfoService& DeviceInfoService::getInstance()
                {
                    static DeviceInfoService instance;
                    return instance;
                }

                DeviceInfoService::DeviceInfoService() : created_(false), service_(nullptr) {}

                bool DeviceInfoService::create(const char* manufacturer, const char* model,
                                               const char* serial, const char* firmware_rev,
                                               const char* hardware_rev, const char* software_rev)
                {
                    if (created_)
                    {
                        return true;
                    }

                    auto& mgr = Manager::getInstance();
                    if (!mgr.isInitialized())
                    {
                        ESP_LOGE(TAG, "BLE 未初始化");
                        return false;
                    }

                    service_ = mgr.createService(UUID::DEVICE_INFO_SERVICE);
                    if (!service_)
                    {
                        ESP_LOGE(TAG, "创建设备信息服务失败");
                        return false;
                    }

                    if (manufacturer)
                    {
                        auto* chr = mgr.createCharacteristic(service_, UUID::MANUFACTURER_CHAR,
                                                             NIMBLE_PROPERTY::READ);
                        if (chr)
                            chr->setValue(manufacturer);
                    }

                    if (model)
                    {
                        auto* chr = mgr.createCharacteristic(service_, UUID::MODEL_NUMBER_CHAR,
                                                             NIMBLE_PROPERTY::READ);
                        if (chr)
                            chr->setValue(model);
                    }

                    if (serial)
                    {
                        auto* chr = mgr.createCharacteristic(service_, UUID::SERIAL_NUMBER_CHAR,
                                                             NIMBLE_PROPERTY::READ);
                        if (chr)
                            chr->setValue(serial);
                    }

                    if (firmware_rev)
                    {
                        auto* chr = mgr.createCharacteristic(service_, UUID::FIRMWARE_REV_CHAR,
                                                             NIMBLE_PROPERTY::READ);
                        if (chr)
                            chr->setValue(firmware_rev);
                    }

                    if (hardware_rev)
                    {
                        auto* chr = mgr.createCharacteristic(service_, UUID::HARDWARE_REV_CHAR,
                                                             NIMBLE_PROPERTY::READ);
                        if (chr)
                            chr->setValue(hardware_rev);
                    }

                    if (software_rev)
                    {
                        auto* chr = mgr.createCharacteristic(service_, UUID::SOFTWARE_REV_CHAR,
                                                             NIMBLE_PROPERTY::READ);
                        if (chr)
                            chr->setValue(software_rev);
                    }

                    mgr.startService(service_);

                    created_ = true;
                    ESP_LOGI(TAG, "设备信息服务创建完成");
                    return true;
                }

                // ==================== BatteryService ====================

                BatteryService& BatteryService::getInstance()
                {
                    static BatteryService instance;
                    return instance;
                }

                BatteryService::BatteryService()
                    : created_(false), service_(nullptr), level_char_(nullptr)
                {
                }

                bool BatteryService::create()
                {
                    if (created_)
                    {
                        return true;
                    }

                    auto& mgr = Manager::getInstance();
                    if (!mgr.isInitialized())
                    {
                        ESP_LOGE(TAG, "BLE 未初始化");
                        return false;
                    }

                    service_ = mgr.createService(UUID::BATTERY_SERVICE);
                    if (!service_)
                    {
                        ESP_LOGE(TAG, "创建电池服务失败");
                        return false;
                    }

                    level_char_ = mgr.createCharacteristic(
                        service_, UUID::BATTERY_LEVEL_CHAR,
                        NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY, 1);

                    if (!level_char_)
                    {
                        ESP_LOGE(TAG, "创建电池电量特征失败");
                        return false;
                    }

                    uint8_t initial_level = 100;
                    level_char_->setValue(&initial_level, 1);

                    mgr.startService(service_);

                    created_ = true;
                    ESP_LOGI(TAG, "电池服务创建完成");
                    return true;
                }

                void BatteryService::updateLevel(uint8_t level)
                {
                    if (level > 100)
                    {
                        level = 100;
                    }

                    if (level_char_)
                    {
                        level_char_->setValue(&level, 1);
                        level_char_->notify();
                    }
                }

            } // namespace gatt
        }     // namespace ble
    }         // namespace network
} // namespace app
