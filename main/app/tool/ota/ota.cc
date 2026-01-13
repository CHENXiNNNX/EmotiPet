#include "ota.hpp"

#include <algorithm>
#include <cstring>
#include <ctime>
#include <memory>
#include <mutex>

#include "protocol/http/http.hpp"
#include "protocol/ntp/ntp.hpp"
#include "tool/time/time.hpp"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "mbedtls/md5.h"
#include "system/task/task.hpp"

static const char* const TAG = "OTA";

namespace app
{
    namespace tool
    {
        namespace ota
        {

            OtaManager& OtaManager::getInstance()
            {
                static OtaManager instance;
                return instance;
            }

            bool OtaManager::init(const std::string& device_id, const std::string& current_version)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (initialized_)
                {
                    return true;
                }

                device_id_           = device_id;
                current_version_     = current_version;
                status_              = OtaStatus::IDLE;
                progress_callback_   = nullptr;
                status_callback_     = nullptr;
                complete_callback_   = nullptr;
                current_update_task_ = nullptr;
                cancelled_           = false;
                initialized_         = true;
                return true;
            }

            void OtaManager::deinit()
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (!initialized_)
                {
                    return;
                }

                if (status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING ||
                    status_ == OtaStatus::CHECKING)
                {
                    ESP_LOGW(TAG, "OTA 升级进行中，无法去初始化");
                    return;
                }

                initialized_         = false;
                status_              = OtaStatus::IDLE;
                progress_callback_   = nullptr;
                status_callback_     = nullptr;
                complete_callback_   = nullptr;
                current_update_task_ = nullptr;
                cancelled_           = false;
            }

            std::string OtaManager::getTimestamp() const
            {
                auto& ntp_mgr = app::protocol::ntp::NTPManager::getInstance();
                if (ntp_mgr.isInitialized() &&
                    ntp_mgr.getSyncStatus() != app::protocol::ntp::SyncStatus::COMPLETED)
                {
                    ESP_LOGW(TAG, "NTP 未同步，使用系统时间");
                }

                return app::tool::time::iso8601Timestamp();
            }

            std::string OtaManager::buildUrl(const std::string& server_url,
                                             const std::string& path) const
            {
                std::string url = server_url;
                if (!url.empty() && url.back() != '/')
                {
                    url += '/';
                }
                if (!path.empty() && path.front() == '/')
                {
                    url += path.substr(1);
                }
                else
                {
                    url += path;
                }
                return url;
            }

            void OtaManager::handleError(OtaStatus new_status, const std::string& error_msg)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_ = new_status;
                }
                if (status_callback_)
                {
                    status_callback_(new_status);
                }
                if (!error_msg.empty())
                {
                    ESP_LOGE(TAG, "%s", error_msg.c_str());
                }
            }

            std::string OtaManager::md5ToString(const unsigned char* md5_bytes) const
            {
                char md5_str[33];
                for (int i = 0; i < 16; i++)
                {
                    snprintf(md5_str + i * 2, 3, "%02x", md5_bytes[i]);
                }
                md5_str[32] = '\0';
                return std::string(md5_str);
            }

            std::string OtaManager::buildBaseJsonMessage(const std::string& type) const
            {
                JsonRAII json;
                if (!json.isValid())
                {
                    return "";
                }

                cJSON_AddStringToObject(json.get(), "type", type.c_str());
                cJSON_AddStringToObject(json.get(), "from", device_id_.c_str());
                cJSON_AddStringToObject(json.get(), "to", "ota_server");
                cJSON_AddStringToObject(json.get(), "timestamp", getTimestamp().c_str());

                JsonStringRAII json_str(cJSON_Print(json.get()));
                return json_str.isValid() ? json_str.toString() : std::string();
            }

            // ==================== JSON 消息构建 ====================

            std::string OtaManager::buildCheckUpdateMessage() const
            {
                std::string base_json = buildBaseJsonMessage("check_update");
                JsonRAII    json(base_json.c_str());
                if (!json.isValid())
                {
                    // 回退方案：创建新的 JSON 对象
                    JsonRAII fallback_json;
                    if (!fallback_json.isValid())
                    {
                        return "";
                    }
                    cJSON_AddStringToObject(fallback_json.get(), "type", "check_update");
                    cJSON_AddStringToObject(fallback_json.get(), "from", device_id_.c_str());
                    cJSON_AddStringToObject(fallback_json.get(), "to", "ota_server");
                    cJSON_AddStringToObject(fallback_json.get(), "current_version",
                                            current_version_.c_str());
                    cJSON_AddStringToObject(fallback_json.get(), "timestamp",
                                            getTimestamp().c_str());

                    JsonStringRAII json_str(cJSON_Print(fallback_json.get()));
                    return json_str.isValid() ? json_str.toString() : std::string();
                }

                cJSON_AddStringToObject(json.get(), "current_version", current_version_.c_str());

                JsonStringRAII json_str(cJSON_Print(json.get()));
                return json_str.isValid() ? json_str.toString() : std::string();
            }

            std::string OtaManager::buildGetFirmwareInfoMessage() const
            {
                return buildBaseJsonMessage("get_firmware_info");
            }

            std::string OtaManager::buildRequestFirmwareMessage(const FirmwareInfo& info) const
            {
                std::string base_json = buildBaseJsonMessage("request_firmware");
                JsonRAII    json(base_json.c_str());
                if (!json.isValid())
                {
                    // 回退方案：创建新的 JSON 对象
                    JsonRAII fallback_json;
                    if (!fallback_json.isValid())
                    {
                        return "";
                    }
                    cJSON_AddStringToObject(fallback_json.get(), "type", "request_firmware");
                    cJSON_AddStringToObject(fallback_json.get(), "from", device_id_.c_str());
                    cJSON_AddStringToObject(fallback_json.get(), "to", "ota_server");
                    cJSON_AddStringToObject(fallback_json.get(), "timestamp",
                                            getTimestamp().c_str());

                    JsonRAII data;
                    if (data.isValid())
                    {
                        cJSON_AddStringToObject(data.get(), "name", info.name.c_str());
                        cJSON_AddStringToObject(data.get(), "target_version", info.version.c_str());
                        cJSON_AddStringToObject(data.get(), "md5", info.md5.c_str());
                        cJSON_AddItemToObject(fallback_json.get(), "data", data.release());
                    }

                    JsonStringRAII json_str(cJSON_Print(fallback_json.get()));
                    return json_str.isValid() ? json_str.toString() : std::string();
                }

                JsonRAII data;
                if (data.isValid())
                {
                    cJSON_AddStringToObject(data.get(), "name", info.name.c_str());
                    cJSON_AddStringToObject(data.get(), "target_version", info.version.c_str());
                    cJSON_AddStringToObject(data.get(), "md5", info.md5.c_str());
                    cJSON_AddItemToObject(json.get(), "data", data.release());
                }

                JsonStringRAII json_str(cJSON_Print(json.get()));
                return json_str.isValid() ? json_str.toString() : std::string();
            }

            std::string OtaManager::buildReportStatusMessage(uint8_t status, uint8_t progress) const
            {
                std::string base_json = buildBaseJsonMessage("report_status");
                JsonRAII    json(base_json.c_str());
                if (!json.isValid())
                {
                    // 回退方案：创建新的 JSON 对象
                    JsonRAII fallback_json;
                    if (!fallback_json.isValid())
                    {
                        return "";
                    }
                    cJSON_AddStringToObject(fallback_json.get(), "type", "report_status");
                    cJSON_AddStringToObject(fallback_json.get(), "from", device_id_.c_str());
                    cJSON_AddStringToObject(fallback_json.get(), "to", "ota_server");
                    cJSON_AddStringToObject(fallback_json.get(), "timestamp",
                                            getTimestamp().c_str());

                    JsonRAII data;
                    if (data.isValid())
                    {
                        cJSON_AddNumberToObject(data.get(), "status", status);
                        cJSON_AddNumberToObject(data.get(), "progress", progress);
                        cJSON_AddStringToObject(data.get(), "current_version",
                                                current_version_.c_str());
                        cJSON_AddItemToObject(fallback_json.get(), "data", data.release());
                    }

                    JsonStringRAII json_str(cJSON_Print(fallback_json.get()));
                    return json_str.isValid() ? json_str.toString() : std::string();
                }

                JsonRAII data;
                if (data.isValid())
                {
                    cJSON_AddNumberToObject(data.get(), "status", status);
                    cJSON_AddNumberToObject(data.get(), "progress", progress);
                    cJSON_AddStringToObject(data.get(), "current_version",
                                            current_version_.c_str());
                    cJSON_AddItemToObject(json.get(), "data", data.release());
                }

                JsonStringRAII json_str(cJSON_Print(json.get()));
                return json_str.isValid() ? json_str.toString() : std::string();
            }

            bool OtaManager::parseReplyUpdate(const std::string& json, int& respond,
                                              std::string& download_url)
            {
                JsonRAII root(json.c_str());
                if (!root.isValid())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                cJSON* respond_item = cJSON_GetObjectItem(root.get(), "respond");
                if (cJSON_IsNumber(respond_item))
                {
                    respond = respond_item->valueint;
                }
                else
                {
                    return false;
                }

                cJSON* url_item = cJSON_GetObjectItem(root.get(), "download_url");
                if (cJSON_IsString(url_item))
                {
                    download_url = url_item->valuestring;
                }

                return true;
            }

            bool OtaManager::parseFirmwareInfo(const std::string& json, FirmwareInfo& info)
            {
                JsonRAII root(json.c_str());
                if (!root.isValid())
                {
                    ESP_LOGE(TAG, "JSON 解析失败: %s", cJSON_GetErrorPtr());
                    return false;
                }

                cJSON* file_item = cJSON_GetObjectItem(root.get(), "file");
                if (!file_item || !cJSON_IsObject(file_item))
                {
                    return false;
                }

                cJSON* version_item = cJSON_GetObjectItem(file_item, "version");
                if (cJSON_IsString(version_item))
                {
                    info.version = version_item->valuestring;
                }

                cJSON* name_item = cJSON_GetObjectItem(file_item, "name");
                if (cJSON_IsString(name_item))
                {
                    info.name = name_item->valuestring;
                }

                cJSON* size_item = cJSON_GetObjectItem(file_item, "size");
                if (cJSON_IsNumber(size_item))
                {
                    info.size = (size_t)size_item->valueint;
                }

                cJSON* info_item = cJSON_GetObjectItem(file_item, "info");
                if (cJSON_IsString(info_item))
                {
                    info.info = info_item->valuestring;
                }

                cJSON* md5_item = cJSON_GetObjectItem(file_item, "md5");
                if (cJSON_IsString(md5_item))
                {
                    info.md5 = md5_item->valuestring;
                }

                cJSON* time_item = cJSON_GetObjectItem(file_item, "time");
                if (cJSON_IsString(time_item))
                {
                    info.time = time_item->valuestring;
                }

                return true;
            }

            bool OtaManager::parseError(const std::string& json, int& code, std::string& message)
            {
                JsonRAII root(json.c_str());
                if (!root.isValid())
                {
                    return false;
                }

                // 检查是否是错误响应类型
                cJSON* type_item = cJSON_GetObjectItem(root.get(), "type");
                if (!type_item || !cJSON_IsString(type_item) ||
                    strcmp(type_item->valuestring, "error") != 0)
                {
                    return false;
                }

                // 解析 data 字段
                cJSON* data_item = cJSON_GetObjectItem(root.get(), "data");
                if (!data_item || !cJSON_IsObject(data_item))
                {
                    return false;
                }

                cJSON* code_item = cJSON_GetObjectItem(data_item, "code");
                if (cJSON_IsNumber(code_item))
                {
                    code = code_item->valueint;
                }

                cJSON* message_item = cJSON_GetObjectItem(data_item, "message");
                if (cJSON_IsString(message_item))
                {
                    message = message_item->valuestring;
                }

                return true;
            }

            bool OtaManager::checkUpdate(const std::string& server_url, int32_t timeout_ms)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if (!initialized_)
                    {
                        ESP_LOGE(TAG, "OTA 管理器未初始化");
                        return false;
                    }

                    if (status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING)
                    {
                        ESP_LOGE(TAG, "OTA 升级进行中，无法检查更新");
                        return false;
                    }

                    status_ = OtaStatus::CHECKING;
                }

                if (status_callback_)
                {
                    status_callback_(OtaStatus::CHECKING);
                }

                auto&       http_client  = app::protocol::http::HttpClient::getInstance();
                std::string url          = buildUrl(server_url, "api/ota/check");
                std::string request_body = buildCheckUpdateMessage();

                app::protocol::http::HttpResponse response;
                bool http_result = http_client.post(url, request_body, response, timeout_ms);

                if (!http_result)
                {
                    handleError(OtaStatus::FAILED, "检查更新失败：HTTP 请求失败");
                    return false;
                }

                if (response.status_code != app::protocol::http::HttpStatus::OK)
                {
                    char error_buf[64];
                    snprintf(error_buf, sizeof(error_buf), "检查更新失败：HTTP 状态码 %ld",
                             (long)response.status_code_int);
                    handleError(OtaStatus::FAILED, error_buf);
                    return false;
                }

                // 解析响应
                std::string body_str(response.body.begin(), response.body.end());

                // 检查是否是错误响应
                int         error_code;
                std::string error_message;
                if (parseError(body_str, error_code, error_message))
                {
                    char error_buf[128];
                    snprintf(error_buf, sizeof(error_buf), "检查更新失败：%s (错误码: %d)",
                             error_message.c_str(), error_code);
                    handleError(OtaStatus::FAILED, error_buf);
                    return false;
                }

                int         respond;
                std::string download_url;
                if (!parseReplyUpdate(body_str, respond, download_url))
                {
                    handleError(OtaStatus::FAILED, "检查更新失败：无法解析服务器响应");
                    return false;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_ = OtaStatus::IDLE;
                }
                if (status_callback_)
                {
                    status_callback_(OtaStatus::IDLE);
                }
                return respond == 1 || respond == 2;
            }

            bool OtaManager::getFirmwareInfo(const std::string& server_url, FirmwareInfo& info,
                                             int32_t timeout_ms)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (!initialized_)
                    {
                        ESP_LOGE(TAG, "OTA 管理器未初始化");
                        return false;
                    }
                }

                auto& http_client = app::protocol::http::HttpClient::getInstance();

                // 构建 URL
                std::string url = buildUrl(server_url, "api/ota/info");

                // 构建请求消息
                std::string request_body = buildGetFirmwareInfoMessage();

                // 发送 HTTP POST 请求（在锁外执行，避免死锁）
                app::protocol::http::HttpResponse response;
                if (!http_client.post(url, request_body, response, timeout_ms))
                {
                    ESP_LOGE(TAG, "获取固件信息失败：HTTP 请求失败");
                    return false;
                }

                if (response.status_code != app::protocol::http::HttpStatus::OK)
                {
                    ESP_LOGE(TAG, "获取固件信息失败：HTTP 状态码 %ld",
                             (long)response.status_code_int);
                    return false;
                }

                std::string body_str(response.body.begin(), response.body.end());

                int         error_code;
                std::string error_message;
                if (parseError(body_str, error_code, error_message))
                {
                    ESP_LOGE(TAG, "获取固件信息失败：%s (错误码: %d)", error_message.c_str(),
                             error_code);
                    return false;
                }

                if (!parseFirmwareInfo(body_str, info))
                {
                    ESP_LOGE(TAG, "获取固件信息失败：无法解析服务器响应");
                    return false;
                }

                return true;
            }

            bool OtaManager::startUpdate(const std::string&  server_url,
                                         const FirmwareInfo& firmware_info, int32_t timeout_ms)
            {
                ESP_LOGI(TAG, "开始启动 OTA 升级，固件版本: %s", firmware_info.version.c_str());

                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (!initialized_)
                    {
                        ESP_LOGE(TAG, "OTA 管理器未初始化");
                        return false;
                    }

                    if (status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING)
                    {
                        ESP_LOGE(TAG, "OTA 升级已在进行中");
                        return false;
                    }
                }

                // 在任务中执行升级
                auto task_config       = app::sys::task::Config();
                task_config.name       = "ota_update";
                task_config.stack_size = 16384;
                task_config.priority   = app::sys::task::Priority::HIGH;
                task_config.core_id    = -1;

                struct UpdateContext
                {
                    std::string      server_url;
                    FirmwareInfo     firmware_info;
                    int32_t          timeout_ms;
                    OtaManager*      manager;
                    ProgressCallback progress_callback;
                    CompleteCallback complete_callback;

                    UpdateContext(const std::string& url, const FirmwareInfo& info, int32_t timeout,
                                  OtaManager* mgr, const ProgressCallback& prog_cb,
                                  const CompleteCallback& comp_cb)
                        : server_url(url), firmware_info(info), timeout_ms(timeout), manager(mgr),
                          progress_callback(prog_cb), complete_callback(comp_cb)
                    {
                    }
                };

                // 使用智能指针管理上下文，避免内存泄漏
                auto ctx =
                    std::make_shared<UpdateContext>(server_url, firmware_info, timeout_ms, this,
                                                    progress_callback_, complete_callback_);

                // 使用 unique_ptr 管理 shared_ptr 指针，传递给任务函数
                auto ctx_ptr = std::make_unique<std::shared_ptr<UpdateContext>>(ctx);

                app::sys::task::Task::TaskFunction task_function = [](void* param)
                {
                    // 使用 unique_ptr 自动管理 shared_ptr 指针
                    std::unique_ptr<std::shared_ptr<UpdateContext>> ctx_owner(
                        static_cast<std::shared_ptr<UpdateContext>*>(param));
                    auto  ctx     = *ctx_owner;
                    auto& manager = *ctx->manager;

                    // 延迟一小段时间，确保已释放锁
                    app::sys::task::TaskManager::delayMs(10);

                    {
                        std::lock_guard<std::mutex> lock(manager.mutex_);
                        auto&        task_mgr    = app::sys::task::TaskManager::getInstance();
                        TaskHandle_t task_handle = task_mgr.findTask("ota_update");
                        if (manager.current_update_task_ == nullptr)
                        {
                            manager.current_update_task_ = task_handle;
                        }
                        manager.cancelled_ = false;
                    }

                    manager.updateStatus(OtaStatus::DOWNLOADING);

                    // 获取 OTA 分区
                    const esp_partition_t* update_partition = nullptr;
                    esp_ota_handle_t       ota_handle       = 0;

                    // 获取当前运行的分区
                    const esp_partition_t* running = esp_ota_get_running_partition();
                    esp_ota_img_states_t   ota_state;
                    if (esp_ota_get_state_partition(running, &ota_state) == ESP_OK)
                    {
                        if (ota_state == ESP_OTA_IMG_PENDING_VERIFY)
                        {
                            ESP_LOGW(TAG, "检测到待验证的 OTA 镜像，标记为有效");
                            esp_ota_mark_app_valid_cancel_rollback();
                        }
                    }

                    // 查找 OTA 分区
                    if (running->subtype == ESP_PARTITION_SUBTYPE_APP_OTA_0)
                    {
                        update_partition = esp_partition_find_first(
                            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_1, nullptr);
                    }
                    else
                    {
                        update_partition = esp_partition_find_first(
                            ESP_PARTITION_TYPE_APP, ESP_PARTITION_SUBTYPE_APP_OTA_0, nullptr);
                    }

                    if (update_partition == nullptr)
                    {
                        ESP_LOGE(TAG, "未找到 OTA 分区");
                        manager.updateStatus(OtaStatus::FAILED);
                        if (ctx->complete_callback)
                        {
                            ctx->complete_callback(false, "未找到 OTA 分区");
                        }
                        {
                            std::lock_guard<std::mutex> lock(manager.mutex_);
                            manager.current_update_task_ = nullptr;
                        }
                        // ctx_owner会在函数返回时自动释放shared_ptr
                        return;
                    }

                    ESP_LOGI(TAG, "使用 OTA 分区：%s, 偏移: 0x%08x, 大小: %u",
                             update_partition->label, (unsigned int)update_partition->address,
                             (unsigned int)update_partition->size);

                    // 开始 OTA 写入
                    esp_err_t err = esp_ota_begin(update_partition, OTA_SIZE_UNKNOWN, &ota_handle);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_begin 失败: %s", esp_err_to_name(err));
                        manager.updateStatus(OtaStatus::FAILED);
                        if (ctx->complete_callback)
                        {
                            char error_buf[128];
                            snprintf(error_buf, sizeof(error_buf), "esp_ota_begin 失败: %s",
                                     esp_err_to_name(err));
                            ctx->complete_callback(false, error_buf);
                        }
                        {
                            std::lock_guard<std::mutex> lock(manager.mutex_);
                            manager.current_update_task_ = nullptr;
                        }
                        // ctx_owner会在函数返回时自动释放shared_ptr
                        return;
                    }

                    // 构建下载 URL
                    std::string url =
                        manager.buildUrl(ctx->server_url, "firmware/" + ctx->firmware_info.name);

                    // 配置 HTTP 请求
                    app::protocol::http::HttpRequest request;
                    request.url        = url;
                    request.method     = app::protocol::http::HttpMethod::GET;
                    request.timeout_ms = ctx->timeout_ms;

                    // MD5 计算（使用 RAII 自动管理）
                    Md5ContextRAII md5_ctx;
                    mbedtls_md5_starts(&md5_ctx.get());

                    size_t total_received = 0;
                    size_t total_size     = ctx->firmware_info.size;
                    bool   download_ok    = false;

                    // 执行 HTTP 下载（流式）
                    auto& http_client = app::protocol::http::HttpClient::getInstance();

                    bool http_success = http_client.perform(
                        request,
                        [&total_size](const app::protocol::http::HttpResponse& resp)
                        {
                            if (resp.content_length > 0)
                            {
                                total_size = resp.content_length;
                            }
                            return resp.status_code == app::protocol::http::HttpStatus::OK;
                        },
                        [ctx, &ota_handle, &md5_ctx, &total_received, &total_size,
                         &download_ok](const uint8_t* data, size_t len) -> bool
                        {
                            // 检查是否已取消（通过manager的cancelled_标志）
                            {
                                std::lock_guard<std::mutex> lock(ctx->manager->mutex_);
                                if (ctx->manager->cancelled_)
                                {
                                    ESP_LOGW(TAG, "下载已取消");
                                    download_ok = false;
                                    return false;
                                }
                            }

                            esp_err_t err = esp_ota_write(ota_handle, data, len);
                            if (err != ESP_OK)
                            {
                                ESP_LOGE(TAG, "esp_ota_write 失败: %s", esp_err_to_name(err));
                                download_ok = false;
                                return false;
                            }

                            mbedtls_md5_update(&md5_ctx.get(), data, len);
                            total_received += len;
                            float percent = 0.0f;
                            if (total_size > 0)
                            {
                                percent = (float)total_received * 100.0f / (float)total_size;
                            }

                            if (ctx->progress_callback)
                            {
                                ctx->progress_callback(total_received, total_size, percent);
                            }

                            download_ok = true;
                            return true;
                        });

                    // 检查是否已取消
                    bool was_cancelled = false;
                    {
                        std::lock_guard<std::mutex> lock(manager.mutex_);
                        was_cancelled = manager.cancelled_;
                    }

                    if (!http_success || !download_ok)
                    {
                        const char* error_msg = was_cancelled ? "下载已取消" : "下载失败";
                        ESP_LOGE(TAG, "%s", error_msg);
                        esp_ota_abort(ota_handle);
                        // md5_ctx 会在作用域结束时自动释放（RAII）
                        manager.updateStatus(OtaStatus::FAILED);
                        if (ctx->complete_callback)
                        {
                            ctx->complete_callback(false, error_msg);
                        }
                        {
                            std::lock_guard<std::mutex> lock(manager.mutex_);
                            manager.current_update_task_ = nullptr;
                        }
                        // ctx_owner会在函数返回时自动释放shared_ptr
                        return;
                    }

                    unsigned char md5_result[16];
                    mbedtls_md5_finish(&md5_ctx.get(), md5_result);
                    // md5_ctx 会在作用域结束时自动释放（RAII）

                    std::string md5_str = manager.md5ToString(md5_result);
                    manager.updateStatus(OtaStatus::VERIFYING);

                    std::string expected_md5 = ctx->firmware_info.md5;
                    std::string actual_md5   = md5_str;
                    std::transform(expected_md5.begin(), expected_md5.end(), expected_md5.begin(),
                                   ::tolower);
                    std::transform(actual_md5.begin(), actual_md5.end(), actual_md5.begin(),
                                   ::tolower);

                    if (expected_md5 != actual_md5)
                    {
                        ESP_LOGE(TAG, "MD5 校验失败：期望=%s, 实际=%s",
                                 ctx->firmware_info.md5.c_str(), md5_str.c_str());
                        esp_ota_abort(ota_handle);
                        manager.updateStatus(OtaStatus::FAILED);
                        if (ctx->complete_callback)
                        {
                            ctx->complete_callback(false, "MD5 校验失败");
                        }
                        {
                            std::lock_guard<std::mutex> lock(manager.mutex_);
                            manager.current_update_task_ = nullptr;
                        }
                        return;
                    }

                    err = esp_ota_end(ota_handle);
                    if (err != ESP_OK)
                    {
                        const char* error_msg = (err == ESP_ERR_OTA_VALIDATE_FAILED)
                                                    ? "OTA 镜像验证失败"
                                                    : "esp_ota_end 失败";
                        ESP_LOGE(TAG, "%s: %s", error_msg, esp_err_to_name(err));
                        manager.updateStatus(OtaStatus::FAILED);
                        if (ctx->complete_callback)
                        {
                            char error_buf[128];
                            snprintf(error_buf, sizeof(error_buf), "%s: %s", error_msg,
                                     esp_err_to_name(err));
                            ctx->complete_callback(false, error_buf);
                        }
                        {
                            std::lock_guard<std::mutex> lock(manager.mutex_);
                            manager.current_update_task_ = nullptr;
                        }
                        return;
                    }

                    err = esp_ota_set_boot_partition(update_partition);
                    if (err != ESP_OK)
                    {
                        ESP_LOGE(TAG, "esp_ota_set_boot_partition 失败: %s", esp_err_to_name(err));
                        manager.updateStatus(OtaStatus::FAILED);
                        if (ctx->complete_callback)
                        {
                            char error_buf[128];
                            snprintf(error_buf, sizeof(error_buf), "设置引导分区失败: %s",
                                     esp_err_to_name(err));
                            ctx->complete_callback(false, error_buf);
                        }
                        {
                            std::lock_guard<std::mutex> lock(manager.mutex_);
                            manager.current_update_task_ = nullptr;
                        }
                        return;
                    }

                    manager.updateStatus(OtaStatus::COMPLETED);
                    if (ctx->complete_callback)
                    {
                        ctx->complete_callback(true, "");
                    }

                    {
                        std::lock_guard<std::mutex> lock(manager.mutex_);
                        manager.current_update_task_ = nullptr;
                    }

                    // 延迟后重启
                    app::sys::task::TaskManager::delayMs(1000);
                    esp_restart();

                    // ctx_owner会在函数返回时自动释放shared_ptr
                };

                // 释放 ctx_ptr 的所有权，传递给任务（任务函数会负责释放）
                void* task_param = ctx_ptr.release();
                auto  task =
                    std::make_unique<app::sys::task::Task>(task_function, task_config, task_param);

                if (!task->start())
                {
                    ESP_LOGE(TAG, "启动 OTA 升级任务失败");
                    handleError(OtaStatus::FAILED, "启动 OTA 升级任务失败");
                    // 如果启动失败，需要手动删除传递的指针
                    delete static_cast<std::shared_ptr<UpdateContext>*>(task_param);
                    return false;
                }

                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    current_update_task_ = task->getHandle();
                }

                return true;
            }

            bool OtaManager::reportStatus(const std::string& server_url, uint8_t status,
                                          uint8_t progress, int32_t timeout_ms)
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (!initialized_)
                    {
                        return false;
                    }
                }

                auto& http_client = app::protocol::http::HttpClient::getInstance();

                // 构建 URL
                std::string url = buildUrl(server_url, "api/ota/status");

                // 构建请求消息
                std::string request_body = buildReportStatusMessage(status, progress);

                // 发送 HTTP POST 请求（异步，不等待响应）
                app::protocol::http::HttpResponse response;
                http_client.post(url, request_body, response, timeout_ms);

                return true;
            }

            bool OtaManager::cancel()
            {
                {
                    std::lock_guard<std::mutex> lock(mutex_);

                    if (status_ != OtaStatus::DOWNLOADING && status_ != OtaStatus::VERIFYING)
                    {
                        ESP_LOGW(TAG, "当前没有进行中的 OTA 升级");
                        return false;
                    }

                    // 设置取消标志，任务会在下次检查时退出
                    cancelled_ = true;
                    status_    = OtaStatus::IDLE;
                }

                if (status_callback_)
                {
                    status_callback_(OtaStatus::IDLE);
                }

                return true;
            }

            OtaStatus OtaManager::getStatus() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return status_;
            }

            bool OtaManager::isUpdating() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return status_ == OtaStatus::DOWNLOADING || status_ == OtaStatus::VERIFYING ||
                       status_ == OtaStatus::CHECKING;
            }

            std::string OtaManager::getCurrentVersion() const
            {
                std::lock_guard<std::mutex> lock(mutex_);
                return current_version_;
            }

            // ==================== 回调设置 ====================

            void OtaManager::setProgressCallback(const ProgressCallback& callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                progress_callback_ = callback;
            }

            void OtaManager::setStatusCallback(const StatusCallback& callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status_callback_ = callback;
            }

            void OtaManager::setCompleteCallback(const CompleteCallback& callback)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                complete_callback_ = callback;
            }

            // ==================== 内部方法 ====================

            void OtaManager::updateStatus(OtaStatus status)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                status_ = status;

                if (status_callback_)
                {
                    status_callback_(status);
                }
            }

        } // namespace ota
    } // namespace tool
} // namespace app
