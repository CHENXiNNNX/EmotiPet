#pragma once

#include "bluetooth.hpp"
#include <functional>
#include <string>

// 前向声明 NimBLE 类型
class NimBLEService;
class NimBLECharacteristic;

namespace app
{
    namespace network
    {
        namespace ble
        {
            namespace gatt
            {

                // ==================== 标准 UUID ====================

                namespace UUID
                {
                    // 标准服务 UUID
                    constexpr const char* GAP_SERVICE         = "1800";
                    constexpr const char* GATT_SERVICE        = "1801";
                    constexpr const char* DEVICE_INFO_SERVICE = "180A";
                    constexpr const char* BATTERY_SERVICE     = "180F";

                    // 标准特征 UUID
                    constexpr const char* DEVICE_NAME_CHAR   = "2A00";
                    constexpr const char* APPEARANCE_CHAR    = "2A01";
                    constexpr const char* BATTERY_LEVEL_CHAR = "2A19";
                    constexpr const char* MANUFACTURER_CHAR  = "2A29";
                    constexpr const char* MODEL_NUMBER_CHAR  = "2A24";
                    constexpr const char* SERIAL_NUMBER_CHAR = "2A25";
                    constexpr const char* FIRMWARE_REV_CHAR  = "2A26";
                    constexpr const char* HARDWARE_REV_CHAR  = "2A27";
                    constexpr const char* SOFTWARE_REV_CHAR  = "2A28";

                    // 自定义配网服务 UUID
                    constexpr const char* PROVISION_SERVICE =
                        "12345678-1234-5678-1234-56789abcdef0";
                    constexpr const char* WIFI_SSID_CHAR = "12345678-1234-5678-1234-56789abcdef1";
                    constexpr const char* WIFI_PASSWORD_CHAR =
                        "12345678-1234-5678-1234-56789abcdef2";
                    constexpr const char* WIFI_STATUS_CHAR = "12345678-1234-5678-1234-56789abcdef3";
                    constexpr const char* WIFI_COMMAND_CHAR =
                        "12345678-1234-5678-1234-56789abcdef4";

                    // 自定义设备控制服务 UUID
                    constexpr const char* DEVICE_CONTROL_SERVICE =
                        "87654321-4321-8765-4321-fedcba987650";
                    constexpr const char* DEVICE_STATE_CHAR =
                        "87654321-4321-8765-4321-fedcba987651";
                    constexpr const char* DEVICE_COMMAND_CHAR =
                        "87654321-4321-8765-4321-fedcba987652";
                    constexpr const char* DEVICE_DATA_CHAR = "87654321-4321-8765-4321-fedcba987653";

                } // namespace UUID

                // ==================== WiFi 配网状态 ====================

                enum class ProvisionStatus : uint8_t
                {
                    IDLE             = 0x00, // 空闲
                    CONNECTING       = 0x01, // 正在连接
                    CONNECTED        = 0x02, // 已连接
                    FAILED_TIMEOUT   = 0x10, // 连接超时
                    FAILED_WRONG_PWD = 0x11, // 密码错误
                    FAILED_NOT_FOUND = 0x12, // 网络未找到
                    FAILED_UNKNOWN   = 0x1F  // 未知错误
                };

                // 配网命令
                enum class ProvisionCommand : uint8_t
                {
                    CONNECT    = 0x01, // 连接 WiFi
                    DISCONNECT = 0x02, // 断开 WiFi
                    SCAN       = 0x03, // 扫描 WiFi
                    SAVE       = 0x04, // 保存凭证
                    CLEAR      = 0x05, // 清除凭证
                    GET_STATUS = 0x10, // 获取状态
                    GET_IP     = 0x11  // 获取 IP
                };

                // ==================== 回调类型 ====================

                using ProvisionConnectCallback =
                    std::function<void(const char* ssid, const char* password)>;
                using ProvisionDisconnectCallback = std::function<void()>;
                using ProvisionCommandCallback    = std::function<void(ProvisionCommand cmd)>;

                // ==================== 配网服务类 ====================

                /**
                 * @brief WiFi 配网 GATT 服务
                 *
                 * 特征说明：
                 * - SSID 特征：可写可读，用于设置 WiFi SSID
                 * - Password 特征：可写，用于设置 WiFi 密码
                 * - Status 特征：可读可通知，用于获取连接状态
                 * - Command 特征：可写，用于发送命令
                 */
                class ProvisionService
                {
                public:
                    static ProvisionService& getInstance();

                    /**
                     * @brief 创建并注册服务
                     * @return 成功返回 true
                     * @note 必须在 Manager::startServer() 之前调用
                     */
                    bool create();

                    bool isCreated() const
                    {
                        return created_;
                    }

                    void setConnectCallback(ProvisionConnectCallback callback);
                    void setDisconnectCallback(ProvisionDisconnectCallback callback);
                    void setCommandCallback(ProvisionCommandCallback callback);

                    /**
                     * @brief 更新状态
                     * @param status 新状态
                     */
                    void updateStatus(ProvisionStatus status);

                    /**
                     * @brief 发送状态数据
                     * @param data 数据指针
                     * @param len 数据长度
                     */
                    void sendStatusData(const uint8_t* data, size_t len);

                    const std::string& getSsid() const
                    {
                        return ssid_;
                    }
                    const std::string& getPassword() const
                    {
                        return password_;
                    }

                private:
                    ProvisionService();
                    ~ProvisionService()                                  = default;
                    ProvisionService(const ProvisionService&)            = delete;
                    ProvisionService& operator=(const ProvisionService&) = delete;

                    void onSsidWrite(NimBLECharacteristic* chr, const ConnectionInfo& conn,
                                     const uint8_t* data, size_t len);
                    void onPasswordWrite(NimBLECharacteristic* chr, const ConnectionInfo& conn,
                                         const uint8_t* data, size_t len);
                    void onCommandWrite(NimBLECharacteristic* chr, const ConnectionInfo& conn,
                                        const uint8_t* data, size_t len);

                    bool                        created_;
                    NimBLEService*              service_;
                    NimBLECharacteristic*       ssid_char_;
                    NimBLECharacteristic*       password_char_;
                    NimBLECharacteristic*       status_char_;
                    NimBLECharacteristic*       command_char_;
                    std::string                 ssid_;
                    std::string                 password_;
                    ProvisionStatus             status_;
                    ProvisionConnectCallback    connect_callback_;
                    ProvisionDisconnectCallback disconnect_callback_;
                    ProvisionCommandCallback    command_callback_;
                };

                // ==================== 设备信息服务类 ====================

                /**
                 * @brief 设备信息 GATT 服务（标准服务 0x180A）
                 */
                class DeviceInfoService
                {
                public:
                    static DeviceInfoService& getInstance();

                    /**
                     * @brief 创建并注册服务
                     * @param manufacturer 制造商名称
                     * @param model 型号
                     * @param serial 序列号
                     * @param firmware_rev 固件版本
                     * @param hardware_rev 硬件版本
                     * @param software_rev 软件版本
                     * @return 成功返回 true
                     */
                    bool create(const char* manufacturer = "EmotiPet", const char* model = "EP-001",
                                const char* serial = "000001", const char* firmware_rev = "1.0.0",
                                const char* hardware_rev = "1.0",
                                const char* software_rev = "1.0.0");

                    bool isCreated() const
                    {
                        return created_;
                    }

                private:
                    DeviceInfoService();
                    ~DeviceInfoService()                                   = default;
                    DeviceInfoService(const DeviceInfoService&)            = delete;
                    DeviceInfoService& operator=(const DeviceInfoService&) = delete;

                    bool           created_;
                    NimBLEService* service_;
                };

                // ==================== 电池服务类 ====================

                /**
                 * @brief 电池 GATT 服务（标准服务 0x180F）
                 */
                class BatteryService
                {
                public:
                    static BatteryService& getInstance();

                    bool create();
                    bool isCreated() const
                    {
                        return created_;
                    }

                    /**
                     * @brief 更新电池电量
                     * @param level 电量百分比（0-100）
                     */
                    void updateLevel(uint8_t level);

                private:
                    BatteryService();
                    ~BatteryService()                                = default;
                    BatteryService(const BatteryService&)            = delete;
                    BatteryService& operator=(const BatteryService&) = delete;

                    bool                  created_;
                    NimBLEService*        service_;
                    NimBLECharacteristic* level_char_;
                };

            } // namespace gatt
        }     // namespace ble
    }         // namespace network
} // namespace app
