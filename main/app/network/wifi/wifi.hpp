#pragma once

#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

#include "event.hpp"
#include "system/task/task.hpp"

extern "C"
{
    typedef const char* esp_event_base_t;
}

namespace app
{
    namespace network
    {
        namespace wifi
        {

            enum class State
            {
                DISCONNECTED,
                CONNECTING,
                CONNECTED,
                FAILED
            };

            enum class FailureReason
            {
                NONE,
                TIMEOUT,
                WRONG_PASSWORD,
                NETWORK_NOT_FOUND,
                CONNECTION_FAILED,
                UNKNOWN
            };

            struct Credentials
            {
                char ssid[32];
                char password[64];

                Credentials()
                {
                    ssid[0]     = '\0';
                    password[0] = '\0';
                }

                Credentials(const char* ssid_str, const char* pass_str)
                {
                    setSsid(ssid_str);
                    setPassword(pass_str);
                }

                void setSsid(const char* ssid_str)
                {
                    if (ssid_str)
                    {
                        strncpy(ssid, ssid_str, sizeof(ssid) - 1);
                        ssid[sizeof(ssid) - 1] = '\0';
                    }
                    else
                    {
                        ssid[0] = '\0';
                    }
                }

                void setPassword(const char* pass_str)
                {
                    if (pass_str)
                    {
                        strncpy(password, pass_str, sizeof(password) - 1);
                        password[sizeof(password) - 1] = '\0';
                    }
                    else
                    {
                        password[0] = '\0';
                    }
                }

                bool isValid() const
                {
                    return ssid[0] != '\0';
                }
            };

            struct ApInfo
            {
                char    ssid[32];
                uint8_t bssid[6];
                int8_t  rssi;
                uint8_t authmode;
                bool    is_encrypted;

                ApInfo() : rssi(0), authmode(0), is_encrypted(false)
                {
                    ssid[0] = '\0';
                    memset(bssid, 0, sizeof(bssid));
                }
            };

            struct Info
            {
                State         state;
                FailureReason failure_reason;
                char          ssid[32];
                uint8_t       ip[4];
                int8_t        rssi;

                Info() : state(State::DISCONNECTED), failure_reason(FailureReason::NONE), rssi(0)
                {
                    ssid[0] = '\0';
                    ip[0]   = 0;
                    ip[1]   = 0;
                    ip[2]   = 0;
                    ip[3]   = 0;
                }
            };

            using StateCallback = std::function<void(State state, FailureReason reason)>;
            using ScanCallback  = std::function<void(const std::vector<ApInfo>& aps)>;

            class WiFiManager
            {
            public:
                static WiFiManager& getInstance();

                bool init();

                void deinit();

                bool scan(ScanCallback callback = nullptr);

                bool connect(const char* ssid = nullptr, const char* password = nullptr,
                             uint32_t timeout_ms = 30000);

                void disconnect();

                const Info& getInfo() const;

                State getState() const;

                bool isConnected() const;

                void setStateCallback(StateCallback callback);

                bool saveCredentials(const Credentials& credentials);

                bool loadCredentials(Credentials& credentials);

                bool clearCredentials();

                bool hasSavedCredentials();

            private:
                WiFiManager();
                ~WiFiManager()                             = default;
                WiFiManager(const WiFiManager&)            = delete;
                WiFiManager& operator=(const WiFiManager&) = delete;

                void handleWiFiEvent(esp_event_base_t event_base, app::sys::event::EventId event_id,
                                     const app::sys::event::EventData& event_data);
                void handleIPEvent(esp_event_base_t event_base, app::sys::event::EventId event_id,
                                   const app::sys::event::EventData& event_data);

                void updateState(State state, FailureReason reason = FailureReason::NONE);

                void clearTimeoutTask();

                void checkTimeout();

                static void timeoutTaskWrapper(void* param);

                mutable std::mutex                    mutex_;
                bool                                  initialized_;
                Info                                  info_;
                StateCallback                         state_callback_;
                ScanCallback                          scan_callback_;
                uint32_t                              connect_timeout_ms_;
                bool                                  connect_timeout_set_;
                uint32_t                              connect_start_tick_;
                bool                                  disconnecting_;
                bool                                  scanning_;
                std::unique_ptr<app::sys::task::Task> timeout_task_;
            };

        } // namespace wifi
    }     // namespace network
} // namespace app
