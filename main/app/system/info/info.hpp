#pragma once

#include <cstddef>
#include <cstdint>

namespace app
{
    namespace sys
    {
        namespace info
        {

            /**
             * @brief 内存信息类
             */
            class MemoryInfo
            {
            public:
                /**
                 * @brief 获取内存信息
                 * @return 内存信息实例
                 */
                static MemoryInfo getMemoryInfo();

                /**
                 * @brief 获取内部 SRAM 总量
                 * @return 内部 SRAM 总量（字节）
                 */
                size_t getSramTotal() const
                {
                    return sram_total;
                }

                /**
                 * @brief 获取内部 SRAM 空闲
                 * @return 内部 SRAM 空闲（字节）
                 */
                size_t getSramFree() const
                {
                    return sram_free;
                }

                /**
                 * @brief 获取 PSRAM 总容量
                 * @return PSRAM 总容量（字节）
                 */
                size_t getPsramTotal() const
                {
                    return psram_total;
                }

                /**
                 * @brief 获取 PSRAM 空闲
                 * @return PSRAM 空闲（字节）
                 */
                size_t getPsramFree() const
                {
                    return psram_free;
                }

            private:
                /**
                 * @brief 构造函数，自动获取当前内存信息
                 */
                MemoryInfo();

                size_t sram_total;  // 内部 SRAM 总量（字节）
                size_t sram_free;   // 内部 SRAM 空闲（字节）
                size_t psram_total; // PSRAM 总容量（字节）
                size_t psram_free;  // PSRAM 空闲（字节）
            };

            /**
             * @brief CPU 信息类
             */
            class CpuInfo
            {
            public:
                /**
                 * @brief 获取 CPU 信息
                 * @return CPU 信息实例
                 */
                static CpuInfo getCpuInfo();

                /**
                 * @brief 获取 CPU 频率
                 * @return CPU 频率（Hz）
                 */
                uint32_t getCpuFrequency() const
                {
                    return cpu_frequency;
                }

            private:
                /**
                 * @brief 构造函数，自动获取当前 CPU 信息
                 */
                CpuInfo();

                uint32_t cpu_frequency; // CPU 频率（Hz）
            };

        } // namespace info
    } // namespace sys
} // namespace app
