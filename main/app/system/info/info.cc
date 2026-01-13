#include "info.hpp"

#include "esp_private/esp_clk.h"
#include "esp_heap_caps.h"
#include "esp_psram.h"

namespace app
{
    namespace sys
    {
        namespace info
        {

            MemoryInfo::MemoryInfo()
                : sram_total(heap_caps_get_total_size(MALLOC_CAP_INTERNAL)),
                  sram_free(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  psram_total(esp_psram_is_initialized() ? esp_psram_get_size() : 0),
                  psram_free(esp_psram_is_initialized() ? heap_caps_get_free_size(MALLOC_CAP_SPIRAM)
                                                        : 0)
            {
            }

            MemoryInfo MemoryInfo::getMemoryInfo()
            {
                return MemoryInfo();
            }

            CpuInfo::CpuInfo() : cpu_frequency(static_cast<uint32_t>(esp_clk_cpu_freq())) {}

            CpuInfo CpuInfo::getCpuInfo()
            {
                return CpuInfo();
            }

        } // namespace info
    } // namespace sys
} // namespace app
