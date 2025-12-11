#include "time.hpp"

#include <sys/time.h>

#include "esp_timer.h"
#include "task.hpp"

namespace app
{
    namespace tool
    {
        namespace time
        {

            int64_t unixTimestampSec()
            {
                struct timeval tv;
                gettimeofday(&tv, nullptr);
                return static_cast<int64_t>(tv.tv_sec);
            }

            int64_t unixTimestampMs()
            {
                struct timeval tv;
                gettimeofday(&tv, nullptr);
                return (static_cast<int64_t>(tv.tv_sec) * 1000LL) +
                       (static_cast<int64_t>(tv.tv_usec) / 1000LL);
            }

            int64_t uptimeMs()
            {
                uint32_t ticks = app::sys::task::TaskManager::getTickCount();
                return static_cast<int64_t>(ticks) * 1000LL / configTICK_RATE_HZ;
            }

            int64_t uptimeUs()
            {
                return esp_timer_get_time();
            }

            int64_t uptimeSec()
            {
                return esp_timer_get_time() / 1000000LL;
            }

        } // namespace time
    }     // namespace tool
} // namespace app
