#include "app.hpp"

#include "config/config.hpp"
#include "system/info/info.hpp"
#include "network/network.hpp"
#include "network/wifi/wifi.hpp"
#include "protocol/ntp/ntp.hpp"
#include "chatbot/message/message.hpp"
#include "media/audio/capture/capture.hpp"
#include "media/audio/wakeword/wakeword.hpp"
#include "media/camera/camera.hpp"
#include "media/camera/process/jpeg/encode/jpeg_enc.hpp"
#include "assets/assets.hpp"
#include "logic/logic.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "system/task/task.hpp"
#include <sstream>
#include <vector>

static const char* const TAG = "App";

namespace app
{
    // ==================== 公共方法 ====================
    bool App::setup()
    {
        if (!initNVS())
        {
            ESP_LOGE(TAG, "NVS 初始化失败");
            return false;
        }

        if (!initI2C(app::config::I2C_SDA, app::config::I2C_SCL, I2C_NUM_1))
        {
            ESP_LOGE(TAG, "I2C 初始化失败");
            return false;
        }

        if (!initAssets())
        {
            ESP_LOGE(TAG, "Assets 初始化失败");
            return false;
        }

        if (!initEvent())
        {
            ESP_LOGE(TAG, "事件系统初始化失败");
            return false;
        }

        return true;
    }

    void App::run()
    {
        while (true)
        {
            switch (current_state_)
            {
            case DeviceState::INIT:
                handleInitState();
                break;

            case DeviceState::PROVISIONING:
                handleProvisioningState();
                break;

            case DeviceState::NTP_SYNC:
                handleNtpSyncState();
                break;

            case DeviceState::CONNECTING:
                handleConnectingState();
                break;

            case DeviceState::WAKEWORD_WAIT:
                handleWakewordWaitState();
                break;

            case DeviceState::RUNNING:
                handleRunningState();
                break;

            case DeviceState::ERROR:
                handleErrorState();
                break;

            case DeviceState::RECOVERY:
                handleRecoveryState();
                break;

            default:
                ESP_LOGE(TAG, "未知状态: %d", static_cast<int>(current_state_));
                setState(DeviceState::ERROR);
                break;
            }

            // 防止忙等，给其他任务运行机会
            sys::task::TaskManager::delayMs(100);
        }
    }

    // 获取I2C总线句柄
    i2c_master_bus_handle_t App::getI2CBusHandle() const
    {
        return i2c_.getBusHandle();
    }

    // ==================== 状态管理 ====================

    void App::setState(DeviceState new_state)
    {
        if (current_state_ == new_state)
        {
            return; // 状态未改变
        }

        // 记录状态转换
        const char* old_state_name = "UNKNOWN";
        const char* new_state_name = "UNKNOWN";

        // 获取旧状态名称
        switch (current_state_)
        {
        case DeviceState::INIT:
            old_state_name = "INIT";
            break;
        case DeviceState::PROVISIONING:
            old_state_name = "PROVISIONING";
            break;
        case DeviceState::NTP_SYNC:
            old_state_name = "NTP_SYNC";
            break;
        case DeviceState::CONNECTING:
            old_state_name = "CONNECTING";
            break;
        case DeviceState::WAKEWORD_WAIT:
            old_state_name = "WAKEWORD_WAIT";
            break;
        case DeviceState::RUNNING:
            old_state_name = "RUNNING";
            break;
        case DeviceState::ERROR:
            old_state_name = "ERROR";
            break;
        case DeviceState::RECOVERY:
            old_state_name = "RECOVERY";
            break;
        }

        // 获取新状态名称
        switch (new_state)
        {
        case DeviceState::INIT:
            new_state_name = "INIT";
            break;
        case DeviceState::PROVISIONING:
            new_state_name = "PROVISIONING";
            break;
        case DeviceState::NTP_SYNC:
            new_state_name = "NTP_SYNC";
            break;
        case DeviceState::CONNECTING:
            new_state_name = "CONNECTING";
            break;
        case DeviceState::WAKEWORD_WAIT:
            new_state_name = "WAKEWORD_WAIT";
            break;
        case DeviceState::RUNNING:
            new_state_name = "RUNNING";
            break;
        case DeviceState::ERROR:
            new_state_name = "ERROR";
            break;
        case DeviceState::RECOVERY:
            new_state_name = "RECOVERY";
            break;
        }

        ESP_LOGI(TAG, "状态转换: %s -> %s", old_state_name, new_state_name);

        // 更新状态
        current_state_    = new_state;
        state_start_time_ = esp_timer_get_time();

        // 某些状态转换时重置重试计数
        if (new_state == DeviceState::PROVISIONING || new_state == DeviceState::CONNECTING ||
            new_state == DeviceState::WAKEWORD_WAIT)
        {
            resetRetryCount();
        }
    }

    bool App::isStateTimeout(int timeout_ms) const
    {
        int64_t current_time = esp_timer_get_time();
        int64_t elapsed_time = (current_time - state_start_time_) / 1000; // 转换为毫秒
        return elapsed_time >= timeout_ms;
    }

    // ==================== 状态处理函数 ====================

    void App::handleInitState()
    {
        if (!initAudio(getI2CBusHandle(), 16000))
        {
            ESP_LOGE(TAG, "音频初始化失败");
            setState(DeviceState::ERROR);
            return;
        }

        if (!initQMI8658A(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "QMI8658A 初始化失败");
        }

        if (!initAPDS9930(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "APDS-9930 初始化失败");
        }
        else
        {
            // 启动 APDS-9930 传感器数据获取
            if (apds9930_.start())
            {
                // 启动后台数据采集任务
                if (!apds9930_.startDataCollection(5000))
                {
                    ESP_LOGW(TAG, "APDS-9930 数据采集任务启动失败");
                }
            }
        }

        if (!initMPR121(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "MPR121 触摸传感器初始化失败");
        }
        else
        {
            // 启动后台数据采集任务
            if (!mpr121_.startDataCollection(100))
            {
                ESP_LOGW(TAG, "MPR121 触摸传感器数据采集任务启动失败");
            }
        }

        if (!initM0404(UART_NUM_2, GPIO_NUM_2, GPIO_NUM_1, 115200))
        {
            ESP_LOGW(TAG, "M0404 压力传感器初始化失败");
        }
        else
        {
            // 启动后台数据采集任务
            if (!m0404_.startDataCollection(10000))
            {
                ESP_LOGW(TAG, "M0404 压力传感器数据采集任务启动失败");
            }
        }

        // 初始化摄像头
        if (!initCamera(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "摄像头初始化失败，图片上传功能将不可用");
        }

        if (!initProvision())
        {
            ESP_LOGE(TAG, "配网管理器初始化失败");
            setState(DeviceState::ERROR);
            return;
        }

        if (!initNTP())
        {
            ESP_LOGW(TAG, "NTP初始化失败，将在后台继续尝试");
        }

        // ESP_LOGI(TAG, "初始化完成，进入配网状态");
        setState(DeviceState::PROVISIONING);
    }

    void App::handleProvisioningState()
    {

        // 检查配网是否已完成
        if (provision_completed_)
        {
            if (provision_success_)
            {
                ESP_LOGI(TAG, "配网成功，进入NTP同步状态");
                setState(DeviceState::NTP_SYNC);
            }
            else
            {
                // 配网失败，检查重试次数
                retry_count_++;
                if (retry_count_ >= max_retries_)
                {
                    ESP_LOGE(TAG, "配网重试次数超过上限 (%d 次)，进入错误状态", max_retries_);
                    setState(DeviceState::ERROR);
                }
                else
                {
                    ESP_LOGW(TAG, "配网失败，第 %d 次重试...", retry_count_);
                    provision_completed_ = false;

                    // 重新启动配网
                    auto& provision = app::network::ProvisionManager::getInstance();
                    if (provision.start())
                    {
                        // 重置状态开始时间
                        state_start_time_ = esp_timer_get_time();
                    }
                    else
                    {
                        ESP_LOGE(TAG, "重新启动配网失败");
                        setState(DeviceState::ERROR);
                    }
                }
            }
            return;
        }

        // 检查超时（30秒）
        if (isStateTimeout(30000))
        {
            ESP_LOGW(TAG, "配网超时");
            retry_count_++;
            if (retry_count_ >= max_retries_)
            {
                ESP_LOGE(TAG, "配网超时重试次数超过上限 (%d 次），进入错误状态", max_retries_);
                setState(DeviceState::ERROR);
            }
            else
            {
                ESP_LOGW(TAG, "配网超时，第 %d 次重试...", retry_count_);
                // 重新启动配网
                auto& provision = app::network::ProvisionManager::getInstance();
                if (provision.start())
                {
                    // 重置状态开始时间
                    state_start_time_ = esp_timer_get_time();
                }
                else
                {
                    ESP_LOGE(TAG, "重新启动配网失败");
                    setState(DeviceState::ERROR);
                }
            }
        }
    }

    void App::handleNtpSyncState()
    {

        // 检查NTP同步是否已完成
        if (ntp_sync_completed_)
        {
            if (ntp_sync_success_)
            {
                ESP_LOGI(TAG, "NTP时间同步成功，进入WebSocket连接状态");
                setState(DeviceState::CONNECTING);
            }
            else
            {
                ESP_LOGW(TAG, "NTP时间同步失败，将在后台继续尝试");
                setState(DeviceState::CONNECTING);
            }
            return;
        }
    }

    void App::handleConnectingState()
    {
        // 初始化Chatbot（如果还未初始化）
        if (!chatbot_initialized_)
        {
            // 从配置中读取服务器地址和端口
            // const std::string server_host = "192.168.50.100";
            // const int         server_port  = 8081;
            // const std::string path         = "/ws/device/{MAC}";

            const std::string server_host = "192.168.50.68";
            const int         server_port = 8080;

            if (!initChatbot(server_host, server_port, 5, 5, 10000))
            {
                ESP_LOGE(TAG, "Chatbot 初始化失败");
                retry_count_++;
                if (retry_count_ >= max_retries_)
                {
                    ESP_LOGE(TAG, "Chatbot 初始化重试次数超过上限 (%d 次），回退到配网状态",
                             max_retries_);
                    setState(DeviceState::PROVISIONING);
                }
                else
                {
                    // 重置状态开始时间，等待指数退避后重试
                    state_start_time_          = esp_timer_get_time();
                    last_connect_attempt_time_ = esp_timer_get_time();
                }
                return;
            }
            chatbot_initialized_ = true;
            // 初始化后立即尝试连接
            last_connect_attempt_time_ = 0; // 设置为0，表示立即尝试
        }

        // 检查是否已连接
        if (chatbot_.isConnected())
        {
            ESP_LOGI(TAG, "WebSocket连接成功，进入唤醒词等待状态");
            resetRetryCount();
            setState(DeviceState::WAKEWORD_WAIT);
            return;
        }

        int64_t current_time = esp_timer_get_time();

        // 如果是第一次尝试（last_connect_attempt_time_ == 0），立即连接
        // 否则检查是否到了重试时间
        if (last_connect_attempt_time_ != 0)
        {
            // 计算指数退避延迟（毫秒）
            // 第1次：1秒，第2次：2秒，第3次：4秒，第4次：8秒，第5次：16秒
            const int base_delay_ms = 1000;                                // 基础延迟1秒
            const int delay_ms      = base_delay_ms * (1 << retry_count_); // 2^retry_count
            int64_t   delay_us      = delay_ms * 1000;                     // 转换为微秒

            // 检查是否到了重试时间
            int64_t elapsed_time = current_time - last_connect_attempt_time_;

            if (elapsed_time < delay_us)
            {
                // 还没到重试时间，继续等待
                return;
            }
        }

        // 尝试连接
        ESP_LOGI(TAG, "尝试连接WebSocket服务器（第 %d 次）...", retry_count_ + 1);
        last_connect_attempt_time_ = current_time;

        if (chatbot_.connect())
        {
            ESP_LOGI(TAG, "WebSocket连接请求已发送，等待连接确认...");
            state_start_time_ = current_time;
        }
        else
        {
            ESP_LOGW(TAG, "WebSocket连接请求失败，等待重试...");
            retry_count_++;
            if (retry_count_ >= max_retries_)
            {
                ESP_LOGE(TAG, "WebSocket连接重试次数超过上限 (%d 次），回退到配网状态",
                         max_retries_);
                chatbot_initialized_ = false; // 重置初始化状态，下次重新初始化
                setState(DeviceState::PROVISIONING);
            }
            else
            {
                // 重置状态开始时间，等待指数退避后重试
                state_start_time_ = current_time;
            }
        }

        // 检查连接超时（10秒）
        if (isStateTimeout(10000))
        {
            ESP_LOGW(TAG, "WebSocket连接超时");
            retry_count_++;
            if (retry_count_ >= max_retries_)
            {
                ESP_LOGE(TAG, "WebSocket连接重试次数超过上限 (%d 次），回退到配网状态",
                         max_retries_);
                chatbot_initialized_ = false; // 重置初始化状态，下次重新初始化
                setState(DeviceState::PROVISIONING);
            }
            else
            {
                // 重置状态开始时间，等待指数退避后重试
                state_start_time_ = current_time;
            }
        }
    }

    void App::handleWakewordWaitState()
    {
        // 初始化音频（如果还未初始化）
        if (!audio_.isInitialized())
        {
            if (!initAudio(getI2CBusHandle(), 16000))
            {
                ESP_LOGE(TAG, "音频初始化失败");
                // 音频初始化失败，等待重试
                return;
            }
        }

        // 初始化AFE（如果还未初始化）
        if (!afe_ || !afe_->isValid())
        {
            if (!initAfe())
            {
                ESP_LOGW(TAG, "AFE初始化失败，将在后台继续尝试");
                // AFE初始化失败，等待重试
                return;
            }
        }

        // 启动音频采集（如果还未启动）
        auto& capture = media::audio::capture::AudioCapture::getInstance();
        if (capture.getSampleRate() != 0 && !capture.isCapturing())
        {
            if (!startAudioCapture())
            {
                ESP_LOGE(TAG, "启动音频采集失败");
                return;
            }
        }

        // 初始化唤醒词检测（如果还未初始化）
        if (!wakeword_)
        {
            if (!initWakeWord())
            {
                ESP_LOGW(TAG, "唤醒词初始化失败，将在后台继续尝试");
                // 唤醒词初始化失败，等待重试
                return;
            }
        }

        // 启动唤醒词检测（如果还未启动）
        if (wakeword_ && !wakeword_->isRunning())
        {
            if (!startWakeWord())
            {
                ESP_LOGW(TAG, "启动唤醒词检测失败，将在后台继续尝试");
                return;
            }
        }

        // 检查是否检测到唤醒词
        if (wakeword_detected_)
        {
            ESP_LOGI(TAG, "检测到唤醒词，进入正常运行状态");
            wakeword_detected_ = false; // 重置标志
            resetRetryCount();
            setState(DeviceState::RUNNING);
            return;
        }

        // 等待唤醒词（阻塞式，但不阻塞主循环）
        // 唤醒词检测通过事件回调异步触发，这里只需要等待标志被设置
    }

    void App::handleRunningState()
    {
        // 停止唤醒词检测（仅在首次进入时执行一次）
        static bool wakeword_stopped = false;
        if (!wakeword_stopped && wakeword_ && wakeword_->isRunning())
        {
            stopWakeWord();
            wakeword_stopped = true;
        }

        // 检查WebSocket连接状态
        if (!chatbot_.isConnected())
        {
            ESP_LOGW(TAG, "WebSocket连接断开，尝试重连...");
            retry_count_++;
            if (retry_count_ >= max_retries_)
            {
                ESP_LOGE(TAG, "WebSocket重连次数超过上限 (%d 次），回退到配网状态", max_retries_);
                chatbot_initialized_ = false;
                wakeword_stopped     = false; // 重置标志
                setState(DeviceState::PROVISIONING);
                return;
            }
            else
            {
                // 尝试重连
                if (chatbot_.connect())
                {
                    ESP_LOGI(TAG, "WebSocket重连成功");
                    resetRetryCount();
                }
                else
                {
                    ESP_LOGW(TAG, "WebSocket重连失败，等待下次重试");
                    // 使用指数退避延迟
                    sys::task::TaskManager::delayMs(1000 *
                                                    (1 << retry_count_)); // 1s, 2s, 4s, 8s, 16s
                }
                return;
            }
        }

        // 如果连接正常，重置重试计数
        if (retry_count_ > 0)
        {
            resetRetryCount();
        }

        // 初始化逻辑配置（仅一次）
        static const logic_config_t config      = initLogicConfig();
        static int                  zero_streak = 0;

        // 定期采集并发送传感器数据（每1秒）
        static int64_t last_sensor_time = 0;
        int64_t        current_time     = esp_timer_get_time();
        if (current_time - last_sensor_time >= 1000000) // 1秒
        {
            // 计算控制位并转换为二进制字符串
            std::string command_str = calculateAndConvertControl(config, zero_streak);

            // 采集并发送传感器数据
            collectAndSendSensorData(command_str);

            last_sensor_time = current_time;
        }

        // 摄像头图片上传（当 command[4] == '1' 时，每5秒上传一张）
        static int64_t last_image_time = 0;
        if (current_time - last_image_time >= 5000000) // 5秒
        {
            // 检查 command[4] 是否为 '1'（摄像头使能）
            static std::string last_command_str;
            std::string        command_str = calculateAndConvertControl(config, zero_streak);

            // 如果 command[4] == '1'，上传图片
            if (command_str.length() == 5 && command_str[4] == '1')
            {
                if (captureAndSendImage())
                {
                    ESP_LOGI(TAG, "图片上传成功");
                }
                else
                {
                    ESP_LOGW(TAG, "图片上传失败");
                }
            }

            last_image_time = current_time;
        }

        // 定期打印系统信息（每5秒）
        static int64_t last_log_time = 0;
        if (current_time - last_log_time >= 5000000) // 5秒
        {
            logSystemInfo();
            last_log_time = current_time;
        }
    }

    void App::handleErrorState() {}

    void App::handleRecoveryState() {}

    // ==================== 初始化方法 ====================
    // 初始化NVS
    bool App::initNVS()
    {
        esp_err_t ret = nvs_flash_init();
        if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND)
        {
            ESP_ERROR_CHECK(nvs_flash_erase());
            ret = nvs_flash_init();
        }
        ESP_ERROR_CHECK(ret);
        return true;
    }

    // 初始化I2C
    bool App::initI2C(gpio_num_t sda, gpio_num_t scl, i2c_port_t port)
    {
        i2c::Config cfg;
        cfg.sda_pin = sda;
        cfg.scl_pin = scl;
        cfg.port    = port;

        if (!i2c_.init(&cfg))
        {
            ESP_LOGE(TAG, "I2C 初始化失败");
            return false;
        }
        else
        {
            i2c_.scan(200);
        }
        return true;
    }

    // 初始化Assets
    bool App::initAssets()
    {
        auto& assets = app::assets::Assets::getInstance();

        // 初始化 Assets
        if (!assets.init())
        {
            ESP_LOGE(TAG, "Assets 初始化失败");
            return false;
        }

        // 加载模型配置
        if (!assets.apply())
        {
            ESP_LOGE(TAG, "加载模型失败");
            return false;
        }

        // 打印模型列表
        srmodel_list_t* models = assets.getModelsList();
        if (models != nullptr && models->num > 0)
        {
            ESP_LOGI(TAG, "成功加载 %d 个模型:", models->num);
            for (int i = 0; i < models->num; i++)
            {
                ESP_LOGI(TAG, "  [%d] %s", i, models->model_name[i]);
            }
        }
        return true;
    }

    // 初始化事件系统
    bool App::initEvent()
    {
        auto& event_mgr = app::sys::event::EventManager::getInstance();
        if (!event_mgr.init())
        {
            ESP_LOGE(TAG, "事件系统初始化失败");
            return false;
        }
        return true;
    }

    // 初始化QMI8658A
    bool App::initQMI8658A(i2c_master_bus_handle_t i2c_handle)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }
        if (!qmi8658a_.init(i2c_handle, device::qmi8658a::QMI8658A_ADDR_LOW))
        {
            ESP_LOGE(TAG, "QMI8658A 初始化失败");
            return false;
        }
        return true;
    }

    // 初始化APDS-9930
    bool App::initAPDS9930(i2c_master_bus_handle_t i2c_handle)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }
        if (!apds9930_.init(i2c_handle, device::apds9930::APDS9930_I2C_ADDR))
        {
            ESP_LOGE(TAG, "APDS-9930 初始化失败");
            return false;
        }
        return true;
    }

    // 初始化MPR121
    bool App::initMPR121(i2c_master_bus_handle_t i2c_handle)
    {
        if (i2c_handle == nullptr)
        {
            ESP_LOGE(TAG, "I2C 总线句柄无效");
            return false;
        }

        if (!mpr121_.init(i2c_handle, device::mpr121::MPR121_I2C_ADDR, GPIO_NUM_NC))
        {
            ESP_LOGE(TAG, "MPR121 初始化失败");
            return false;
        }
        return true;
    }

    // 初始化M0404
    bool App::initM0404(uart_port_t uart_num, gpio_num_t tx_pin, gpio_num_t rx_pin, int baud_rate)
    {
        if (!m0404_.init(uart_num, tx_pin, rx_pin, baud_rate))
        {
            ESP_LOGE(TAG, "M0404 初始化失败");
            return false;
        }
        return true;
    }

    bool App::initProvision()
    {
        auto& provision = app::network::ProvisionManager::getInstance();
        if (!provision.init("EmotiPet"))
        {
            ESP_LOGE(TAG, "配网管理器初始化失败");
            return false;
        }

        // 设置状态回调
        provision.setStatusCallback([this](app::network::ProvisionStatus status)
                                    { onProvisionStatus(status); });

        // 设置完成回调
        provision.setCompleteCallback([this](bool success, const char* ssid)
                                      { onProvisionComplete(success, ssid); });

        auto& wifi = app::network::wifi::WiFiManager::getInstance();

        // 检查是否有保存的WiFi凭证
        if (!wifi.hasSavedCredentials())
        {
            ESP_LOGI(TAG, "没有保存的 WiFi 凭证，启动配网");
            if (!provision.start())
            {
                ESP_LOGE(TAG, "启动配网失败");
                return false;
            }
        }
        else
        {
            std::vector<app::network::wifi::Credentials> saved_creds;
            if (wifi.getCredentials(saved_creds) && !saved_creds.empty())
            {
                ESP_LOGI(TAG, "已保存的 WiFi 凭证数量: %u", (unsigned int)saved_creds.size());
                for (size_t i = 0; i < saved_creds.size(); i++)
                {
                    ESP_LOGI(TAG, "  [%u] SSID: %s", (unsigned int)i, saved_creds[i].ssid);
                }

                // 即使有保存的凭证，也启动配网（允许用户重新配置）
                if (!provision.start())
                {
                    ESP_LOGE(TAG, "启动配网失败");
                    return false;
                }
            }
        }

        return true;
    }

    bool App::initNTP()
    {
        auto& ntp_mgr = app::protocol::ntp::NTPManager::getInstance();

        // 初始化NTP
        if (!ntp_mgr.init())
        {
            ESP_LOGE(TAG, "NTP初始化失败");
            return false;
        }

        // 配置NTP服务器
        std::vector<std::string> ntp_servers = {
            "cn.pool.ntp.org",  // 中国NTP服务器池
            "time.windows.com", // Windows时间服务器
            "ntp.aliyun.com"    // 阿里云NTP服务器
        };

        if (!ntp_mgr.configure(ntp_servers, app::protocol::ntp::SyncMode::IMMEDIATE))
        {
            ESP_LOGE(TAG, "NTP服务器配置失败");
            return false;
        }

        // 设置时区（中国标准时间，UTC+8）
        if (!ntp_mgr.setTimezone("CST-8"))
        {
            ESP_LOGW(TAG, "设置时区失败，使用默认时区");
        }

        // 设置同步回调
        ntp_mgr.setSyncCallback(
            [this](app::protocol::ntp::SyncStatus status)
            {
                switch (status)
                {
                case app::protocol::ntp::SyncStatus::COMPLETED:
                    ESP_LOGI(TAG, "NTP时间同步完成");
                    ntp_sync_completed_ = true;
                    ntp_sync_success_   = true;
                    break;
                case app::protocol::ntp::SyncStatus::FAILED:
                    ESP_LOGE(TAG, "NTP时间同步失败");
                    ntp_sync_completed_ = true;
                    ntp_sync_success_   = false;
                    break;
                case app::protocol::ntp::SyncStatus::IN_PROGRESS:
                    ESP_LOGI(TAG, "NTP时间同步进行中...");
                    break;
                default:
                    break;
                }
            });

        // 启动NTP同步
        if (!ntp_mgr.start())
        {
            ESP_LOGE(TAG, "启动NTP同步失败");
            return false;
        }

        ESP_LOGI(TAG, "NTP初始化成功");
        return true;
    }

    bool App::initChatbot(const std::string& server_host, int server_port, int ping_interval_sec,
                          int pingpong_timeout_sec, int reconnect_timeout_ms,
                          const std::string& path)
    {
        // 构建WebSocket URI
        std::ostringstream uri_stream;
        uri_stream << "ws://" << server_host << ":" << server_port;

        // 如果提供了路径，添加到URI
        if (!path.empty())
        {
            std::string processed_path = path;

            // 替换 {MAC} 占位符为实际MAC地址
            std::string mac_address = chatbot::message::getDeviceMacAddress();
            size_t      pos         = processed_path.find("{MAC}");
            if (pos != std::string::npos)
            {
                processed_path.replace(pos, 5, mac_address);
            }

            // 确保路径以 / 开头
            if (processed_path[0] != '/')
            {
                uri_stream << "/";
            }
            uri_stream << processed_path;
        }

        std::string server_uri = uri_stream.str();

        ESP_LOGI(TAG, "服务器地址: %s", server_uri.c_str());

        // 配置Chatbot
        chatbot::Chatbot::Config chatbot_config;
        chatbot_config.server_uri              = server_uri;
        chatbot_config.ping_interval_sec       = ping_interval_sec;
        chatbot_config.pingpong_timeout_sec    = pingpong_timeout_sec;
        chatbot_config.reconnect_timeout_ms    = reconnect_timeout_ms;
        chatbot_config.network_timeout_ms      = 10000; // 网络操作超时
        chatbot_config.disable_auto_reconnect  = false; // 禁用自动重连
        chatbot_config.disable_pingpong_discon = true;  // 禁用心跳包断开

        // 初始化Chatbot
        if (!chatbot_.init(chatbot_config))
        {
            ESP_LOGE(TAG, "Chatbot 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "Chatbot 初始化成功");

        // 设置发送回调
        chatbot_.setSendCallback([this](chatbot::message::Message& msg) -> std::string
                                 { return message_sender_.processMessage(msg); });

        // 设置接收回调
        chatbot_.setReceiveCallback([this](const std::string& json_str) -> bool
                                    { return message_receiver_.handleMessage(json_str); });

        // 设置消息处理函数
        setupMessageHandlers();

        return true;
    }

    bool App::initAudio(i2c_master_bus_handle_t i2c_handle, int sample_rate)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }

        if (sample_rate != 8000 && sample_rate != 16000 && sample_rate != 32000 &&
            sample_rate != 44100 && sample_rate != 48000)
        {
            ESP_LOGE(TAG, "采样率无效: %d (支持: 8000, 16000, 32000, 44100, 48000)", sample_rate);
            return false;
        }

        // 配置 Audio
        media::audio::Config cfg;
        cfg.i2c_master_handle  = i2c_handle;
        cfg.input_sample_rate  = sample_rate;
        cfg.output_sample_rate = sample_rate;
        cfg.input_reference    = false;

        if (!audio_.init(&cfg))
        {
            ESP_LOGE(TAG, "Audio 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "Audio 初始化成功 (采样率=%dHz)", sample_rate);

        // 初始化 AudioCapture
        auto& capture = media::audio::capture::AudioCapture::getInstance();

        // 帧大小设置为 160（对应 10ms @ 16kHz）
        const size_t frame_size = 160;

        if (!capture.init(&audio_, frame_size))
        {
            ESP_LOGE(TAG, "AudioCapture 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "AudioCapture 初始化成功");
        ESP_LOGI(TAG, "  - 帧大小: %u 样本", (unsigned int)capture.getFrameSize());
        ESP_LOGI(TAG, "  - 采样率: %d Hz", capture.getSampleRate());
        ESP_LOGI(TAG, "  - 通道数: %d", capture.getChannels());

        return true;
    }

    bool App::initCamera(i2c_master_bus_handle_t i2c_handle)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }

        // 配置摄像头
        media::camera::Config cam_cfg;
        cam_cfg.i2c_handle = i2c_handle;

        if (!camera_.init(&cam_cfg))
        {
            ESP_LOGE(TAG, "摄像头初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "摄像头初始化成功");
        return true;
    }

    bool App::initAfe()
    {
        // 获取 Assets 中的模型列表
        auto&           assets      = app::assets::Assets::getInstance();
        srmodel_list_t* models_list = nullptr;

        if (assets.isPartitionValid())
        {
            models_list = assets.getModelsList();
            if (models_list != nullptr && models_list->num > 0)
            {
                ESP_LOGI(TAG, "使用 Assets 加载的模型 (数量=%d)", models_list->num);
            }
            else
            {
                ESP_LOGW(TAG, "未加载模型，AFE 将使用默认模型");
            }
        }
        else
        {
            ESP_LOGW(TAG, "Assets 分区无效，AFE 将使用默认模型");
        }

        // 配置 AFE
        // 注意：AFE_TYPE_VC 模式只支持单麦克风通道，多通道输入时只会选择第一个通道
        // 对于多麦输入，需要先做波束成形，转换为单声道
        media::audio::process::afe::Config afe_config;
        afe_config.input_format     = "M"; // 单麦克风
        afe_config.sample_rate      = 16000;
        afe_config.enable_aec       = false;              // 回声消除
        afe_config.enable_vad       = true;               // 人声检测
        afe_config.enable_ns        = false;              // 噪声抑制
        afe_config.enable_agc       = false;              // 自动增益控制
        afe_config.vad_mode         = VAD_MODE_0;         // 人声检测模式（0 最宽松，4 最严格）
        afe_config.vad_min_noise_ms = 100;                // 最小静音时长（毫秒）
        afe_config.afe_type         = AFE_TYPE_VC;        // 语音通信模式
        afe_config.afe_mode         = AFE_MODE_HIGH_PERF; // 高性能模式
        afe_config.models_list      = models_list;        // 模型列表

        // 创建 AFE 实例
        afe_ = std::make_unique<media::audio::process::afe::Afe>(afe_config);

        if (!afe_ || !afe_->isValid())
        {
            ESP_LOGE(TAG, "AFE 初始化失败");
            afe_.reset();
            return false;
        }

        ESP_LOGI(TAG, "AFE 初始化成功");

        // 设置 AFE 的 VAD 状态回调
        afe_->setVadStateCallback(
            [this](bool is_speaking)
            {
                // 更新 VAD 状态
                is_speaking_.store(is_speaking, std::memory_order_release);

                // VAD 状态变化时的处理
                if (is_speaking)
                {
                    ESP_LOGI(TAG, "检测到语音");
                }
            });

        // 设置 AFE 的音频输出回调（处理后的音频）
        afe_->setAudioOutputCallback(
            [](const int16_t* data, size_t samples)
            {
                // 处理后的音频数据可以用于：
                // 1. 音频上传（OPUS 编码）
                // 2. 唤醒词检测
                // 3. 其他音频处理
                // 这里暂时不处理，后续可以在 handleListen 中启用上传
                (void)data;
                (void)samples;
            });

        return true;
    }

    bool App::startAudioCapture()
    {
        // 检查 AFE 是否已初始化
        if (!afe_ || !afe_->isValid())
        {
            ESP_LOGE(TAG, "AFE 未初始化，无法启动音频采集");
            return false;
        }

        // 获取 AudioCapture 实例
        auto& capture = media::audio::capture::AudioCapture::getInstance();

        // 检查 AudioCapture 是否已初始化
        if (capture.getSampleRate() == 0)
        {
            ESP_LOGE(TAG, "AudioCapture 未初始化，无法启动");
            return false;
        }

        // 如果已经启动，先停止
        if (capture.isCapturing())
        {
            ESP_LOGW(TAG, "音频采集已在运行，先停止");
            stopAudioCapture();
        }

        // 注册AFE的音频数据回调(不要在此函数中执行耗时操作，避免阻塞音频采集)
        afe_callback_id_ = capture.registerCallback(
            [this](const int16_t* data, size_t samples, int channels, int sample_rate)
            {
                (void)sample_rate; // 未使用

                if (!afe_ || !afe_->isValid())
                {
                    return;
                }

                // 获取 AFE 需要的输入帧大小
                static size_t s_afe_feed_size = 0;
                if (s_afe_feed_size == 0)
                {
                    s_afe_feed_size = afe_->getFeedSize();
                    if (s_afe_feed_size == 0)
                    {
                        return;
                    }
                    ESP_LOGI(TAG, "AFE 输入帧大小: %u 样本", (unsigned int)s_afe_feed_size);
                }

                // 波束成形：将多通道转换为单声道（AFE_TYPE_VC 只需要单声道输入）
                // 所有通道的平均值（简单波束成形）
                static int16_t s_mono_buffer[160]; // 单帧缓冲区
                const size_t   mono_samples = (samples < 160) ? samples : 160;

                if (channels > 1)
                {
                    // 多通道转单声道
                    for (size_t i = 0; i < mono_samples; i++)
                    {
                        int32_t sum = 0;
                        for (int ch = 0; ch < channels; ch++)
                        {
                            sum += static_cast<int32_t>(data[(i * channels) + ch]);
                        }
                        s_mono_buffer[i] = static_cast<int16_t>(sum / channels);
                    }
                }
                else
                {
                    // 已经是单声道，直接复制
                    for (size_t i = 0; i < mono_samples; i++)
                    {
                        s_mono_buffer[i] = data[i];
                    }
                }

                // 用于累积多帧数据，以满足 AFE 的输入要求
                static int16_t s_afe_buffer[512];    // AFE 最大输入帧大小
                static size_t  s_afe_buffer_pos = 0; // 当前累积位置

                // 将当前帧数据复制到累积缓冲区
                size_t copy_size = (s_afe_buffer_pos + mono_samples <= s_afe_feed_size)
                                       ? mono_samples
                                       : (s_afe_feed_size - s_afe_buffer_pos);
                for (size_t i = 0; i < copy_size; i++)
                {
                    s_afe_buffer[s_afe_buffer_pos + i] = s_mono_buffer[i];
                }
                s_afe_buffer_pos += copy_size;

                // 当累积到足够的数据时，输入到 AFE
                if (s_afe_buffer_pos >= s_afe_feed_size)
                {
                    // 输入到 AFE
                    if (afe_->feed(s_afe_buffer, s_afe_feed_size))
                    {
                        // 获取处理结果
                        afe_->fetch(0); // 0 表示非阻塞
                    }

                    // 如果还有剩余数据，保留在缓冲区中
                    if (s_afe_buffer_pos > s_afe_feed_size)
                    {
                        size_t remaining = s_afe_buffer_pos - s_afe_feed_size;
                        for (size_t i = 0; i < remaining; i++)
                        {
                            s_afe_buffer[i] = s_afe_buffer[s_afe_feed_size + i];
                        }
                        s_afe_buffer_pos = remaining;
                    }
                    else
                    {
                        s_afe_buffer_pos = 0;
                    }
                }
            });

        if (afe_callback_id_ < 0)
        {
            ESP_LOGE(TAG, "注册AFE音频数据回调失败");
            return false;
        }

        ESP_LOGI(TAG, "AFE音频数据回调已注册 (ID=%d)", afe_callback_id_);

        // 启动音频采集
        if (!capture.start())
        {
            ESP_LOGE(TAG, "启动音频采集失败");
            // 取消注册回调
            capture.unregisterCallback(afe_callback_id_);
            afe_callback_id_ = -1;
            return false;
        }

        ESP_LOGI(TAG, "音频采集已启动");
        return true;
    }

    void App::stopAudioCapture()
    {
        // 取消注册AFE音频数据回调
        if (afe_callback_id_ >= 0)
        {
            auto& capture = media::audio::capture::AudioCapture::getInstance();
            capture.unregisterCallback(afe_callback_id_);
            afe_callback_id_ = -1;
            ESP_LOGI(TAG, "AFE音频数据回调已取消注册");
        }

        auto& capture = media::audio::capture::AudioCapture::getInstance();

        // 检查是否正在采集
        if (!capture.isCapturing())
        {
            ESP_LOGW(TAG, "AudioCapture 未在采集");
            return;
        }

        // 停止音频采集
        capture.stop();
        ESP_LOGI(TAG, "音频采集已停止");
    }

    bool App::initWakeWord()
    {
        // 检查 Assets 是否已加载
        auto&           assets      = app::assets::Assets::getInstance();
        srmodel_list_t* models_list = nullptr;

        if (assets.isPartitionValid())
        {
            models_list = assets.getModelsList();
            if (models_list == nullptr || models_list->num == 0)
            {
                ESP_LOGW(TAG, "未加载模型，唤醒词检测将无法使用");
                return false;
            }
        }
        else
        {
            ESP_LOGW(TAG, "Assets 分区无效，唤醒词检测将无法使用");
            return false;
        }

        // 创建 WakeWord 实例
        wakeword_ = std::unique_ptr<media::audio::wakeword::WakeWord>(
            media::audio::wakeword::createCustomWakeWord());

        if (!wakeword_)
        {
            ESP_LOGE(TAG, "创建 WakeWord 实例失败");
            return false;
        }

        // 获取音频配置
        int sample_rate = audio_.getInputSampleRate();
        int channels    = audio_.getInputChannels();

        // 初始化 WakeWord
        if (!wakeword_->init(models_list, sample_rate, channels))
        {
            ESP_LOGE(TAG, "WakeWord 初始化失败");
            wakeword_.reset();
            return false;
        }

        ESP_LOGI(TAG, "WakeWord 初始化成功");
        ESP_LOGI(TAG, "  - 采样率: %d Hz", sample_rate);
        ESP_LOGI(TAG, "  - 通道数: %d", channels);
        ESP_LOGI(TAG, "  - 输入帧大小: %u 样本", (unsigned int)wakeword_->getFeedSize());

        // 注册唤醒词事件处理器
        auto& event_mgr = app::sys::event::EventManager::getInstance();
        event_mgr.registerHandler(
            media::audio::wakeword::WAKEWORD_EVENT_BASE,
            media::audio::wakeword::WAKEWORD_EVENT_DETECTED,
            [this](esp_event_base_t event_base, app::sys::event::EventId event_id,
                   const app::sys::event::EventData& event_data)
            {
                (void)event_base;
                (void)event_id;

                if (event_data.data != nullptr)
                {
                    const media::audio::wakeword::WakeWordEventData* wake_data =
                        static_cast<const media::audio::wakeword::WakeWordEventData*>(
                            event_data.data);

                    ESP_LOGI(TAG, "========== 检测到唤醒词 ==========");
                    ESP_LOGI(TAG, "  文本: %s", wake_data->text);
                    ESP_LOGI(TAG, "  命令: %s", wake_data->command);
                    ESP_LOGI(TAG, "  动作: %s", wake_data->action);
                    ESP_LOGI(TAG, "  概率: %.2f", wake_data->probability);
                    ESP_LOGI(TAG, "====================================");

                    // 设置唤醒词检测标志
                    wakeword_detected_ = true;
                }
            });

        return true;
    }

    bool App::startWakeWord()
    {
        // 检查 WakeWord 是否已初始化
        if (!wakeword_)
        {
            if (!initWakeWord())
            {
                ESP_LOGE(TAG, "WakeWord 初始化失败，无法启动");
                return false;
            }
        }

        // 检查是否已经在运行
        if (wakeword_->isRunning())
        {
            ESP_LOGW(TAG, "WakeWord 已在运行");
            return true;
        }

        // 获取 AudioCapture 实例
        auto& capture = media::audio::capture::AudioCapture::getInstance();

        // 检查 AudioCapture 是否已初始化
        if (capture.getSampleRate() == 0)
        {
            ESP_LOGE(TAG, "AudioCapture 未初始化，无法启动唤醒词检测");
            return false;
        }

        // 注册唤醒词的音频数据回调（不要在此函数中执行耗时操作，避免阻塞音频采集）
        wakeword_callback_id_ = capture.registerCallback(
            [this](const int16_t* data, size_t samples, int channels, int sample_rate)
            {
                (void)sample_rate; // 未使用

                if (!wakeword_ || !wakeword_->isRunning())
                {
                    return;
                }

                // 获取 WakeWord 需要的输入帧大小
                static size_t wakeword_feed_size = 0;
                if (wakeword_feed_size == 0)
                {
                    wakeword_feed_size = wakeword_->getFeedSize();
                    if (wakeword_feed_size == 0)
                    {
                        return;
                    }
                }

                // 波束成形：将多通道转换为单声道
                // 所有通道的平均值（简单波束成形）
                static std::vector<int16_t> wakeword_buffer;
                wakeword_buffer.clear();
                wakeword_buffer.reserve(samples);

                if (channels > 1)
                {
                    // 多通道转单声道
                    for (size_t i = 0; i < samples; i++)
                    {
                        int32_t sum = 0;
                        for (int ch = 0; ch < channels; ch++)
                        {
                            sum += static_cast<int32_t>(data[(i * channels) + ch]);
                        }
                        wakeword_buffer.push_back(static_cast<int16_t>(sum / channels));
                    }
                }
                else
                {
                    // 已经是单声道，直接复制
                    wakeword_buffer.assign(data, data + samples);
                }

                // 用于累积多帧数据，以满足 WakeWord 的输入要求
                static std::vector<int16_t> wakeword_accum_buffer;

                // 将当前帧数据添加到累积数据中
                wakeword_accum_buffer.insert(wakeword_accum_buffer.end(), wakeword_buffer.begin(),
                                             wakeword_buffer.end());

                // 当累积到足够的数据时，输入到 WakeWord
                if (wakeword_accum_buffer.size() >= wakeword_feed_size)
                {
                    // 输入到 WakeWord
                    wakeword_->feed(wakeword_accum_buffer);

                    // 如果还有剩余数据，保留在缓冲区中
                    if (wakeword_accum_buffer.size() > wakeword_feed_size)
                    {
                        std::vector<int16_t> remaining_data(wakeword_accum_buffer.begin() +
                                                                wakeword_feed_size,
                                                            wakeword_accum_buffer.end());
                        wakeword_accum_buffer = std::move(remaining_data);
                    }
                    else
                    {
                        wakeword_accum_buffer.clear();
                    }
                }
            });

        if (wakeword_callback_id_ < 0)
        {
            ESP_LOGE(TAG, "注册唤醒词音频数据回调失败");
            return false;
        }

        ESP_LOGI(TAG, "唤醒词音频数据回调已注册 (ID=%d)", wakeword_callback_id_);

        // 启动唤醒词检测
        wakeword_->start();

        ESP_LOGI(TAG, "唤醒词检测已启动");
        return true;
    }

    void App::stopWakeWord()
    {
        // 停止唤醒词检测
        if (wakeword_ && wakeword_->isRunning())
        {
            wakeword_->stop();
            ESP_LOGI(TAG, "唤醒词检测已停止");
        }

        // 取消注册音频数据回调
        if (wakeword_callback_id_ >= 0)
        {
            auto& capture = media::audio::capture::AudioCapture::getInstance();
            capture.unregisterCallback(wakeword_callback_id_);
            wakeword_callback_id_ = -1;
            ESP_LOGI(TAG, "唤醒词音频数据回调已取消注册");
        }
    }

    // ==================== 日志打印 ====================

    void App::logSystemInfo()
    {
        ESP_LOGI(TAG, "================= 系统信息 ===================");
        logMemoryInfo();
        logWiFiInfo();
        logQMI8658AInfo();
        logMPR121Info();
        logM0404Info();
        logAPDS9930Info();
        ESP_LOGI(TAG, "==============================================");
    }

    void App::logMemoryInfo()
    {
        auto mem_info = app::sys::info::MemoryInfo::getMemoryInfo();
        auto cpu_info = app::sys::info::CpuInfo::getCpuInfo();

        ESP_LOGI(TAG, "CPU 频率: %lu MHz", cpu_info.getCpuFrequency() / 1000000);
        ESP_LOGI(TAG, "内部 SRAM: %u / %u KB (空闲/总量)",
                 (unsigned int)(mem_info.getSramFree() / 1024),
                 (unsigned int)(mem_info.getSramTotal() / 1024));
        ESP_LOGI(TAG, "PSRAM: %u / %u KB (空闲/总量)",
                 (unsigned int)(mem_info.getPsramFree() / 1024),
                 (unsigned int)(mem_info.getPsramTotal() / 1024));
    }

    void App::logWiFiInfo()
    {
        auto& provision = app::network::ProvisionManager::getInstance();
        auto  status    = provision.getStatus();

        if (status == app::network::ProvisionStatus::CONNECTED)
        {
            std::string ssid = provision.getCurrentSsid();
            std::string ip   = provision.getCurrentIp();
            ESP_LOGI(TAG, "WiFi SSID: %s, IP: %s", ssid.c_str(), ip.c_str());
        }
    }

    void App::logQMI8658AInfo()
    {
        device::qmi8658a::SensorData data;
        // 读取传感器数据并计算姿态角
        if (qmi8658a_.read(data, device::qmi8658a::READ_ALL))
        {
            ESP_LOGI(TAG, "QMI8658A 加速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f m/s²", data.accel_x,
                     data.accel_y, data.accel_z);
            ESP_LOGI(TAG, "QMI8658A 角速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f rad/s", data.gyro_x,
                     data.gyro_y, data.gyro_z);
            ESP_LOGI(TAG, "QMI8658A 姿态:   Roll=%+7.1f°  Pitch=%+7.1f°  Yaw=%+7.1f°", data.angle_x,
                     data.angle_y, data.angle_z);
        }
    }

    void App::logAPDS9930Info()
    {
        float lux_value = 0.0f;
        if (apds9930_.readAmbientLightLux(lux_value))
        {
            // 根据阈值判断状态（<1000 lux 为暗，>=1000 lux 为亮）
            int light_status = (lux_value >= 1000.0f) ? 1 : 0;
            ESP_LOGI(TAG, "APDS-9930 环境光传感器:");
            ESP_LOGI(TAG, "  状态: %d (%s)", light_status, light_status ? "亮" : "暗");
            ESP_LOGI(TAG, "  照度: %.2f lux", lux_value);
        }
        else
        {
            ESP_LOGW(TAG, "APDS-9930 环境光传感器: 读取失败");
        }
    }

    void App::logMPR121Info()
    {
        device::mpr121::TouchData data;
        if (mpr121_.readTouch(data))
        {
            int touch_status = (data.touched != 0) ? 1 : 0;
            ESP_LOGI(TAG, "MPR121 触摸传感器状态: %d (位掩码: 0x%04X)", touch_status, data.touched);
        }
        else
        {
            ESP_LOGW(TAG, "MPR121 触摸传感器: 读取失败");
        }
    }

    void App::logM0404Info()
    {
        app::device::m0404::PressureData data;
        if (m0404_.getLatestPressureData(data))
        {
            ESP_LOGI(TAG, "M0404 压力值 (16个传感器):");
            for (size_t i = 0; i < 16; i++)
            {
                uint16_t raw_value = data.pressures[i]; // 原始值范围：0-65535
                ESP_LOGI(TAG, "  传感器[%2lu]: %5u", (unsigned long)i, raw_value);
            }
        }
    }

    // ==================== 数据上传 ====================

    void App::collectAndSendSensorData(const std::string& command_str)
    {
        // 验证 command 长度
        if (command_str.length() != 5)
        {
            ESP_LOGE(TAG, "command 长度错误: %u，应为5位", (unsigned int)command_str.length());
            return;
        }

        // 构造传感器数据
        chatbot::message::SensorData sensor_data;

        // 根据 command 控制哪些传感器数据需要上传
        // command[0]: 触摸传感器 (touch)
        // command[1]: 压力传感器 (pressure)
        // command[2]: 陀螺仪 (gyroscope)
        // command[3]: 光敏传感器 (photosensitive)
        // command[4]: 摄像头 (camera) - 暂不在此处理

        // 触摸传感器 (command[0])
        if (command_str[0] == '1')
        {
            sensor_data.touch = mpr121_.getCurrentTouchStatus();
        }
        else
        {
            sensor_data.touch = 0; // 不上传，使用默认值
        }

        // 压力传感器 (command[1]) - 16个点位
        if (command_str[1] == '1')
        {
            app::device::m0404::PressureData pressure_data;
            if (m0404_.getLatestPressureData(pressure_data) && pressure_data.valid)
            {
                for (size_t i = 0; i < 16; i++)
                {
                    sensor_data.pressure[i] = pressure_data.pressures[i];
                }
            }
            else
            {
                // 如果读取失败，保持为0
                sensor_data.pressure.fill(0);
            }
        }
        else
        {
            // 不上传，使用默认值（全0）
            sensor_data.pressure.fill(0);
        }

        // 陀螺仪 (command[2])
        if (command_str[2] == '1')
        {
            device::qmi8658a::SensorData gyro_data;
            if (qmi8658a_.read(gyro_data, device::qmi8658a::READ_ALL))
            {
                sensor_data.gyroscope.x = gyro_data.angle_x; // 使用姿态角
                sensor_data.gyroscope.y = gyro_data.angle_y;
                sensor_data.gyroscope.z = gyro_data.angle_z;
            }
            else
            {
                sensor_data.gyroscope.x = 0.0f;
                sensor_data.gyroscope.y = 0.0f;
                sensor_data.gyroscope.z = 0.0f;
            }
        }
        else
        {
            // 不上传，使用默认值
            sensor_data.gyroscope.x = 0.0f;
            sensor_data.gyroscope.y = 0.0f;
            sensor_data.gyroscope.z = 0.0f;
        }

        // 光敏传感器 (command[3])
        if (command_str[3] == '1')
        {
            float photosensitive = 0.0f;
            apds9930_.readAmbientLightLux(photosensitive);
            sensor_data.photosensitive = photosensitive;
        }
        else
        {
            // 不上传，使用默认值
            sensor_data.photosensitive = 0.0f;
        }

        // 构造并发送 transport_info 消息
        chatbot::message::TransportInfoMessage msg;
        msg.base.type = chatbot::message::MessageType::TRANSPORT_INFO;
        msg.base.to   = "server";
        msg.command   = command_str;
        msg.data      = sensor_data;

        if (chatbot_.sendMessage(msg))
        {
            ESP_LOGI(TAG, "传感器数据发送成功 (command: %s)", command_str.c_str());
        }
        else
        {
            ESP_LOGW(TAG, "传感器数据发送失败");
        }
    }

    std::string App::calculateAndConvertControl(const logic_config_t& config, int& zero_streak)
    {
        // 计算控制位
        int control_value =
            calculateControl(mpr121_.getCurrentTouchStatus(), m0404_.getCurrentPressureStatus(),
                             qmi8658a_.getCurrentMotionStatus(), apds9930_.getCurrentLightStatus(),
                             isSpeaking() ? 1 : 0, config, zero_streak, TAG);

        // 转换为5位二进制字符串
        std::string command_str(5, '0');
        for (int i = 0; i < 5; i++)
        {
            if (control_value & (1 << (4 - i)))
            {
                command_str[i] = '1';
            }
        }

        return command_str;
    }

    bool App::captureAndSendImage()
    {
        if (!camera_.isInitialized())
        {
            ESP_LOGE(TAG, "摄像头未初始化，无法捕获图片");
            return false;
        }

        // 捕获帧
        media::camera::FrameBuffer frame;
        if (!camera_.capture(frame, 2)) // 2帧跳过，获取最新帧
        {
            ESP_LOGE(TAG, "摄像头捕获失败");
            return false;
        }

        ESP_LOGI(TAG, "摄像头捕获成功: %dx%d %u bytes", frame.res.width, frame.res.height,
                 (unsigned int)frame.len);

        // 检查像素格式并处理
        if (camera_.getPixelFormat() == media::camera::PixelFormat::JPEG)
        {
            // 直接发送JPEG数据
            ESP_LOGI(TAG, "发送JPEG图片数据，长度: %u 字节", (unsigned int)frame.len);
            return chatbot_.sendBinary(frame.data, frame.len);
        }
        else if (camera_.getPixelFormat() == media::camera::PixelFormat::YUV422)
        {
            // JPEG 编码配置
            media::camera::process::jpeg::encode::EncodeConfig encode_config;
            encode_config.quality   = 80;   // JPEG质量 80%
            encode_config.use_psram = true; // 使用PSRAM

            ESP_LOGI(TAG, "开始YUV422→JPEG编码...");

            int64_t encode_start = esp_timer_get_time();
            auto    jpeg_result  = media::camera::process::jpeg::encode::encodeYUV422ToJPEG(
                frame.data, frame.res.width, frame.res.height, &encode_config);
            int64_t encode_time = esp_timer_get_time() - encode_start;

            if (jpeg_result)
            {
                float compression_ratio = (jpeg_result.len() * 100.0f) / frame.len;
                ESP_LOGI(TAG, "JPEG编码成功 | YUV: %.1fKB → JPEG: %.1fKB (%.1f%%) | 耗时: %ums",
                         frame.len / 1024.0f, jpeg_result.len() / 1024.0f, compression_ratio,
                         (unsigned int)(encode_time / 1000));

                // 发送JPEG数据
                ESP_LOGI(TAG, "发送JPEG图片数据，长度: %u 字节", (unsigned int)jpeg_result.len());
                return chatbot_.sendBinary(jpeg_result.get(), jpeg_result.len());
            }
            else
            {
                ESP_LOGE(TAG, "JPEG编码失败");
                return false;
            }
        }
        else
        {
            ESP_LOGE(TAG, "不支持的像素格式");
            return false;
        }
    }

    // ==================== 回调方法 ====================

    void App::onProvisionStatus(app::network::ProvisionStatus status)
    {
        // 只记录失败状态
        if (status == app::network::ProvisionStatus::FAILED_TIMEOUT ||
            status == app::network::ProvisionStatus::FAILED_WRONG_PWD ||
            status == app::network::ProvisionStatus::FAILED_NOT_FOUND ||
            status == app::network::ProvisionStatus::FAILED_UNKNOWN)
        {
            const char* status_str = "Unknown";
            switch (status)
            {
            case app::network::ProvisionStatus::FAILED_TIMEOUT:
                status_str = "超时";
                break;
            case app::network::ProvisionStatus::FAILED_WRONG_PWD:
                status_str = "密码错误";
                break;
            case app::network::ProvisionStatus::FAILED_NOT_FOUND:
                status_str = "网络未找到";
                break;
            case app::network::ProvisionStatus::FAILED_UNKNOWN:
                status_str = "未知错误";
                break;
            default:
                break;
            }
            ESP_LOGE(TAG, "配网失败: %s", status_str);
        }
    }

    void App::onProvisionComplete(bool success, const char* ssid)
    {
        // 标记配网完成
        provision_completed_ = true;
        provision_success_   = success;

        if (success)
        {
            ESP_LOGI(TAG, "配网成功: SSID=%s", ssid ? ssid : "未知");
        }
        else
        {
            ESP_LOGE(TAG, "配网失败: SSID=%s", ssid ? ssid : "未知");
        }
    }

    // ==================== 消息处理 ====================

    void App::handleRecvInfoMessage(const chatbot::message::RecvInfoMessage& msg)
    {
        ESP_LOGI(TAG, "收到数据接收控制消息，控制位: %s", msg.command.c_str());

        // 解析控制位，更新传感器数据上传配置
        // command是5位字符串：触摸-压力-陀螺仪-光敏-摄像头
        if (msg.command.length() == 5)
        {
            ESP_LOGI(TAG, "传感器上传控制 - 触摸:%c 压力:%c 陀螺仪:%c 光敏:%c 摄像头:%c",
                     msg.command[0], msg.command[1], msg.command[2], msg.command[3],
                     msg.command[4]);

            // TODO: 根据控制位动态调整传感器数据上传
            // 例如：如果摄像头控制位为'1'，则启用摄像头数据上传
            // 如果为'0'，则停止摄像头数据上传
        }
        else
        {
            ESP_LOGW(TAG, "控制位长度错误，期望5位，收到: %s", msg.command.c_str());
        }
    }

    void App::setupMessageHandlers()
    {
        // 设置数据接收控制消息处理
        message_receiver_.setRecvInfoHandler([this](const chatbot::message::RecvInfoMessage& msg)
                                             { handleRecvInfoMessage(msg); });

        // 设置运动控制消息处理
        message_receiver_.setMovInfoHandler([this](const chatbot::message::MovInfoMessage& msg)
                                            { handleMovInfoMessage(msg); });

        // 设置音频播放消息处理
        message_receiver_.setPlayHandler([this](const chatbot::message::PlayMessage& msg)
                                         { handlePlayMessage(msg); });

        // 设置情绪反馈消息处理
        message_receiver_.setEmotionHandler([this](const chatbot::message::EmotionMessage& msg)
                                            { handleEmotionMessage(msg); });

        // 设置错误消息处理
        message_receiver_.setErrorHandler([this](const chatbot::message::ErrorMessage& msg)
                                          { handleErrorMessage(msg); });

        ESP_LOGI(TAG, "消息处理函数已设置");
    }

    void App::sendListenMessage()
    {
        chatbot::message::ListenMessage msg;
        msg.base.type = chatbot::message::MessageType::LISTEN;
        msg.base.to   = "server";

        if (chatbot_.sendMessage(msg))
        {
            ESP_LOGI(TAG, "已发送listen消息，告知服务器开始接收音频");
        }
        else
        {
            ESP_LOGW(TAG, "发送listen消息失败");
        }
    }

    void App::handleMovInfoMessage(const chatbot::message::MovInfoMessage& msg)
    {
        ESP_LOGI(TAG, "收到运动控制消息，舵机数量: %u", (unsigned int)msg.data.size());

        // TODO: 实现舵机控制逻辑
        // 这里需要：
        // 1. 解析每个舵机的控制参数 (move_part, start_time, angle, duration)
        // 2. 根据时间轴协调多个舵机运动
        // 3. 调用舵机驱动API执行动作

        for (const auto& [servo_name, servo_ctrl] : msg.data)
        {
            ESP_LOGI(TAG, "  舵机: %s, 部位: %s, 起始时间: %s, 角度: %d°, 持续时间: %dms",
                     servo_name.c_str(), servo_ctrl.move_part.c_str(),
                     servo_ctrl.start_time.c_str(), servo_ctrl.angle, servo_ctrl.duration);
        }
    }

    void App::handlePlayMessage(const chatbot::message::PlayMessage& msg)
    {
        ESP_LOGI(TAG, "收到音频播放请求消息");

        // TODO: 实现音频播放逻辑
        // 这里需要：
        // 1. 准备接收音频数据流
        // 2. 设置音频播放状态
        // 3. 等待接收音频数据并播放

        (void)msg; // 暂时避免未使用参数警告
    }

    void App::handleEmotionMessage(const chatbot::message::EmotionMessage& msg)
    {
        ESP_LOGI(TAG, "收到情绪反馈消息 - 情绪代码: %s", msg.data.code.c_str());

        // 根据情绪代码处理不同的情绪反馈
        const std::map<std::string, std::string> emotion_map = {
            {"0", "开心的"}, {"1", "伤心的"}, {"2", "生气的"}, {"3", "平淡的"},
            {"4", "恐惧的"}, {"5", "惊讶的"}, {"6", "未知的"}};

        auto        it           = emotion_map.find(msg.data.code);
        std::string emotion_desc = (it != emotion_map.end()) ? it->second : "未知情绪";

        ESP_LOGI(TAG, "机器人情绪反馈: %s", emotion_desc.c_str());

        // TODO: 根据情绪反馈调整机器人的行为或表情
        // 例如：开心时播放欢快音乐，生气时改变LED颜色等
    }

    void App::handleErrorMessage(const chatbot::message::ErrorMessage& msg)
    {
        ESP_LOGE(TAG, "收到错误消息 - 代码: %d, 消息: %s", msg.data.code, msg.data.message.c_str());

        // 根据错误代码处理不同类型的错误
        switch (msg.data.code)
        {
        case 1000: // 示例错误码
            ESP_LOGE(TAG, "处理错误码1000的逻辑");
            break;
        default:
            ESP_LOGE(TAG, "未知错误码: %d", msg.data.code);
            break;
        }
    }

} // namespace app
