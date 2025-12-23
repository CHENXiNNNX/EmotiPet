#include "assets.hpp"

#include <cJSON.h>
#include <esp_log.h>
#include <esp_timer.h>
#include <spi_flash_mmap.h>

static const char* const TAG = "Assets";

namespace app
{
    namespace assets
    {
        struct MmapAssetsTable
        {
            char     asset_name[32];
            uint32_t asset_size;
            uint32_t asset_offset;
            uint16_t asset_width;
            uint16_t asset_height;
        };

        Assets& Assets::getInstance()
        {
            static Assets instance;
            return instance;
        }

        Assets::Assets() = default;

        Assets::~Assets()
        {
            if (mmap_handle_ != 0)
            {
                esp_partition_munmap(mmap_handle_);
                mmap_handle_ = 0;
            }

            if (models_list_ != nullptr)
            {
                esp_srmodel_deinit(models_list_);
                models_list_ = nullptr;
            }
        }

        uint32_t Assets::calculateChecksum(const char* data, uint32_t length)
        {
            uint32_t checksum = 0;
            for (uint32_t i = 0; i < length; i++)
            {
                checksum += static_cast<uint8_t>(data[i]);
            }
            return checksum & 0xFFFF;
        }

        bool Assets::initializePartition()
        {
            partition_valid_ = false;
            checksum_valid_  = false;
            assets_.clear();

            partition_ = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                  ESP_PARTITION_SUBTYPE_ANY, "assets");
            if (partition_ == nullptr)
            {
                ESP_LOGE(TAG, "未找到 assets 分区");
                return false;
            }

            int      free_pages   = spi_flash_mmap_get_free_pages(SPI_FLASH_MMAP_DATA);
            uint32_t storage_size = free_pages * 64 * 1024;

            if (storage_size < partition_->size)
            {
                ESP_LOGE(TAG, "mmap 空间不足");
                return false;
            }

            const void* mmap_ptr = nullptr;
            esp_err_t   err      = esp_partition_mmap(partition_, 0, partition_->size,
                                                      ESP_PARTITION_MMAP_DATA, &mmap_ptr, &mmap_handle_);
            mmap_root_           = static_cast<const char*>(mmap_ptr);
            if (err != ESP_OK)
            {
                ESP_LOGE(TAG, "内存映射失败: %s", esp_err_to_name(err));
                return false;
            }

            partition_valid_ = true;

            uint32_t stored_files  = *reinterpret_cast<const uint32_t*>(mmap_root_ + 0);
            uint32_t stored_chksum = *reinterpret_cast<const uint32_t*>(mmap_root_ + 4);
            uint32_t stored_len    = *reinterpret_cast<const uint32_t*>(mmap_root_ + 8);

            if (stored_len > partition_->size - 12)
            {
                ESP_LOGE(TAG, "数据长度无效");
                return false;
            }

            uint32_t calculated_checksum = calculateChecksum(mmap_root_ + 12, stored_len);

            if (calculated_checksum != stored_chksum)
            {
                ESP_LOGE(TAG, "校验和不匹配");
                return false;
            }

            checksum_valid_ = true;

            for (uint32_t i = 0; i < stored_files; i++)
            {
                const auto* item = reinterpret_cast<const MmapAssetsTable*>(
                    mmap_root_ + 12 + i * sizeof(MmapAssetsTable));

                Asset asset;
                asset.size   = static_cast<size_t>(item->asset_size);
                asset.offset = static_cast<size_t>(12 + sizeof(MmapAssetsTable) * stored_files +
                                                   item->asset_offset);
                assets_[item->asset_name] = asset;
            }

            ESP_LOGI(TAG, "Assets 初始化成功 (文件: %lu, 大小: %lu KB)", stored_files,
                     partition_->size / 1024);
            return true;
        }

        bool Assets::init()
        {
            return initializePartition();
        }

        bool Assets::apply()
        {
            if (!partition_valid_ || !checksum_valid_)
            {
                ESP_LOGE(TAG, "Assets 分区无效");
                return false;
            }

            void*  ptr  = nullptr;
            size_t size = 0;

            if (!getAssetData("index.json", ptr, size))
            {
                ESP_LOGE(TAG, "未找到 index.json");
                return false;
            }

            cJSON* root = cJSON_ParseWithLength(static_cast<const char*>(ptr), size);
            if (root == nullptr)
            {
                ESP_LOGE(TAG, "解析 index.json 失败");
                return false;
            }

            cJSON* version = cJSON_GetObjectItem(root, "version");
            if (cJSON_IsNumber(version) && version->valuedouble > 1)
            {
                ESP_LOGE(TAG, "不支持的 assets 版本: %d", version->valueint);
                cJSON_Delete(root);
                return false;
            }

            cJSON* srmodels = cJSON_GetObjectItem(root, "srmodels");
            if (cJSON_IsString(srmodels))
            {
                std::string srmodels_file = srmodels->valuestring;

                if (getAssetData(srmodels_file, ptr, size))
                {
                    if (models_list_ != nullptr)
                    {
                        esp_srmodel_deinit(models_list_);
                        models_list_ = nullptr;
                    }

                    models_list_ = srmodel_load(static_cast<uint8_t*>(ptr));
                    if (models_list_ != nullptr)
                    {
                        ESP_LOGI(TAG, "SR 模型加载成功 (%d 个)", models_list_->num);
                    }
                    else
                    {
                        ESP_LOGE(TAG, "加载 SR 模型失败");
                    }
                }
                else
                {
                    ESP_LOGE(TAG, "未找到 SR 模型文件");
                }
            }

            cJSON_Delete(root);
            return true;
        }

        bool Assets::getAssetData(const std::string& name, void*& ptr, size_t& size)
        {
            auto asset = assets_.find(name);
            if (asset == assets_.end())
            {
                return false;
            }

            const char* data = mmap_root_ + asset->second.offset;

            if (static_cast<uint8_t>(data[0]) != 0x5A || static_cast<uint8_t>(data[1]) != 0x5A)
            {
                ESP_LOGE(TAG, "资源魔数无效: %s", name.c_str());
                return false;
            }

            ptr  = reinterpret_cast<void*>(const_cast<char*>(data + 2));
            size = asset->second.size;

            return true;
        }

        bool Assets::download(const std::string&                              url,
                              std::function<void(int progress, size_t speed)> progress_callback)
        {
            (void)url;
            (void)progress_callback;
            ESP_LOGE(TAG, "下载功能未实现");
            return false;
        }

    } // namespace assets
} // namespace app
