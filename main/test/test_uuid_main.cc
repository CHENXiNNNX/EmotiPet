#include "uuid.hpp"

#include "esp_log.h"
#include "system/task/task.hpp"

static const char* const TAG = "UuidTest";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "=== UUID 模块测试 ===");
    ESP_LOGI(TAG, "");

    // ==========================================
    // 生成 UUID
    // ==========================================
    ESP_LOGI(TAG, "--- 生成 UUID 测试 ---");

    app::tool::uuid::Uuid uuid1 = app::tool::uuid::generate();
    app::tool::uuid::Uuid uuid2 = app::tool::uuid::generate();

    char buffer1[app::tool::uuid::UUID_STRING_SIZE];
    char buffer2[app::tool::uuid::UUID_STRING_SIZE];

    if (app::tool::uuid::toString(uuid1, buffer1, sizeof(buffer1)))
    {
        ESP_LOGI(TAG, "UUID 1: %s", buffer1);
    }
    else
    {
        ESP_LOGE(TAG, "UUID 1 格式化失败");
    }

    if (app::tool::uuid::toString(uuid2, buffer2, sizeof(buffer2)))
    {
        ESP_LOGI(TAG, "UUID 2: %s", buffer2);
    }
    else
    {
        ESP_LOGE(TAG, "UUID 2 格式化失败");
    }

    ESP_LOGI(TAG, "");

    // ==========================================
    // UUID 比较测试
    // ==========================================
    ESP_LOGI(TAG, "--- UUID 比较测试 ---");

    if (uuid1 == uuid2)
    {
        ESP_LOGI(TAG, "UUID 1 和 UUID 2 相同（不应该发生）");
    }
    else
    {
        ESP_LOGI(TAG, "UUID 1 和 UUID 2 不同（正确）");
    }

    app::tool::uuid::Uuid uuid1_copy = uuid1;
    if (uuid1 == uuid1_copy)
    {
        ESP_LOGI(TAG, "UUID 1 和其副本相同（正确）");
    }
    else
    {
        ESP_LOGE(TAG, "UUID 1 和其副本不同（错误）");
    }

    ESP_LOGI(TAG, "");

    // ==========================================
    // 从字符串解析 UUID
    // ==========================================
    ESP_LOGI(TAG, "--- 字符串解析测试 ---");

    const char*           test_uuid_str = "550e8400-e29b-41d4-a716-446655440000";
    app::tool::uuid::Uuid parsed_uuid;

    if (app::tool::uuid::fromString(test_uuid_str, parsed_uuid))
    {
        char parsed_buffer[app::tool::uuid::UUID_STRING_SIZE];
        if (app::tool::uuid::toString(parsed_uuid, parsed_buffer, sizeof(parsed_buffer)))
        {
            ESP_LOGI(TAG, "解析成功: %s", test_uuid_str);
            ESP_LOGI(TAG, "格式化后: %s", parsed_buffer);
        }
    }
    else
    {
        ESP_LOGE(TAG, "解析失败: %s", test_uuid_str);
    }

    // 测试无效格式
    const char*           invalid_str = "invalid-uuid-format";
    app::tool::uuid::Uuid invalid_uuid;
    if (!app::tool::uuid::fromString(invalid_str, invalid_uuid))
    {
        ESP_LOGI(TAG, "无效格式检测成功: %s", invalid_str);
    }
    else
    {
        ESP_LOGE(TAG, "无效格式检测失败");
    }

    ESP_LOGI(TAG, "");

    // ==========================================
    // 空值检查测试
    // ==========================================
    ESP_LOGI(TAG, "--- 空值检查测试 ---");

    app::tool::uuid::Uuid empty_uuid = {};
    memset(empty_uuid.data, 0, app::tool::uuid::UUID_BYTE_SIZE);

    if (app::tool::uuid::isEmpty(empty_uuid))
    {
        ESP_LOGI(TAG, "空 UUID 检测成功");
    }
    else
    {
        ESP_LOGE(TAG, "空 UUID 检测失败");
    }

    if (!app::tool::uuid::isEmpty(uuid1))
    {
        ESP_LOGI(TAG, "非空 UUID 检测成功");
    }
    else
    {
        ESP_LOGE(TAG, "非空 UUID 检测失败");
    }

    ESP_LOGI(TAG, "");

    // ==========================================
    // 生成多个 UUID 验证唯一性
    // ==========================================
    ESP_LOGI(TAG, "--- 批量生成 UUID 测试 ---");

    constexpr int         NUM_UUIDS = 5;
    app::tool::uuid::Uuid uuids[NUM_UUIDS];
    char                  uuid_buffers[NUM_UUIDS][app::tool::uuid::UUID_STRING_SIZE];

    for (int i = 0; i < NUM_UUIDS; i++)
    {
        uuids[i] = app::tool::uuid::generate();
        if (app::tool::uuid::toString(uuids[i], uuid_buffers[i], sizeof(uuid_buffers[i])))
        {
            ESP_LOGI(TAG, "[%d] %s", i + 1, uuid_buffers[i]);
        }
        app::sys::task::TaskManager::delayMs(10); // 短暂延时
    }

    // 验证唯一性
    bool all_unique = true;
    for (int i = 0; i < NUM_UUIDS; i++)
    {
        for (int j = i + 1; j < NUM_UUIDS; j++)
        {
            if (uuids[i] == uuids[j])
            {
                ESP_LOGE(TAG, "发现重复 UUID: [%d] 和 [%d]", i + 1, j + 1);
                all_unique = false;
            }
        }
    }

    if (all_unique)
    {
        ESP_LOGI(TAG, "所有 UUID 都是唯一的（正确）");
    }

    ESP_LOGI(TAG, "");

    // ==========================================
    // UUID 格式验证
    // ==========================================
    ESP_LOGI(TAG, "--- UUID 格式验证 ---");

    app::tool::uuid::Uuid test_uuid = app::tool::uuid::generate();
    char                  format_buffer[app::tool::uuid::UUID_STRING_SIZE];

    if (app::tool::uuid::toString(test_uuid, format_buffer, sizeof(format_buffer)))
    {
        ESP_LOGI(TAG, "生成的 UUID: %s", format_buffer);

        // 验证格式（检查连字符位置）
        bool format_valid = true;
        if (format_buffer[8] != '-' || format_buffer[13] != '-' || format_buffer[18] != '-' ||
            format_buffer[23] != '-')
        {
            format_valid = false;
        }

        // 验证版本号（第 14 个字符应该是 '4'）
        if (format_buffer[14] != '4')
        {
            format_valid = false;
        }

        if (format_valid)
        {
            ESP_LOGI(TAG, "UUID 格式验证通过");
        }
        else
        {
            ESP_LOGE(TAG, "UUID 格式验证失败");
        }
    }

    ESP_LOGI(TAG, "");

    // ==========================================
    // 往返测试（生成 -> 字符串 -> 解析 -> 比较）
    // ==========================================
    ESP_LOGI(TAG, "--- 往返测试 ---");

    app::tool::uuid::Uuid original = app::tool::uuid::generate();
    char                  roundtrip_buffer[app::tool::uuid::UUID_STRING_SIZE];
    app::tool::uuid::Uuid roundtrip_uuid;

    if (app::tool::uuid::toString(original, roundtrip_buffer, sizeof(roundtrip_buffer)))
    {
        ESP_LOGI(TAG, "原始 UUID: %s", roundtrip_buffer);

        if (app::tool::uuid::fromString(roundtrip_buffer, roundtrip_uuid))
        {
            if (original == roundtrip_uuid)
            {
                ESP_LOGI(TAG, "往返测试成功：UUID 保持一致");
            }
            else
            {
                ESP_LOGE(TAG, "往返测试失败：UUID 不一致");
            }
        }
        else
        {
            ESP_LOGE(TAG, "往返测试失败：无法解析");
        }
    }

    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "=== 测试完成 ===");
}
