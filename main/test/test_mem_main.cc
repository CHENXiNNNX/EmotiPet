#include "memory.hpp"
#include "time.hpp"

#include <cstring>

#include "esp_log.h"
#include "system/task/task.hpp"

static const char* const TAG = "MemoryTest";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== 内存池模块测试 ===");
    ESP_LOGI(TAG, "");

    // ==========================================
    // 创建内存池（使用默认 64KB）
    // ==========================================
    ESP_LOGI(TAG, "--- 创建内存池 ---");
    app::tool::memory::MemoryPool pool(32 * 1024); // 32KB 初始大小
    ESP_LOGI(TAG, "内存池创建成功（初始大小: 32KB）");
    ESP_LOGI(TAG, "");

    // ==========================================
    // 测试基本分配和释放
    // ==========================================
    ESP_LOGI(TAG, "--- 基本分配和释放测试 ---");

    void* ptr1 = pool.allocate(1024); // 分配 1KB
    if (ptr1 != nullptr)
    {
        ESP_LOGI(TAG, "分配 1KB 内存成功: %p", ptr1);
        // 使用内存（写入测试数据）
        memset(ptr1, 0xAA, 1024);
        ESP_LOGI(TAG, "内存写入测试数据成功");
    }
    else
    {
        ESP_LOGE(TAG, "分配 1KB 内存失败");
    }

    void* ptr2 = pool.allocate(2048); // 分配 2KB
    if (ptr2 != nullptr)
    {
        ESP_LOGI(TAG, "分配 2KB 内存成功: %p", ptr2);
    }
    else
    {
        ESP_LOGE(TAG, "分配 2KB 内存失败");
    }

    // 获取统计信息
    auto stats = pool.getStats();
    ESP_LOGI(TAG, "当前统计: 总内存=%lu, 已用=%lu, 空闲=%lu, 已分配块=%lu, 空闲块=%lu",
             (unsigned long)stats.total_memory, (unsigned long)stats.used_memory,
             (unsigned long)stats.free_memory, (unsigned long)stats.allocated_blocks,
             (unsigned long)stats.free_blocks);
    ESP_LOGI(TAG, "");

    // 释放内存
    if (ptr1 != nullptr)
    {
        pool.deallocate(ptr1);
        ESP_LOGI(TAG, "释放 1KB 内存成功");
    }
    if (ptr2 != nullptr)
    {
        pool.deallocate(ptr2);
        ESP_LOGI(TAG, "释放 2KB 内存成功");
    }

    stats = pool.getStats();
    ESP_LOGI(TAG, "释放后统计: 总内存=%lu, 已用=%lu, 空闲=%lu, 已分配块=%lu, 空闲块=%lu",
             (unsigned long)stats.total_memory, (unsigned long)stats.used_memory,
             (unsigned long)stats.free_memory, (unsigned long)stats.allocated_blocks,
             (unsigned long)stats.free_blocks);
    ESP_LOGI(TAG, "");

    // ==========================================
    // 测试多次分配和释放
    // ==========================================
    ESP_LOGI(TAG, "--- 多次分配和释放测试 ---");

    constexpr int NUM_ALLOCATIONS           = 10;
    void*         pointers[NUM_ALLOCATIONS] = {nullptr};

    // 分配多个内存块
    for (int i = 0; i < NUM_ALLOCATIONS; i++)
    {
        pointers[i] = pool.allocate(512); // 每个 512 字节
        if (pointers[i] != nullptr)
        {
            ESP_LOGI(TAG, "[%d] 分配 512B 成功: %p", i, pointers[i]);
        }
        else
        {
            ESP_LOGE(TAG, "[%d] 分配 512B 失败", i);
        }
        sys::task::TaskManager::delayMs(pdMS_TO_TICKS(10)); // 短暂延时
    }

    stats = pool.getStats();
    ESP_LOGI(TAG, "分配后统计: 已用=%lu, 空闲=%lu, 已分配块=%lu", (unsigned long)stats.used_memory,
             (unsigned long)stats.free_memory, (unsigned long)stats.allocated_blocks);
    ESP_LOGI(TAG, "");

    // 释放部分内存（测试块合并）
    for (int i = 0; i < NUM_ALLOCATIONS; i += 2)
    {
        if (pointers[i] != nullptr)
        {
            pool.deallocate(pointers[i]);
            pointers[i] = nullptr;
            ESP_LOGI(TAG, "[%d] 释放内存", i);
        }
    }

    stats = pool.getStats();
    ESP_LOGI(TAG, "部分释放后统计: 已用=%lu, 空闲=%lu, 已分配块=%lu, 空闲块=%lu",
             (unsigned long)stats.used_memory, (unsigned long)stats.free_memory,
             (unsigned long)stats.allocated_blocks, (unsigned long)stats.free_blocks);
    ESP_LOGI(TAG, "");

    // 释放剩余内存
    for (int i = 0; i < NUM_ALLOCATIONS; i++)
    {
        if (pointers[i] != nullptr)
        {
            pool.deallocate(pointers[i]);
        }
    }
    ESP_LOGI(TAG, "所有内存已释放");
    ESP_LOGI(TAG, "");

    // ==========================================
    // 测试内存池扩展
    // ==========================================
    ESP_LOGI(TAG, "--- 内存池扩展测试 ---");

    stats = pool.getStats();
    ESP_LOGI(TAG, "扩展前: 总内存=%lu KB", (unsigned long)(stats.total_memory / 1024));

    // 先分配一些内存占用空间
    void* temp_ptr1 = pool.allocate(10 * 1024); // 10KB
    void* temp_ptr2 = pool.allocate(10 * 1024); // 10KB
    void* temp_ptr3 = pool.allocate(5 * 1024);  // 5KB

    stats = pool.getStats();
    ESP_LOGI(TAG, "占用后: 总内存=%lu KB, 已用=%lu KB, 空闲=%lu KB",
             (unsigned long)(stats.total_memory / 1024), (unsigned long)(stats.used_memory / 1024),
             (unsigned long)(stats.free_memory / 1024));

    // 尝试分配大块内存（应该触发扩展，因为剩余空间不足）
    void* large_ptr = pool.allocate(20 * 1024); // 20KB
    if (large_ptr != nullptr)
    {
        ESP_LOGI(TAG, "分配 20KB 成功（触发扩展）: %p", large_ptr);

        stats = pool.getStats();
        ESP_LOGI(TAG, "扩展后: 总内存=%lu KB", (unsigned long)(stats.total_memory / 1024));
        ESP_LOGI(TAG, "扩展后统计: 已用=%lu KB, 空闲=%lu KB",
                 (unsigned long)(stats.used_memory / 1024),
                 (unsigned long)(stats.free_memory / 1024));

        // 释放所有内存
        pool.deallocate(large_ptr);
        if (temp_ptr1 != nullptr)
            pool.deallocate(temp_ptr1);
        if (temp_ptr2 != nullptr)
            pool.deallocate(temp_ptr2);
        if (temp_ptr3 != nullptr)
            pool.deallocate(temp_ptr3);
        ESP_LOGI(TAG, "释放所有测试内存");
    }
    else
    {
        ESP_LOGE(TAG, "分配 20KB 失败");
        // 清理已分配的内存
        if (temp_ptr1 != nullptr)
            pool.deallocate(temp_ptr1);
        if (temp_ptr2 != nullptr)
            pool.deallocate(temp_ptr2);
        if (temp_ptr3 != nullptr)
            pool.deallocate(temp_ptr3);
    }
    ESP_LOGI(TAG, "");

    // ==========================================
    // 测试内存对齐
    // ==========================================
    ESP_LOGI(TAG, "--- 内存对齐测试 ---");

    void* aligned_ptr = pool.allocate(100); // 分配 100 字节
    if (aligned_ptr != nullptr)
    {
        uintptr_t addr = reinterpret_cast<uintptr_t>(aligned_ptr);
        ESP_LOGI(TAG, "分配 100B, 地址: %p (0x%lx)", aligned_ptr, addr);
        ESP_LOGI(TAG, "地址对齐检查: %s",
                 (addr % alignof(std::max_align_t) == 0) ? "已对齐" : "未对齐");
        pool.deallocate(aligned_ptr);
    }
    ESP_LOGI(TAG, "");

    // ==========================================
    // 最终统计
    // ==========================================
    ESP_LOGI(TAG, "--- 最终统计信息 ---");
    stats = pool.getStats();
    ESP_LOGI(TAG, "总内存: %lu KB", (unsigned long)(stats.total_memory / 1024));
    ESP_LOGI(TAG, "已用内存: %lu KB", (unsigned long)(stats.used_memory / 1024));
    ESP_LOGI(TAG, "空闲内存: %lu KB", (unsigned long)(stats.free_memory / 1024));
    ESP_LOGI(TAG, "已分配块: %lu", (unsigned long)stats.allocated_blocks);
    ESP_LOGI(TAG, "空闲块: %lu", (unsigned long)stats.free_blocks);
    ESP_LOGI(TAG, "");

    // ==========================================
    // 测试重置功能
    // ==========================================
    ESP_LOGI(TAG, "--- 重置内存池测试 ---");
    pool.reset();
    stats = pool.getStats();
    ESP_LOGI(TAG, "重置后统计: 总内存=%lu, 已用=%lu, 空闲=%lu", (unsigned long)stats.total_memory,
             (unsigned long)stats.used_memory, (unsigned long)stats.free_memory);
    ESP_LOGI(TAG, "");

    ESP_LOGI(TAG, "=== 测试完成 ===");
}
