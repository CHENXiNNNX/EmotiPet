#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>

#include "cJSON.h"
#include "mbedtls/md5.h"

namespace app
{
    namespace tool
    {
        namespace ota
        {

            /**
             * @brief cJSON RAII 包装器
             *
             * 自动管理 cJSON 对象的生命周期，确保正确释放
             */
            class JsonRAII
            {
            public:
                /**
                 * @brief 构造函数，从字符串解析 JSON
                 * @param json_str JSON 字符串
                 */
                explicit JsonRAII(const char* json_str) : json_(cJSON_Parse(json_str)) {}

                /**
                 * @brief 构造函数，创建空 JSON 对象
                 */
                JsonRAII() : json_(cJSON_CreateObject()) {}

                /**
                 * @brief 禁止拷贝构造
                 */
                JsonRAII(const JsonRAII&) = delete;

                /**
                 * @brief 禁止拷贝赋值
                 */
                JsonRAII& operator=(const JsonRAII&) = delete;

                /**
                 * @brief 移动构造函数
                 */
                JsonRAII(JsonRAII&& other) noexcept : json_(other.json_)
                {
                    other.json_ = nullptr;
                }

                /**
                 * @brief 移动赋值运算符
                 */
                JsonRAII& operator=(JsonRAII&& other) noexcept
                {
                    if (this != &other)
                    {
                        reset();
                        json_       = other.json_;
                        other.json_ = nullptr;
                    }
                    return *this;
                }

                /**
                 * @brief 析构函数，自动删除 JSON 对象
                 */
                ~JsonRAII()
                {
                    reset();
                }

                /**
                 * @brief 获取 cJSON 指针
                 * @return cJSON 指针，如果未创建则返回 nullptr
                 */
                cJSON* get() const
                {
                    return json_;
                }

                /**
                 * @brief 检查 JSON 对象是否有效
                 * @return true 如果有效，false 否则
                 */
                bool isValid() const
                {
                    return json_ != nullptr;
                }

                /**
                 * @brief 获取 cJSON 指针（用于隐式转换）
                 */
                operator cJSON*() const
                {
                    return json_;
                }

                /**
                 * @brief 重置 JSON 对象（删除当前对象）
                 */
                void reset()
                {
                    if (json_ != nullptr)
                    {
                        cJSON_Delete(json_);
                        json_ = nullptr;
                    }
                }

                /**
                 * @brief 释放所有权并返回指针
                 * @return cJSON 指针，调用者负责删除
                 */
                cJSON* release()
                {
                    cJSON* result = json_;
                    json_         = nullptr;
                    return result;
                }

            private:
                cJSON* json_;
            };

            /**
             * @brief cJSON_Print 返回字符串的 RAII 包装器
             *
             * 自动管理 cJSON_Print 返回的字符串，使用 free 释放
             */
            class JsonStringRAII
            {
            public:
                /**
                 * @brief 构造函数
                 * @param json_str cJSON_Print 返回的字符串指针，可以为 nullptr
                 */
                explicit JsonStringRAII(char* json_str) : str_(json_str) {}

                /**
                 * @brief 禁止拷贝构造
                 */
                JsonStringRAII(const JsonStringRAII&) = delete;

                /**
                 * @brief 禁止拷贝赋值
                 */
                JsonStringRAII& operator=(const JsonStringRAII&) = delete;

                /**
                 * @brief 移动构造函数
                 */
                JsonStringRAII(JsonStringRAII&& other) noexcept : str_(other.str_)
                {
                    other.str_ = nullptr;
                }

                /**
                 * @brief 移动赋值运算符
                 */
                JsonStringRAII& operator=(JsonStringRAII&& other) noexcept
                {
                    if (this != &other)
                    {
                        reset();
                        str_       = other.str_;
                        other.str_ = nullptr;
                    }
                    return *this;
                }

                /**
                 * @brief 析构函数，自动释放字符串
                 */
                ~JsonStringRAII()
                {
                    reset();
                }

                /**
                 * @brief 获取字符串指针
                 * @return 字符串指针，如果未创建则返回 nullptr
                 */
                char* get() const
                {
                    return str_;
                }

                /**
                 * @brief 检查字符串是否有效
                 * @return true 如果有效，false 否则
                 */
                bool isValid() const
                {
                    return str_ != nullptr;
                }

                /**
                 * @brief 转换为 std::string
                 * @return std::string，如果字符串无效则返回空字符串
                 */
                std::string toString() const
                {
                    return str_ ? std::string(str_) : std::string();
                }

                /**
                 * @brief 重置字符串（释放当前字符串）
                 */
                void reset()
                {
                    if (str_ != nullptr)
                    {
                        free(str_);
                        str_ = nullptr;
                    }
                }

                /**
                 * @brief 释放所有权并返回指针
                 * @return 字符串指针，调用者负责释放
                 */
                char* release()
                {
                    char* result = str_;
                    str_         = nullptr;
                    return result;
                }

            private:
                char* str_;
            };

            /**
             * @brief mbedtls MD5 上下文 RAII 包装器
             *
             * 自动管理 mbedtls_md5_context 的生命周期
             */
            class Md5ContextRAII
            {
            public:
                /**
                 * @brief 构造函数，初始化 MD5 上下文
                 */
                Md5ContextRAII()
                {
                    mbedtls_md5_init(&ctx_);
                }

                /**
                 * @brief 禁止拷贝构造
                 */
                Md5ContextRAII(const Md5ContextRAII&) = delete;

                /**
                 * @brief 禁止拷贝赋值
                 */
                Md5ContextRAII& operator=(const Md5ContextRAII&) = delete;

                /**
                 * @brief 析构函数，自动释放 MD5 上下文
                 */
                ~Md5ContextRAII()
                {
                    mbedtls_md5_free(&ctx_);
                }

                /**
                 * @brief 获取 MD5 上下文引用
                 * @return MD5 上下文引用
                 */
                mbedtls_md5_context& get()
                {
                    return ctx_;
                }

                /**
                 * @brief 获取 MD5 上下文指针
                 * @return MD5 上下文指针
                 */
                mbedtls_md5_context* getPtr()
                {
                    return &ctx_;
                }

            private:
                mbedtls_md5_context ctx_;
            };

            enum class OtaStatus : uint8_t
            {
                IDLE        = 0,
                CHECKING    = 1,
                DOWNLOADING = 2,
                VERIFYING   = 3,
                COMPLETED   = 4,
                FAILED      = 5
            };

            struct FirmwareInfo
            {
                std::string version;
                std::string name;
                size_t      size;
                std::string info;
                std::string md5;
                std::string time;
            };

            using ProgressCallback =
                std::function<void(size_t received, size_t total, float percent)>;
            using StatusCallback   = std::function<void(OtaStatus status)>;
            using CompleteCallback = std::function<void(bool success, const std::string& error)>;

            class OtaManager
            {
            public:
                static OtaManager& getInstance();

                bool init(const std::string& device_id, const std::string& current_version);
                void deinit();

                bool checkUpdate(const std::string& server_url, int32_t timeout_ms = 10000);
                bool getFirmwareInfo(const std::string& server_url, FirmwareInfo& info,
                                     int32_t timeout_ms = 10000);
                bool startUpdate(const std::string& server_url, const FirmwareInfo& firmware_info,
                                 int32_t timeout_ms = 120000);
                bool reportStatus(const std::string& server_url, uint8_t status, uint8_t progress,
                                  int32_t timeout_ms = 5000);
                bool cancel();

                OtaStatus   getStatus() const;
                void        setProgressCallback(const ProgressCallback& callback);
                void        setStatusCallback(const StatusCallback& callback);
                void        setCompleteCallback(const CompleteCallback& callback);
                bool        isUpdating() const;
                std::string getCurrentVersion() const;

            private:
                OtaManager() = default;
                ~OtaManager()
                {
                    deinit();
                }
                OtaManager(const OtaManager&)            = delete;
                OtaManager& operator=(const OtaManager&) = delete;

                std::string getTimestamp() const;
                std::string buildCheckUpdateMessage() const;
                std::string buildGetFirmwareInfoMessage() const;
                std::string buildRequestFirmwareMessage(const FirmwareInfo& info) const;
                std::string buildReportStatusMessage(uint8_t status, uint8_t progress) const;
                bool        parseReplyUpdate(const std::string& json, int& respond,
                                             std::string& download_url);
                bool        parseFirmwareInfo(const std::string& json, FirmwareInfo& info);
                bool        parseError(const std::string& json, int& code, std::string& message);
                void        updateStatus(OtaStatus status);
                std::string buildUrl(const std::string& server_url, const std::string& path) const;
                void        handleError(OtaStatus new_status, const std::string& error_msg = "");
                std::string md5ToString(const unsigned char* md5_bytes) const;
                std::string buildBaseJsonMessage(const std::string& type) const;

                mutable std::mutex mutex_;
                bool               initialized_;
                std::string        device_id_;
                std::string        current_version_;
                OtaStatus          status_;
                ProgressCallback   progress_callback_;
                StatusCallback     status_callback_;
                CompleteCallback   complete_callback_;
                void*              current_update_task_;
                volatile bool      cancelled_;
            };

        } // namespace ota
    } // namespace tool
} // namespace app
