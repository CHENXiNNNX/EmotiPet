#include "bluetooth.hpp"

#include "NimBLEDevice.h"
#include "esp_log.h"

#include <algorithm>
#include <map>

static const char* const TAG = "BLE";

namespace app
{
    namespace network
    {
        namespace ble
        {

            // 特征回调存储
            static std::map<NimBLECharacteristic*, CharCallbacks> s_char_callbacks;

            // ==================== NimBLE 回调类 ====================

            class ServerCallbacks : public NimBLEServerCallbacks
            {
            public:
                void onConnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo) override
                {
                    ConnectionInfo info;
                    info.conn_handle = connInfo.getConnHandle();
                    info.mtu         = 23;
                    info.encrypted   = connInfo.isEncrypted();

                    NimBLEAddress  addr = connInfo.getAddress();
                    const uint8_t* val  = addr.getVal();
                    if (val)
                    {
                        memcpy(info.address, val, 6);
                    }

                    Manager::getInstance().onConnect(info);
                }

                void onDisconnect(NimBLEServer* pServer, NimBLEConnInfo& connInfo,
                                  int reason) override
                {
                    ConnectionInfo info;
                    info.conn_handle = connInfo.getConnHandle();

                    NimBLEAddress  addr = connInfo.getAddress();
                    const uint8_t* val  = addr.getVal();
                    if (val)
                    {
                        memcpy(info.address, val, 6);
                    }

                    Manager::getInstance().onDisconnect(info, reason);
                }

                void onMTUChange(uint16_t MTU, NimBLEConnInfo& connInfo) override
                {
                    Manager::getInstance().onMTUChange(MTU, connInfo.getConnHandle());
                }

                uint32_t onPassKeyDisplay() override
                {
                    return 123456;
                }

                void onConfirmPassKey(NimBLEConnInfo& connInfo, uint32_t pin) override
                {
                    NimBLEDevice::injectConfirmPasskey(connInfo, true);
                }

                void onAuthenticationComplete(NimBLEConnInfo& connInfo) override
                {
                    if (!connInfo.isEncrypted())
                    {
                        ESP_LOGW(TAG, "认证失败");
                    }
                }
            };

            class CharacteristicCallbacksImpl : public NimBLECharacteristicCallbacks
            {
            public:
                void onRead(NimBLECharacteristic* pCharacteristic,
                            NimBLEConnInfo&       connInfo) override
                {
                    auto it = s_char_callbacks.find(pCharacteristic);
                    if (it != s_char_callbacks.end() && it->second.on_read)
                    {
                        ConnectionInfo info;
                        info.conn_handle = connInfo.getConnHandle();
                        info.encrypted   = connInfo.isEncrypted();

                        NimBLEAddress  addr = connInfo.getAddress();
                        const uint8_t* val  = addr.getVal();
                        if (val)
                        {
                            memcpy(info.address, val, 6);
                        }

                        it->second.on_read(pCharacteristic, info);
                    }
                }

                void onWrite(NimBLECharacteristic* pCharacteristic,
                             NimBLEConnInfo&       connInfo) override
                {
                    auto it = s_char_callbacks.find(pCharacteristic);
                    if (it != s_char_callbacks.end() && it->second.on_write)
                    {
                        ConnectionInfo info;
                        info.conn_handle = connInfo.getConnHandle();
                        info.encrypted   = connInfo.isEncrypted();

                        NimBLEAddress  addr    = connInfo.getAddress();
                        const uint8_t* addrVal = addr.getVal();
                        if (addrVal)
                        {
                            memcpy(info.address, addrVal, 6);
                        }

                        NimBLEAttValue chrVal = pCharacteristic->getValue();
                        it->second.on_write(pCharacteristic, info, chrVal.data(), chrVal.size());
                    }
                }

                void onSubscribe(NimBLECharacteristic* pCharacteristic, NimBLEConnInfo& connInfo,
                                 uint16_t subValue) override
                {
                    auto it = s_char_callbacks.find(pCharacteristic);
                    if (it != s_char_callbacks.end() && it->second.on_subscribe)
                    {
                        ConnectionInfo info;
                        info.conn_handle = connInfo.getConnHandle();
                        info.encrypted   = connInfo.isEncrypted();

                        NimBLEAddress  addr = connInfo.getAddress();
                        const uint8_t* val  = addr.getVal();
                        if (val)
                        {
                            memcpy(info.address, val, 6);
                        }

                        it->second.on_subscribe(pCharacteristic, info, subValue);
                    }
                }
            };

            static ServerCallbacks             s_server_callbacks;
            static CharacteristicCallbacksImpl s_char_callbacks_impl;

            // ==================== Manager ====================

            Manager& Manager::getInstance()
            {
                static Manager instance;
                return instance;
            }

            Manager::Manager()
                : initialized_(false), state_(State::UNINITIALIZED), server_(nullptr),
                  security_mode_(SecurityMode::NONE), passkey_(123456)
            {
            }

            Manager::~Manager()
            {
                deinit(true);
            }

            bool Manager::init(const char* device_name)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (initialized_)
                {
                    ESP_LOGW(TAG, "已初始化");
                    return true;
                }

                if (!NimBLEDevice::init(device_name ? device_name : "EmotiPet"))
                {
                    ESP_LOGE(TAG, "初始化失败");
                    return false;
                }

                adv_config_.device_name = device_name;
                initialized_            = true;
                state_                  = State::IDLE;

                ESP_LOGI(TAG, "初始化完成: %s", device_name);
                return true;
            }

            void Manager::deinit(bool clear_all)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    return;
                }

                stopAdvertising();
                s_char_callbacks.clear();

                NimBLEDevice::deinit(clear_all);

                server_ = nullptr;
                services_.clear();
                initialized_ = false;
                state_       = State::UNINITIALIZED;
                connections_.clear();

                ESP_LOGI(TAG, "反初始化完成");
            }

            NimBLEService* Manager::createService(const char* uuid)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_)
                {
                    ESP_LOGE(TAG, "未初始化");
                    return nullptr;
                }

                if (!server_)
                {
                    server_ = NimBLEDevice::createServer();
                    if (!server_)
                    {
                        ESP_LOGE(TAG, "创建服务器失败");
                        return nullptr;
                    }
                    server_->setCallbacks(&s_server_callbacks);
                }

                NimBLEService* svc = server_->createService(uuid);
                if (svc)
                {
                    services_.push_back(svc);
                }
                return svc;
            }

            NimBLECharacteristic* Manager::createCharacteristic(NimBLEService* service,
                                                                const char*    uuid,
                                                                uint32_t       properties,
                                                                uint16_t       max_len)
            {
                if (!service)
                {
                    return nullptr;
                }

                return service->createCharacteristic(uuid, properties, max_len);
            }

            void Manager::setCharacteristicCallbacks(NimBLECharacteristic* chr,
                                                     const CharCallbacks&  callbacks)
            {
                if (!chr)
                {
                    return;
                }

                s_char_callbacks[chr] = callbacks;
                chr->setCallbacks(&s_char_callbacks_impl);
            }

            bool Manager::startService(NimBLEService* service)
            {
                if (!service)
                {
                    return false;
                }
                return service->start();
            }

            bool Manager::startServer()
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_ || !server_)
                {
                    ESP_LOGE(TAG, "未初始化或服务器不存在");
                    return false;
                }

                server_->start();
                ESP_LOGI(TAG, "服务器已启动，包含 %d 个服务", (int)services_.size());
                return true;
            }

            bool Manager::startAdvertising(const AdvertiseConfig& config, uint32_t duration_ms)
            {
                StateCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (!initialized_)
                    {
                        ESP_LOGE(TAG, "未初始化");
                        return false;
                    }

                    if (state_ == State::ADVERTISING)
                    {
                        ESP_LOGW(TAG, "已在广播中");
                        return true;
                    }

                    adv_config_ = config;

                    NimBLEAdvertising* pAdv = NimBLEDevice::getAdvertising();
                    if (!pAdv)
                    {
                        ESP_LOGE(TAG, "获取广播对象失败");
                        return false;
                    }

                    pAdv->setName(config.device_name ? config.device_name : "EmotiPet");
                    pAdv->setMinInterval(config.min_interval);
                    pAdv->setMaxInterval(config.max_interval);
                    pAdv->enableScanResponse(config.scan_response);

                    if (config.appearance != 0)
                    {
                        pAdv->setAppearance(config.appearance);
                    }

                    for (auto* svc : services_)
                    {
                        pAdv->addServiceUUID(svc->getUUID());
                    }

                    if (!pAdv->start(duration_ms))
                    {
                        ESP_LOGE(TAG, "启动广播失败");
                        return false;
                    }

                    state_ = State::ADVERTISING;
                    cb     = state_callback_;
                }

                if (cb)
                {
                    cb(State::ADVERTISING);
                }

                ESP_LOGI(TAG, "广播已启动: %s", config.device_name);
                return true;
            }

            bool Manager::stopAdvertising()
            {
                StateCallback cb;
                State         new_state;
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (!initialized_ || state_ != State::ADVERTISING)
                    {
                        return true;
                    }

                    if (!NimBLEDevice::stopAdvertising())
                    {
                        ESP_LOGE(TAG, "停止广播失败");
                        return false;
                    }

                    new_state = connections_.empty() ? State::IDLE : State::CONNECTED;
                    state_    = new_state;
                    cb        = state_callback_;
                }

                if (cb)
                {
                    cb(new_state);
                }

                ESP_LOGI(TAG, "广播已停止");
                return true;
            }

            bool Manager::disconnect(uint16_t conn_handle, uint8_t reason)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!initialized_ || !server_)
                {
                    return false;
                }

                if (conn_handle == 0xFFFF)
                {
                    auto peers = server_->getPeerDevices();
                    for (auto handle : peers)
                    {
                        server_->disconnect(handle, reason);
                    }
                    return true;
                }

                return server_->disconnect(conn_handle, reason);
            }

            uint8_t Manager::getConnectedCount() const
            {
                std::lock_guard<std::mutex> lock(mutex_);

                if (!server_)
                {
                    return 0;
                }
                return server_->getConnectedCount();
            }

            std::vector<ConnectionInfo> Manager::getConnectedDevices() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return connections_;
            }

            void Manager::setStateCallback(StateCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                state_callback_ = std::move(callback);
            }

            void Manager::setConnectCallback(ConnectCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                connect_callback_ = std::move(callback);
            }

            void Manager::setDisconnectCallback(DisconnectCallback callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                disconnect_callback_ = std::move(callback);
            }

            void Manager::setSecurityMode(SecurityMode mode, uint32_t passkey)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                security_mode_ = mode;
                passkey_       = passkey;

                if (!initialized_)
                {
                    return;
                }

                switch (mode)
                {
                case SecurityMode::NONE:
                    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
                    break;
                case SecurityMode::PASSKEY:
                    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_ONLY);
                    NimBLEDevice::setSecurityPasskey(passkey);
                    break;
                case SecurityMode::NUMERIC_COMP:
                    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_DISPLAY_YESNO);
                    break;
                }
            }

            bool Manager::setMTU(uint16_t mtu)
            {
                return NimBLEDevice::setMTU(mtu);
            }

            uint16_t Manager::getMTU() const
            {
                return NimBLEDevice::getMTU();
            }

            bool Manager::setTxPower(int8_t dbm)
            {
                return NimBLEDevice::setPower(dbm);
            }

            std::string Manager::getAddressString() const
            {
                return NimBLEDevice::getAddress().toString();
            }

            void Manager::onConnect(const ConnectionInfo& info)
            {
                ConnectCallback cb;
                StateCallback   state_cb;
                bool            state_changed = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    connections_.push_back(info);
                    cb = connect_callback_;

                    if (state_ != State::CONNECTED)
                    {
                        state_        = State::CONNECTED;
                        state_changed = true;
                        state_cb      = state_callback_;
                    }
                }

                if (state_changed && state_cb)
                {
                    state_cb(State::CONNECTED);
                }

                if (cb)
                {
                    cb(info);
                }

                ESP_LOGI(TAG, "设备已连接: 句柄=%d", info.conn_handle);
            }

            void Manager::onDisconnect(const ConnectionInfo& info, int reason)
            {
                DisconnectCallback cb;
                StateCallback      state_cb;
                bool               state_changed = false;
                State              new_state     = State::IDLE;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    connections_.erase(
                        std::remove_if(connections_.begin(), connections_.end(),
                                       [&](const ConnectionInfo& c)
                                       { return c.conn_handle == info.conn_handle; }),
                        connections_.end());

                    cb = disconnect_callback_;

                    if (connections_.empty() && state_ != State::IDLE)
                    {
                        state_        = State::IDLE;
                        state_changed = true;
                        state_cb      = state_callback_;
                    }
                }

                if (state_changed && state_cb)
                {
                    state_cb(new_state);
                }

                if (cb)
                {
                    cb(info, reason);
                }

                ESP_LOGI(TAG, "设备已断开: 句柄=%d, 原因=%d", info.conn_handle, reason);
            }

            void Manager::onMTUChange(uint16_t mtu, uint16_t conn_handle)
            {
                std::lock_guard<std::mutex> lock(mutex_);

                for (auto& conn : connections_)
                {
                    if (conn.conn_handle == conn_handle)
                    {
                        conn.mtu = mtu;
                        break;
                    }
                }

                ESP_LOGI(TAG, "MTU 已更新: 句柄=%d, MTU=%d", conn_handle, mtu);
            }

            void Manager::updateState(State new_state)
            {
                StateCallback cb;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (state_ == new_state)
                    {
                        return;
                    }
                    state_ = new_state;
                    cb     = state_callback_;
                }

                if (cb)
                {
                    cb(new_state);
                }
            }

        } // namespace ble
    } // namespace network
} // namespace app
