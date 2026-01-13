#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

// 前向声明 NimBLE 类型
class NimBLEServer;
class NimBLEService;
class NimBLECharacteristic;
class NimBLEAdvertising;

namespace app
{
    namespace network
    {
        namespace ble
        {

            // BLE 连接状态
            enum class State
            {
                UNINITIALIZED, // 未初始化
                IDLE,          // 已初始化，未广播
                ADVERTISING,   // 广播中
                CONNECTED      // 已连接
            };

            // BLE 安全模式
            enum class SecurityMode
            {
                NONE,        // 无安全（Just Works）
                PASSKEY,     // Passkey 配对
                NUMERIC_COMP // 数字比较配对
            };

            // 连接信息
            struct ConnectionInfo
            {
                uint16_t conn_handle; // 连接句柄
                uint8_t  address[6];  // 对端地址
                uint16_t mtu;         // MTU 大小
                bool     encrypted;   // 是否加密

                ConnectionInfo() : conn_handle(0), mtu(23), encrypted(false)
                {
                    memset(address, 0, sizeof(address));
                }
            };

            // 广播配置
            struct AdvertiseConfig
            {
                const char* device_name;   // 设备名称
                uint16_t    min_interval;  // 最小广播间隔（0.625ms 单位）
                uint16_t    max_interval;  // 最大广播间隔
                bool        connectable;   // 是否可连接
                bool        scan_response; // 是否启用扫描响应
                uint16_t    appearance;    // 设备外观

                AdvertiseConfig()
                    : device_name("EmotiPet"), min_interval(160) // 100ms
                      ,
                      max_interval(320) // 200ms
                      ,
                      connectable(true), scan_response(true), appearance(0)
                {
                }
            };

            // 特征属性标志
            namespace Property
            {
                constexpr uint32_t READ         = 0x0001;
                constexpr uint32_t WRITE        = 0x0002;
                constexpr uint32_t WRITE_NR     = 0x0004; // Write without response
                constexpr uint32_t NOTIFY       = 0x0008;
                constexpr uint32_t INDICATE     = 0x0010;
                constexpr uint32_t READ_ENC     = 0x0100; // 需要加密才能读
                constexpr uint32_t WRITE_ENC    = 0x0200; // 需要加密才能写
                constexpr uint32_t READ_AUTHEN  = 0x0400; // 需要认证才能读
                constexpr uint32_t WRITE_AUTHEN = 0x0800; // 需要认证才能写
            } // namespace Property

            // 回调函数类型
            using StateCallback      = std::function<void(State state)>;
            using ConnectCallback    = std::function<void(const ConnectionInfo& info)>;
            using DisconnectCallback = std::function<void(const ConnectionInfo& info, int reason)>;
            using CharReadCallback =
                std::function<void(NimBLECharacteristic* chr, const ConnectionInfo& conn)>;
            using CharWriteCallback =
                std::function<void(NimBLECharacteristic* chr, const ConnectionInfo& conn,
                                   const uint8_t* data, size_t len)>;
            using CharSubscribeCallback = std::function<void(
                NimBLECharacteristic* chr, const ConnectionInfo& conn, uint16_t sub_value)>;

            /**
             * @brief 特征回调管理器
             */
            struct CharCallbacks
            {
                CharReadCallback      on_read;
                CharWriteCallback     on_write;
                CharSubscribeCallback on_subscribe;
            };

            /**
             * @brief BLE 管理器类（单例）
             *
             * 封装 NimBLE 库，提供简化的 BLE 服务器功能
             */
            class Manager
            {
            public:
                static Manager& getInstance();

                /**
                 * @brief 初始化 BLE
                 * @param device_name 设备名称
                 * @return 成功返回 true
                 */
                bool init(const char* device_name = "EmotiPet");

                /**
                 * @brief 反初始化 BLE
                 * @param clear_all 是否清除所有绑定信息
                 */
                void deinit(bool clear_all = false);

                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 创建服务
                 * @param uuid 服务 UUID
                 * @return NimBLE 服务指针
                 */
                NimBLEService* createService(const char* uuid);

                /**
                 * @brief 创建特征
                 * @param service 服务指针
                 * @param uuid 特征 UUID
                 * @param properties 属性（使用 Property 命名空间）
                 * @param max_len 最大值长度
                 * @return NimBLE 特征指针
                 */
                NimBLECharacteristic* createCharacteristic(NimBLEService* service, const char* uuid,
                                                           uint32_t properties,
                                                           uint16_t max_len = 512);

                /**
                 * @brief 设置特征回调
                 * @param chr 特征指针
                 * @param callbacks 回调函数
                 */
                void setCharacteristicCallbacks(NimBLECharacteristic* chr,
                                                const CharCallbacks&  callbacks);

                /**
                 * @brief 启动服务
                 * @param service 服务指针
                 * @return 成功返回 true
                 */
                bool startService(NimBLEService* service);

                /**
                 * @brief 启动服务器
                 * @return 成功返回 true
                 * @note 在创建所有服务和特征后调用
                 */
                bool startServer();

                /**
                 * @brief 开始广播
                 * @param config 广播配置
                 * @param duration_ms 广播持续时间（0 表示永久）
                 * @return 成功返回 true
                 */
                bool startAdvertising(const AdvertiseConfig& config      = AdvertiseConfig(),
                                      uint32_t               duration_ms = 0);

                /**
                 * @brief 停止广播
                 * @return 成功返回 true
                 */
                bool stopAdvertising();

                /**
                 * @brief 断开连接
                 * @param conn_handle 连接句柄（0xFFFF 断开所有连接）
                 * @param reason 断开原因
                 * @return 成功返回 true
                 */
                bool disconnect(uint16_t conn_handle = 0xFFFF, uint8_t reason = 0x13);

                State getState() const
                {
                    return state_;
                }
                bool isConnected() const
                {
                    return state_ == State::CONNECTED;
                }
                bool isAdvertising() const
                {
                    return state_ == State::ADVERTISING;
                }

                uint8_t                     getConnectedCount() const;
                std::vector<ConnectionInfo> getConnectedDevices() const;

                void setStateCallback(StateCallback callback);
                void setConnectCallback(ConnectCallback callback);
                void setDisconnectCallback(DisconnectCallback callback);

                /**
                 * @brief 设置安全模式
                 * @param mode 安全模式
                 * @param passkey 静态密钥（仅 PASSKEY 模式有效）
                 */
                void setSecurityMode(SecurityMode mode, uint32_t passkey = 123456);

                /**
                 * @brief 设置 MTU
                 * @param mtu MTU 大小
                 * @return 成功返回 true
                 */
                bool setMTU(uint16_t mtu);

                /**
                 * @brief 获取当前 MTU
                 * @return MTU 大小
                 */
                uint16_t getMTU() const;

                /**
                 * @brief 设置发射功率
                 * @param dbm 功率 dBm（-12 到 9）
                 * @return 成功返回 true
                 */
                bool setTxPower(int8_t dbm);

                /**
                 * @brief 获取本机地址字符串
                 * @return 地址字符串 "XX:XX:XX:XX:XX:XX"
                 */
                std::string getAddressString() const;

                /**
                 * @brief 获取 NimBLE 服务器指针
                 * @return NimBLE 服务器指针
                 */
                NimBLEServer* getServer() const
                {
                    return server_;
                }

                // NimBLE 回调（内部使用）
                void onConnect(const ConnectionInfo& info);
                void onDisconnect(const ConnectionInfo& info, int reason);
                void onMTUChange(uint16_t mtu, uint16_t conn_handle);

            private:
                Manager();
                ~Manager();
                Manager(const Manager&)            = delete;
                Manager& operator=(const Manager&) = delete;

                void updateState(State new_state);

                mutable std::mutex          mutex_;
                bool                        initialized_;
                State                       state_;
                NimBLEServer*               server_;
                std::vector<NimBLEService*> services_;
                StateCallback               state_callback_;
                ConnectCallback             connect_callback_;
                DisconnectCallback          disconnect_callback_;
                SecurityMode                security_mode_;
                uint32_t                    passkey_;
                AdvertiseConfig             adv_config_;
                std::vector<ConnectionInfo> connections_;
            };

        } // namespace ble
    } // namespace network
} // namespace app
