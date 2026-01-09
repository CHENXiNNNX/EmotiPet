#pragma once

#include <cstdint>
#include <string>

namespace app
{
    namespace tool
    {
        namespace time
        {

            /**
             * @brief 获取 Unix 时间戳（秒）
             * @return Unix 时间戳，从 1970-01-01 00:00:00 UTC 开始的秒数
             * @note 需要先通过 NTP 同步系统时间，否则返回的是未同步的时间
             */
            int64_t unixTimestampSec();

            /**
             * @brief 获取 Unix 时间戳（毫秒）
             * @return Unix 时间戳，从 1970-01-01 00:00:00 UTC 开始的毫秒数
             * @note 需要先通过 NTP 同步系统时间，否则返回的是未同步的时间
             */
            int64_t unixTimestampMs();

            /**
             * @brief 获取系统启动时间（毫秒）
             * @return 从系统启动开始的毫秒数
             * @note 系统重启后会重置为 0，适合用于相对时间测量
             */
            int64_t uptimeMs();

            /**
             * @brief 获取系统启动时间（微秒）
             * @return 从系统启动开始的微秒数
             * @note 系统重启后会重置为 0，适合用于高精度相对时间测量
             */
            int64_t uptimeUs();

            /**
             * @brief 获取系统启动时间（秒）
             * @return 从系统启动开始的秒数
             * @note 系统重启后会重置为 0，适合用于相对时间测量
             */
            int64_t uptimeSec();

            /**
             * @brief 获取 ISO 8601 格式的时间戳字符串（UTC）
             * @return ISO 8601 格式的时间戳，如 "2025-03-12T19:00:00Z"
             * @note 需要先通过 NTP 同步系统时间，否则返回的是未同步的时间
             */
            std::string iso8601Timestamp();

        } // namespace time
    } // namespace tool
} // namespace app
