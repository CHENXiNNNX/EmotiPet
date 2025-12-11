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

                mutable SemaphoreHandle_t            mutex_;
                size_t                               alignment_;
                double                               expansion_factor_;
                std::vector<PoolBlock>               pool_blocks_;
                std::unordered_map<void*, size_t>    pointer_map_;
                mutable std::multimap<size_t, void*> free_blocks_by_size_;
                mutable std::unordered_map<void*, std::multimap<size_t, void*>::iterator>
                    free_blocks_iterators_;

                void         initializePool(size_t size);
                void         expandPool(size_t required_size);
                BlockHeader* findFreeBlock(size_t size);
                void         splitBlock(BlockHeader* block, size_t size);
                void         coalesceBlocks(BlockHeader* block);
                size_t       alignedSize(size_t size) const;
                size_t       getHeaderSize() const;
            };

        } // namespace memory
    }     // namespace tool
} // namespace app
