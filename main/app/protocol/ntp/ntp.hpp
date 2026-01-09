#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <vector>

namespace app
{
    namespace protocol
    {
        namespace ntp
        {

            enum class SyncStatus
            {
                RESET,
                IN_PROGRESS,
                COMPLETED,
                FAILED
            };

            enum class SyncMode
            {
                IMMEDIATE,
                SMOOTH
            };

            using SyncCallback = std::function<void(SyncStatus status)>;

            class NTPManager
            {
            public:
                static NTPManager& getInstance();

                /**
                 * @brief 初始化 NTP 管理器
                 * @return 是否成功
                 */
                bool init();

                /**
                 * @brief 去初始化 NTP 管理器
                 */
                void deinit();

                /**
                 * @brief 配置 NTP 服务器
                 * @param servers NTP 服务器列表（最多 3 个）
                 * @param sync_mode 同步模式
                 * @return 是否成功
                 */
                bool configure(const std::vector<std::string>& servers,
                               SyncMode                        sync_mode = SyncMode::IMMEDIATE);

                /**
                 * @brief 启动 SNTP 服务
                 * @return 是否成功
                 */
                bool start();

                /**
                 * @brief 停止 SNTP 服务
                 */
                void stop();

                /**
                 * @brief 等待时间同步
                 * @param timeout_ms 超时时间（毫秒）
                 * @return 是否同步成功
                 */
                bool waitSync(uint32_t timeout_ms);

                /**
                 * @brief 设置同步回调
                 * @param callback 回调函数
                 */
                void setSyncCallback(SyncCallback callback);

                /**
                 * @brief 设置时区
                 * @param tz 时区字符串（如 "CST-8"）
                 * @return 是否成功
                 */
                bool setTimezone(const char* tz);

                /**
                 * @brief 获取同步状态
                 * @return 同步状态
                 */
                SyncStatus getSyncStatus() const;

                /**
                 * @brief 检查是否已初始化
                 * @return 是否已初始化
                 */
                bool isInitialized() const;

                /**
                 * @brief 检查 SNTP 是否已启动
                 * @return 是否已启动
                 */
                bool isStarted() const;

            private:
                NTPManager()                  = default;
                ~NTPManager();
                NTPManager(const NTPManager&)            = delete;
                NTPManager& operator=(const NTPManager&) = delete;

                static void sntpSyncCallback(struct timeval* tv);

                mutable std::mutex       mutex_;
                bool                     initialized_;
                bool                     started_;
                SyncStatus               sync_status_;
                SyncCallback             sync_callback_;
                std::vector<std::string> servers_;
                SyncMode                 sync_mode_;
            };

        } // namespace ntp
    }     // namespace protocol
} // namespace app
