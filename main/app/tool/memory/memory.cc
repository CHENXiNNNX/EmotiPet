#include "memory.hpp"

#include <algorithm>

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace app
{
    namespace tool
    {
        namespace memory
        {

            MemoryPool::MemoryPool(size_t initial_size, size_t alignment, double expansion_factor)
                : mutex_(MutexRAII::create()), alignment_(alignment), expansion_factor_(expansion_factor)
            {
                // 如果互斥锁创建失败，使用默认对齐方式并返回
                if (!mutex_.isValid())
                {
                    alignment_ = alignof(std::max_align_t);
                    return;
                }

                // 验证并修正对齐参数（必须是 2 的幂）
                if (alignment_ == 0 || (alignment_ & (alignment_ - 1)) != 0)
                {
                    alignment_ = alignof(std::max_align_t);
                }

                // 验证并修正扩展因子（必须 >= 1.0）
                if (expansion_factor_ < 1.0)
                {
                    expansion_factor_ = 1.0;
                }

                // 初始化内存池
                initPool(initial_size);
            }

            MemoryPool::~MemoryPool()
            {
                // 清理所有资源
                reset();
                // mutex_ 会在析构函数中自动删除
            }

            void MemoryPool::initPool(size_t size)
            {
                size_t aligned_size_value = alignedSize(size);
                size_t header_size        = getHeaderSize();
                size_t total_size         = aligned_size_value + alignment_;

                // 优先从内部 SRAM 分配，失败则使用默认堆
                uint8_t* memory = static_cast<uint8_t*>(
                    heap_caps_malloc(total_size, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
                if (memory == nullptr)
                {
                    memory =
                        static_cast<uint8_t*>(heap_caps_malloc(total_size, MALLOC_CAP_DEFAULT));
                }

                // 如果分配失败，直接返回
                if (memory == nullptr)
                {
                    return;
                }

                // 计算对齐后的内存地址
                uintptr_t memory_addr    = reinterpret_cast<uintptr_t>(memory);
                size_t    offset         = (alignment_ - (memory_addr % alignment_)) % alignment_;
                uint8_t*  aligned_memory = memory + offset;

                // 创建内存池块，使用智能指针管理内存）
                PoolBlock pool_block;
                pool_block.memory = std::unique_ptr<uint8_t[], EspHeapDeleter>(memory);
                pool_block.size   = aligned_size_value;

                // 初始化第一个块头
                auto* first_block    = reinterpret_cast<BlockHeader*>(aligned_memory);
                first_block->size    = aligned_size_value - header_size;
                first_block->is_free = true;
                first_block->next    = nullptr;
                first_block->prev    = nullptr;

                pool_block.first_block = first_block;
                pool_blocks_.push_back(std::move(pool_block));

                // 将第一个块添加到空闲块列表
                void* first_block_ptr = reinterpret_cast<uint8_t*>(first_block) + header_size;
                auto  it              = free_blocks_by_size_.insert({first_block->size, first_block_ptr});
                free_blocks_iterators_[first_block_ptr] = it;
            }

            void* MemoryPool::allocate(size_t size)
            {
                // 参数验证
                if (size == 0 || !mutex_.isValid())
                {
                    return nullptr;
                }

                // 使用 RAII 锁守卫，自动管理互斥锁
                MutexLockGuard lock(mutex_.get());
                if (!lock.isLocked())
                {
                    return nullptr;
                }

                size_t       aligned_request_size = alignedSize(size);
                size_t       pool_index           = 0;
                BlockHeader* block                = nullptr;
                void*        found_ptr            = nullptr;

                // 尝试从空闲块列表中找到合适大小的块
                auto it = free_blocks_by_size_.lower_bound(aligned_request_size);
                if (it != free_blocks_by_size_.end())
                {
                    found_ptr = it->second;
                    block     = reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(found_ptr) -
                                                             getHeaderSize());
                    auto map_it = pointer_map_.find(found_ptr);
                    if (map_it != pointer_map_.end())
                    {
                        pool_index = map_it->second;
                    }
                    free_blocks_by_size_.erase(it);
                    free_blocks_iterators_.erase(found_ptr);
                }

                // 如果没有找到合适的块，尝试扩展内存池
                if (!block)
                {
                    expandPool(aligned_request_size);
                    pool_index = pool_blocks_.size() - 1;
                    block      = pool_blocks_[pool_index].first_block;
                    if (!block || !block->is_free || block->size < aligned_request_size)
                    {
                        return nullptr; // lock 会在返回时自动释放
                    }

                    void* block_ptr = reinterpret_cast<uint8_t*>(block) + getHeaderSize();
                    auto  iter_it   = free_blocks_iterators_.find(block_ptr);
                    if (iter_it != free_blocks_iterators_.end())
                    {
                        free_blocks_by_size_.erase(iter_it->second);
                        free_blocks_iterators_.erase(iter_it);
                    }
                }

                // 分割块（如果可能）
                splitBlock(block, aligned_request_size);
                block->is_free = false;

                // 记录分配的块
                auto* user_data         = reinterpret_cast<uint8_t*>(block) + getHeaderSize();
                pointer_map_[user_data] = pool_index;

                // lock 会在返回时自动释放
                return user_data;
            }

            void MemoryPool::deallocate(void* ptr)
            {
                // 参数验证
                if (!ptr || !mutex_.isValid())
                {
                    return;
                }

                // 使用 RAII 锁守卫
                MutexLockGuard lock(mutex_.get());
                if (!lock.isLocked())
                {
                    return;
                }

                // 查找指针对应的块
                auto pointer_iter = pointer_map_.find(ptr);
                if (pointer_iter == pointer_map_.end())
                {
                    return; // lock 会在返回时自动释放
                }

                size_t pool_index = pointer_iter->second;
                if (pool_index >= pool_blocks_.size())
                {
                    return; // lock 会在返回时自动释放
                }

                // 获取块头
                auto* block = reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(ptr) -
                                                             getHeaderSize());

                // 标记块为空闲
                block->is_free = true;
                void* block_ptr = reinterpret_cast<uint8_t*>(block) + getHeaderSize();
                auto  it       = free_blocks_by_size_.insert({block->size, block_ptr});
                free_blocks_iterators_[block_ptr] = it;

                // 合并相邻的空闲块
                coalesceBlocks(block);

                // 从指针映射中移除
                pointer_map_.erase(pointer_iter);

                // lock 会在返回时自动释放
            }

            void MemoryPool::reset()
            {
                if (!mutex_.isValid())
                {
                    return;
                }

                // 使用 RAII 锁守卫
                MutexLockGuard lock(mutex_.get());
                if (!lock.isLocked())
                {
                    return;
                }

                // 清空所有数据结构
                // pool_blocks_ 中的 unique_ptr 会自动释放内存（RAII）
                pool_blocks_.clear();
                pointer_map_.clear();
                free_blocks_by_size_.clear();
                free_blocks_iterators_.clear();

                // lock 会在返回时自动释放
            }

            MemoryPool::Stats MemoryPool::getStats() const
            {
                Stats stats{0, 0, 0, 0, 0};

                if (!mutex_.isValid())
                {
                    return stats;
                }

                // 使用 RAII 锁守卫
                MutexLockGuard lock(mutex_.get());
                if (!lock.isLocked())
                {
                    return stats;
                }

                // 遍历所有内存池块，统计信息
                for (const auto& pool_block : pool_blocks_)
                {
                    stats.total_memory += pool_block.size;

                    BlockHeader* current = pool_block.first_block;
                    while (current)
                    {
                        if (current->is_free)
                        {
                            stats.free_memory += current->size;
                            stats.free_blocks++;
                        }
                        else
                        {
                            stats.used_memory += current->size;
                            stats.allocated_blocks++;
                        }
                        current = current->next;
                    }
                }

                // lock 会在返回时自动释放
                return stats;
            }

            void MemoryPool::expandPool(size_t required_size)
            {
                auto new_size = static_cast<size_t>(
                    pool_blocks_.empty() ? required_size
                                         : pool_blocks_.back().size * expansion_factor_);

                new_size = std::max(new_size, required_size + getHeaderSize());
                initPool(new_size);
            }

            MemoryPool::BlockHeader* MemoryPool::findFreeBlock(size_t size)
            {
                auto it = free_blocks_by_size_.lower_bound(size);
                if (it != free_blocks_by_size_.end())
                {
                    void* ptr   = it->second;
                    auto* block = reinterpret_cast<BlockHeader*>(reinterpret_cast<uint8_t*>(ptr) -
                                                                 getHeaderSize());
                    return block;
                }

                for (auto& pool_block : pool_blocks_)
                {
                    BlockHeader* current = pool_block.first_block;
                    while (current)
                    {
                        if (current->is_free && current->size >= size)
                        {
                            return current;
                        }
                        current = current->next;
                    }
                }

                return nullptr;
            }

            void MemoryPool::splitBlock(BlockHeader* block, size_t size)
            {
                size_t min_block_size = getHeaderSize() + alignment_;
                if (block->size >= size + min_block_size)
                {
                    auto* new_block_address =
                        reinterpret_cast<uint8_t*>(block) + getHeaderSize() + size;
                    auto* new_block = reinterpret_cast<BlockHeader*>(new_block_address);

                    new_block->size    = block->size - size - getHeaderSize();
                    new_block->is_free = true;
                    new_block->next    = block->next;
                    new_block->prev    = block;

                    block->size = size;
                    block->next = new_block;

                    if (new_block->next)
                    {
                        new_block->next->prev = new_block;
                    }

                    void* new_block_ptr = reinterpret_cast<uint8_t*>(new_block) + getHeaderSize();
                    auto  it = free_blocks_by_size_.insert({new_block->size, new_block_ptr});
                    free_blocks_iterators_[new_block_ptr] = it;
                }
            }

            void MemoryPool::coalesceBlocks(BlockHeader* block)
            {
                BlockHeader* final_block = block;
                void*        block_ptr   = reinterpret_cast<uint8_t*>(block) + getHeaderSize();

                auto iter_it = free_blocks_iterators_.find(block_ptr);
                if (iter_it != free_blocks_iterators_.end())
                {
                    free_blocks_by_size_.erase(iter_it->second);
                    free_blocks_iterators_.erase(iter_it);
                }

                if (block->next && block->next->is_free)
                {
                    BlockHeader* next_block = block->next;
                    void* next_block_ptr = reinterpret_cast<uint8_t*>(next_block) + getHeaderSize();

                    auto next_iter_it = free_blocks_iterators_.find(next_block_ptr);
                    if (next_iter_it != free_blocks_iterators_.end())
                    {
                        free_blocks_by_size_.erase(next_iter_it->second);
                        free_blocks_iterators_.erase(next_iter_it);
                    }

                    block->size += next_block->size + getHeaderSize();
                    block->next = next_block->next;

                    if (next_block->next)
                    {
                        next_block->next->prev = block;
                    }
                }

                if (block->prev && block->prev->is_free)
                {
                    BlockHeader* previous_block = block->prev;
                    void*        prev_block_ptr =
                        reinterpret_cast<uint8_t*>(previous_block) + getHeaderSize();

                    auto prev_iter_it = free_blocks_iterators_.find(prev_block_ptr);
                    if (prev_iter_it != free_blocks_iterators_.end())
                    {
                        free_blocks_by_size_.erase(prev_iter_it->second);
                        free_blocks_iterators_.erase(prev_iter_it);
                    }

                    previous_block->size += block->size + getHeaderSize();
                    previous_block->next = block->next;

                    if (block->next)
                    {
                        block->next->prev = previous_block;
                    }

                    final_block = previous_block;
                }

                void* final_block_ptr = reinterpret_cast<uint8_t*>(final_block) + getHeaderSize();
                auto  it = free_blocks_by_size_.insert({final_block->size, final_block_ptr});
                free_blocks_iterators_[final_block_ptr] = it;
            }

            size_t MemoryPool::alignedSize(size_t size) const
            {
                return (size + alignment_ - 1) & ~(alignment_ - 1);
            }

            size_t MemoryPool::getHeaderSize() const
            {
                return alignedSize(sizeof(BlockHeader));
            }

        } // namespace memory
    }     // namespace tool
} // namespace app
