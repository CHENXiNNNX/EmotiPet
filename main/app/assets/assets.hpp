#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>

#include <esp_partition.h>
#include <model_path.h>

namespace app
{
    namespace assets
    {
        struct Asset
        {
            size_t size;
            size_t offset;
        };

        /**
         * @brief Assets 资源管理器类
         */
        class Assets
        {
        public:
            /**
             * @brief 获取单例实例
             */
            static Assets& getInstance();

            /**
             * @brief 初始化 Assets 系统
             * @return true 成功, false 失败
             */
            bool init();

            /**
             * @brief 应用资源配置（加载模型等）
             * @return true 成功, false 失败
             */
            bool apply();

            /**
             * @brief 获取指定资源的数据
             * @param name 资源名称
             * @param ptr 输出：指向资源数据的指针
             * @param size 输出：资源大小
             * @return true 成功, false 失败
             */
            bool getAssetData(const std::string& name, void*& ptr, size_t& size);

            /**
             * @brief 获取加载的模型列表
             * @return 模型列表指针，如果未加载则返回 nullptr
             */
            srmodel_list_t* getModelsList() const
            {
                return models_list_;
            }

            /**
             * @brief 检查分区是否有效
             */
            bool isPartitionValid() const
            {
                return partition_valid_;
            }

            /**
             * @brief 检查校验和是否有效
             */
            bool isChecksumValid() const
            {
                return checksum_valid_;
            }

            /**
             * @brief 下载新的 assets 文件（可选功能）
             * @param url 下载地址
             * @param progress_callback 进度回调函数 (progress%, speed)
             * @return true 成功, false 失败
             */
            bool download(const std::string&                           url,
                          std::function<void(int progress, size_t speed)> progress_callback = nullptr);

        private:
            Assets();
            ~Assets();
            Assets(const Assets&)            = delete;
            Assets& operator=(const Assets&) = delete;

            bool     initializePartition();
            uint32_t calculateChecksum(const char* data, uint32_t length);

            const esp_partition_t*       partition_       = nullptr;
            esp_partition_mmap_handle_t  mmap_handle_     = 0;
            const char*                  mmap_root_       = nullptr;
            bool                         partition_valid_ = false;
            bool                         checksum_valid_  = false;
            std::map<std::string, Asset> assets_;
            srmodel_list_t*              models_list_     = nullptr;
        };

    } // namespace assets
} // namespace app

