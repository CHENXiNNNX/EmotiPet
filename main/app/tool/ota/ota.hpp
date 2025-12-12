#pragma once

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>

namespace app
{
    namespace tool
    {
        namespace ota
        {

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
                OtaManager()                             = default;
                ~OtaManager()                            = default;
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
    }     // namespace tool
} // namespace app
