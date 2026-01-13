#pragma once

#include <cstddef>
#include <cstdint>
#include <map>
#include <unordered_map>
#include <vector>
#include <memory>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace app
{
    namespace tool
    {
        namespace memory
        {

            /**
             * @brief ESP 堆内存删除器
             *
             * 用于 std::unique_ptr 的自定义删除器，使用 heap_caps_free 释放内存
             */
            struct EspHeapDeleter
            {
                void operator()(uint8_t* ptr) const
                {
                    if (ptr != nullptr)
                    {
                        heap_caps_free(ptr);
                    }
                }
            };

            /**
             * @brief FreeRTOS 互斥锁 RAII 包装器
             *
             * 自动管理互斥锁的创建和删除，确保资源正确释放
             */
            class MutexRAII
            {
            public:
                /**
                 * @brief 创建互斥锁
                 * @return 成功返回非空指针，失败返回 nullptr
                 */
                static MutexRAII create()
                {
                    SemaphoreHandle_t handle = xSemaphoreCreateMutex();
                    return MutexRAII(handle);
                }

                /**
                 * @brief 构造函数（移动语义）
                 */
                MutexRAII(MutexRAII&& other) noexcept : handle_(other.handle_)
                {
                    other.handle_ = nullptr;
                }

                /**
                 * @brief 移动赋值运算符
                 */
                MutexRAII& operator=(MutexRAII&& other) noexcept
                {
                    if (this != &other)
                    {
                        reset();
                        handle_       = other.handle_;
                        other.handle_ = nullptr;
                    }
                    return *this;
                }

                /**
                 * @brief 禁止拷贝构造
                 */
                MutexRAII(const MutexRAII&) = delete;

                /**
                 * @brief 禁止拷贝赋值
                 */
                MutexRAII& operator=(const MutexRAII&) = delete;

                /**
                 * @brief 析构函数，自动删除互斥锁
                 */
                ~MutexRAII()
                {
                    reset();
                }

                /**
                 * @brief 获取互斥锁句柄
                 * @return 互斥锁句柄，如果未创建则返回 nullptr
                 */
                SemaphoreHandle_t get() const
                {
                    return handle_;
                }

                /**
                 * @brief 检查互斥锁是否有效
                 * @return true 如果互斥锁有效，false 否则
                 */
                bool isValid() const
                {
                    return handle_ != nullptr;
                }

                /**
                 * @brief 获取互斥锁（用于隐式转换）
                 */
                operator SemaphoreHandle_t() const
                {
                    return handle_;
                }

                /**
                 * @brief 重置互斥锁（删除当前互斥锁）
                 */
                void reset()
                {
                    if (handle_ != nullptr)
                    {
                        vSemaphoreDelete(handle_);
                        handle_ = nullptr;
                    }
                }

            private:
                /**
                 * @brief 私有构造函数
                 * @param handle 互斥锁句柄
                 */
                explicit MutexRAII(SemaphoreHandle_t handle) : handle_(handle) {}

                SemaphoreHandle_t handle_;
            };

            /**
             * @brief 互斥锁锁定守卫类（RAII）
             *
             * 自动管理互斥锁的获取和释放，确保异常安全
             */
            class MutexLockGuard
            {
            public:
                /**
                 * @brief 构造函数，获取互斥锁
                 * @param mutex 互斥锁句柄
                 * @param timeout_ms 超时时间（毫秒），portMAX_DELAY 表示无限等待
                 */
                explicit MutexLockGuard(SemaphoreHandle_t mutex, TickType_t timeout = portMAX_DELAY)
                    : mutex_(mutex), locked_(false)
                {
                    if (mutex_ != nullptr)
                    {
                        locked_ = (xSemaphoreTake(mutex_, timeout) == pdTRUE);
                    }
                }

                /**
                 * @brief 析构函数，自动释放互斥锁
                 */
                ~MutexLockGuard()
                {
                    release();
                }

                /**
                 * @brief 禁止拷贝构造
                 */
                MutexLockGuard(const MutexLockGuard&) = delete;

                /**
                 * @brief 禁止拷贝赋值
                 */
                MutexLockGuard& operator=(const MutexLockGuard&) = delete;

                /**
                 * @brief 检查是否成功获取锁
                 * @return true 如果已获取锁，false 否则
                 */
                bool isLocked() const
                {
                    return locked_;
                }

                /**
                 * @brief 手动释放锁
                 */
                void release()
                {
                    if (locked_ && mutex_ != nullptr)
                    {
                        xSemaphoreGive(mutex_);
                        locked_ = false;
                    }
                }

            private:
                SemaphoreHandle_t mutex_;
                bool              locked_;
            };

            constexpr size_t BYTES_PER_KILOBYTE        = 1024;
            constexpr size_t BYTES_PER_MEGABYTE        = BYTES_PER_KILOBYTE * 1024;
            constexpr size_t DEFAULT_INITIAL_POOL_SIZE = 64 * BYTES_PER_KILOBYTE; // 64 KB
            constexpr double DEFAULT_EXPANSION_FACTOR  = 2.0;

            /**
             * @brief 内存池类
             */
            class MemoryPool
            {
            public:
                struct Stats
                {
                    size_t total_memory;
                    size_t used_memory;
                    size_t free_memory;
                    size_t allocated_blocks;
                    size_t free_blocks;
                };

                explicit MemoryPool(size_t initial_size     = DEFAULT_INITIAL_POOL_SIZE,
                                    size_t alignment        = alignof(std::max_align_t),
                                    double expansion_factor = DEFAULT_EXPANSION_FACTOR);

                ~MemoryPool();

                MemoryPool(const MemoryPool&)            = delete;
                MemoryPool& operator=(const MemoryPool&) = delete;

                void* allocate(size_t size);
                void  deallocate(void* ptr);
                void  reset();
                Stats getStats() const;

            private:
                struct BlockHeader
                {
                    size_t       size;
                    bool         is_free;
                    BlockHeader* next;
                    BlockHeader* prev;
                };

                struct PoolBlock
                {
                    std::unique_ptr<uint8_t[], EspHeapDeleter> memory;
                    size_t                                     size;
                    BlockHeader*                               first_block;
                };

                MutexRAII                            mutex_;
                size_t                               alignment_;
                double                               expansion_factor_;
                std::vector<PoolBlock>               pool_blocks_;
                std::unordered_map<void*, size_t>    pointer_map_;
                mutable std::multimap<size_t, void*> free_blocks_by_size_;
                mutable std::unordered_map<void*, std::multimap<size_t, void*>::iterator>
                    free_blocks_iterators_;

                void         initPool(size_t size);
                void         expandPool(size_t required_size);
                BlockHeader* findFreeBlock(size_t size);
                void         splitBlock(BlockHeader* block, size_t size);
                void         coalesceBlocks(BlockHeader* block);
                size_t       alignedSize(size_t size) const;
                size_t       getHeaderSize() const;
            };

        } // namespace memory
    } // namespace tool
} // namespace app
