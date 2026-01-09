#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace app
{
    namespace network
    {

        // ==================== 配网状态枚举 ====================

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

        // ==================== 回调类型定义 ====================

        using ProvisionStatusCallback   = std::function<void(ProvisionStatus status)>;
        using ProvisionCompleteCallback = std::function<void(bool success, const char* ssid)>;

        // ==================== 配网管理器类 ====================

        /**
         * @brief 网络配网管理器
         *
         * 整合 BLE 配网服务和 WiFi 模块，提供统一的配网接口。
         * 支持通过 BLE 接收 WiFi 凭证并自动连接，状态同步更新。
         */
        class ProvisionManager
        {
        public:
            static ProvisionManager& getInstance();

            /**
             * @brief 初始化配网管理器
             * @param ble_device_name BLE 设备名称
             * @return 成功返回 true
             */
            bool init(const char* ble_device_name = "EmotiPet");

            /**
             * @brief 反初始化配网管理器
             */
            void deinit();

            /**
             * @brief 开始配网（启动 BLE 广播）
             * @return 成功返回 true
             */
            bool start();

            /**
             * @brief 停止配网（停止 BLE 广播）
             * @return 成功返回 true
             */
            bool stop();

            /**
             * @brief 是否正在配网
             * @return 正在配网返回 true
             */
            bool isProvisioning() const;

            /**
             * @brief 获取当前配网状态
             * @return 配网状态
             */
            ProvisionStatus getStatus() const;

            /**
             * @brief 设置状态回调
             * @param callback 状态变化回调函数
             */
            void setStatusCallback(ProvisionStatusCallback callback);

            /**
             * @brief 设置配网完成回调
             * @param callback 配网完成回调函数
             */
            void setCompleteCallback(ProvisionCompleteCallback callback);

            /**
             * @brief 获取当前连接的 WiFi SSID
             * @return SSID 字符串
             */
            std::string getCurrentSsid() const;

            /**
             * @brief 获取当前 IP 地址（字符串格式）
             * @return IP 地址字符串
             */
            std::string getCurrentIp() const;

            /**
             * @brief 手动触发 WiFi 扫描
             * @return 成功返回 true
             * @note 扫描结果需要通过其他方式返回给客户端
             */
            bool scanWiFi() const;

        private:
            ProvisionManager();
            ~ProvisionManager();
            ProvisionManager(const ProvisionManager&)            = delete;
            ProvisionManager& operator=(const ProvisionManager&) = delete;

            // BLE 配网服务回调
            void onProvisionConnect(const char* ssid, const char* password);
            void onProvisionDisconnect();
            void onProvisionCommand(uint8_t cmd);

            // WiFi 状态回调
            void onWiFiStateChanged(int state, int reason);

            // 更新配网状态
            void updateStatus(ProvisionStatus status);

            // 将 WiFi 状态转换为配网状态
            ProvisionStatus wifiStateToProvisionStatus(int wifi_state, int failure_reason);

            // 启动 BLE 配网（内部方法）
            bool startProvisioning();

            bool                      initialized_;       // 是否已初始化
            bool                      provisioning_;      // 是否正在配网
            bool                      trying_saved_;      // 是否正在尝试连接已保存的网络
            int                       retry_count_;       // 重试计数器（最多3次）
            ProvisionStatus           status_;            // 当前配网状态
            ProvisionStatusCallback   status_callback_;   // 状态回调
            ProvisionCompleteCallback complete_callback_; // 完成回调
            std::string               current_ssid_;      // 当前 SSID
            std::string               current_ip_;        // 当前 IP 地址
        };

    } // namespace network
} // namespace app
