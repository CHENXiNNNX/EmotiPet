#include "assets/assets.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "nvs_flash.h"

static const char* const TAG = "Main";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "  EmotiPet Assets 测试程序");
    ESP_LOGI(TAG, "========================================");

    // 初始化 NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 获取 Assets 单例
    auto& assets = app::assets::Assets::getInstance();

    // 测试 1: 初始化 Assets
    ESP_LOGI(TAG, "\n[测试 1] 初始化 Assets 系统...");
    if (!assets.init())
    {
        ESP_LOGE(TAG, "Assets 初始化失败");
        ESP_LOGE(TAG, "  可能原因:");
        ESP_LOGE(TAG, "  1. assets 分区不存在或未烧录");
        ESP_LOGE(TAG, "  2. assets.bin 文件损坏");
        ESP_LOGE(TAG, "  3. 校验和错误");
        return;
    }
    ESP_LOGI(TAG, "Assets 初始化成功");

    // 测试 2: 检查分区状态
    ESP_LOGI(TAG, "\n[测试 2] 检查分区状态...");
    if (assets.isPartitionValid())
    {
        ESP_LOGI(TAG, "分区有效");
    }
    else
    {
        ESP_LOGE(TAG, "分区无效");
        return;
    }

    if (assets.isChecksumValid())
    {
        ESP_LOGI(TAG, "校验和正确");
    }
    else
    {
        ESP_LOGE(TAG, "校验和错误");
        return;
    }

    // 测试 3: 读取 index.json
    ESP_LOGI(TAG, "\n[测试 3] 读取 index.json...");
    void*  json_ptr  = nullptr;
    size_t json_size = 0;
    if (assets.getAssetData("index.json", json_ptr, json_size))
    {
        ESP_LOGI(TAG, "读取成功，大小: %u 字节", json_size);

        // 打印 JSON 内容（前 200 字节）
        if (json_size > 0)
        {
            size_t print_size = json_size < 200 ? json_size : 200;
            ESP_LOGI(TAG, "内容预览:");
            ESP_LOG_BUFFER_CHAR(TAG, json_ptr, print_size);
            if (json_size > 200)
            {
                ESP_LOGI(TAG, "... (还有 %u 字节)", json_size - 200);
            }
        }
    }
    else
    {
        ESP_LOGE(TAG, "读取 index.json 失败");
        ESP_LOGE(TAG, "  assets.bin 可能未包含 index.json");
    }

    // 测试 4: 读取 srmodels.bin
    ESP_LOGI(TAG, "\n[测试 4] 读取 srmodels.bin...");
    void*  model_ptr  = nullptr;
    size_t model_size = 0;
    if (assets.getAssetData("srmodels.bin", model_ptr, model_size))
    {
        ESP_LOGI(TAG, "读取成功，大小: %u 字节 (%.2f KB)", model_size, model_size / 1024.0f);
    }
    else
    {
        ESP_LOGW(TAG, "⚠ 未找到 srmodels.bin");
        ESP_LOGW(TAG, "  这是正常的，如果你还没有烧录模型文件");
    }

    // 测试 5: 应用配置（加载模型）
    ESP_LOGI(TAG, "\n[测试 5] 应用配置并加载模型...");
    if (!assets.apply())
    {
        ESP_LOGE(TAG, "应用配置失败");
        ESP_LOGE(TAG, "  可能原因:");
        ESP_LOGE(TAG, "  1. index.json 格式错误");
        ESP_LOGE(TAG, "  2. srmodels.bin 不存在或损坏");
        ESP_LOGE(TAG, "  3. 模型文件格式不正确");
    }
    else
    {
        ESP_LOGI(TAG, "配置应用成功");
    }

    // 测试 6: 检查模型列表
    ESP_LOGI(TAG, "\n[测试 6] 检查加载的模型...");
    srmodel_list_t* models = assets.getModelsList();
    if (models != nullptr && models->num > 0)
    {
        ESP_LOGI(TAG, "成功加载 %d 个模型:", models->num);
        for (int i = 0; i < models->num; i++)
        {
            ESP_LOGI(TAG, "  [%d] %s", i, models->model_name[i]);
        }
    }
    else
    {
        ESP_LOGW(TAG, "⚠ 未加载任何模型");
        ESP_LOGW(TAG, "  这是正常的，如果:");
        ESP_LOGW(TAG, "  1. assets.bin 中没有包含模型文件");
        ESP_LOGW(TAG, "  2. 或者 index.json 中未配置 srmodels");
    }

    // 测试总结
    ESP_LOGI(TAG, "\n========================================");
    ESP_LOGI(TAG, "  测试完成！");
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "如果看到错误，请检查:");
    ESP_LOGI(TAG, "1. 是否已执行 'idf.py build' 生成 assets.bin");
    ESP_LOGI(TAG, "2. 是否已执行 'idf.py flash' 烧录 assets 分区");
    ESP_LOGI(TAG, "3. partition.csv 中是否正确配置了 assets 分区");
    ESP_LOGI(TAG, "4. ESP-SR 组件是否正确安装");
    ESP_LOGI(TAG, "");
    ESP_LOGI(TAG, "如果一切正常，可以继续实现唤醒词功能！");
    ESP_LOGI(TAG, "");

    // 保持程序运行
    while (true)
    {
        app::sys::task::TaskManager::delayMs(10000);
    }
}
