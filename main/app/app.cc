#include "app.hpp"

#include "config/config.hpp"
#include "logic/logic.h"
#include "network/wifi/wifi.hpp"
#include "system/event/event.hpp"
#include "system/info/info.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include <cstdio>
#include <cstring>

static const char* const TAG = "App";

namespace app
{

    bool App::setup()
    {
        if (!initNVS())
        {
            ESP_LOGE(TAG, "NVS 初始化失败");
            return false;
        }

        if (!initEvent())
        {
            ESP_LOGE(TAG, "事件系统初始化失败");
            return false;
        }

        if (!initI2C(app::config::I2C_SDA, app::config::I2C_SCL, I2C_NUM_1))
        {
            ESP_LOGE(TAG, "I2C 初始化失败");
            return false;
        }

        // 初始化 QMI8658A 陀螺仪（可选，未连接时不退出）
        if (!initQMI8658A(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "QMI8658A 初始化失败（可能未连接）");
            // 不返回 false，允许其他功能继续
        }

        // 初始化 Audio（可选，失败时不退出）
        if (!initAudio(getI2CBusHandle(), 16000))
        {
            ESP_LOGW(TAG, "Audio 初始化失败");
            // 不返回 false，允许其他功能继续
        }

        // 初始化 APDS-9930 传感器（可选，未连接时不退出）
        if (!initAPDS9930(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "APDS-9930 初始化失败（可能未连接）");
            // 不返回 false，允许其他功能继续
        }

        // 初始化 MPR121 触摸传感器（可选，未连接时不退出）
        if (!initMPR121(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "MPR121 触摸传感器初始化失败（可能未连接）");
            // 不返回 false，允许其他功能继续
        }

        // 初始化 M0404 压力传感器（可选，未连接时不退出）
        // 传感器TX -> ESP32 RX (GPIO15), 传感器RX -> ESP32 TX (GPIO7)
        if (!initM0404(UART_NUM_2, GPIO_NUM_7, GPIO_NUM_15, 115200))
        {
            ESP_LOGW(TAG, "M0404 压力传感器初始化失败（可能未连接）");
            // 不返回 false，允许其他功能继续
        }

        if (!initProvision())
        {
            ESP_LOGE(TAG, "配网管理器初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "应用初始化成功");
        return true;
    }

    void App::run()
    {
        auto& provision = app::network::ProvisionManager::getInstance();
        auto& wifi      = app::network::wifi::WiFiManager::getInstance();

        // wifi.removeCredentials("yf");
        if (!wifi.hasSavedCredentials())
        {
            ESP_LOGE(TAG, "没有保存的 WiFi 凭证");
            // 启动配网
            if (!provision.start())
            {
                ESP_LOGE(TAG, "启动配网失败");
                return;
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

                if (!provision.start())
                {
                    ESP_LOGE(TAG, "启动配网失败");
                    return;
                }
            }
        }

        // 启动 APDS-9930 传感器数据获取（非阻塞）
        if (apds9930_.isInitialized())
        {
            // 设置环境光状态回调函数
            // 当环境光 >= 1500 lux 时，light_status = 1（亮）
            // 当环境光 < 1500 lux 时，light_status = 0（灭）
            apds9930_.setLightStatusCallback(
                [](int light_status)
                {
                    const char* status_str = (light_status == 1) ? "亮" : "灭";
                    ESP_LOGI(TAG, "环境光状态回调: %d (%s)", light_status, status_str);
                    // 通过以下方式获取当前光状态：
                    // int status = apds9930_.getCurrentLightStatus();
                });

            // 启动传感器数据获取
            if (apds9930_.start())
            {
                ESP_LOGI(TAG, "APDS-9930 传感器数据获取已开启");
                // 启动后台数据采集任务（每5秒采集一次）
                if (apds9930_.startDataCollection(5000))
                {
                    ESP_LOGI(TAG, "APDS-9930 数据采集任务已启动");
                }
                else
                {
                    ESP_LOGW(TAG, "APDS-9930 数据采集任务启动失败");
                }
            }
            else
            {
                ESP_LOGW(TAG, "APDS-9930 启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "APDS-9930 未初始化，跳过启动");
        }

        // 启动 M0404 压力传感器数据采集（非阻塞）
        if (m0404_.isInitialized())
        {
            // 设置压力状态回调函数
            // 当有压力时，pressure_status = 1（有压力）
            // 当无压力时，pressure_status = 0（无压力）
            m0404_.setPressureStatusCallback(
                [](int pressure_status)
                {
                    // 只在有压力时才输出日志
                    if (pressure_status == 1)
                    {
                        ESP_LOGI(TAG, "压力状态回调: %d (有压力)", pressure_status);
                    }
                    // 通过以下方式获取当前压力状态：
                    // int status = m0404_.getCurrentPressureStatus();
                });

            // 启动后台数据采集任务（每10秒采集一次）
            if (m0404_.startDataCollection(10000))
            {
                ESP_LOGI(TAG, "M0404 压力传感器数据采集任务已启动");
            }
            else
            {
                ESP_LOGW(TAG, "M0404 压力传感器数据采集任务启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "M0404 压力传感器未初始化，跳过启动");
        }

        // 启动 MPR121 触摸传感器数据采集（非阻塞）
        if (mpr121_.isInitialized())
        {
            // 设置触摸状态回调函数
            // 当有触摸时，touch_status = 1（触摸）
            // 当未触摸时，touch_status = 0（未触摸）
            mpr121_.setTouchStatusCallback(
                [](int touch_status)
                {
                    // 只在触摸时才输出日志
                    if (touch_status == 1)
                    {
                        ESP_LOGI(TAG, "触摸状态回调: %d (触摸)", touch_status);
                    }
                    // 通过以下方式获取当前触摸状态：
                    // int status = mpr121_.getCurrentTouchStatus();
                });

            // 启动后台数据采集任务（每100ms采集一次）
            if (mpr121_.startDataCollection(100))
            {
                ESP_LOGI(TAG, "MPR121 触摸传感器数据采集任务已启动");
            }
            else
            {
                ESP_LOGW(TAG, "MPR121 触摸传感器数据采集任务启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "MPR121 触摸传感器未初始化，跳过启动");
        }

        // 启动 QMI8658A 陀螺仪数据采集（非阻塞）
        if (qmi8658a_.isInitialized())
        {
            // 设置运动状态回调函数
            // 当加速度变化很大时，motion_status = 1（动了）
            // 当加速度没变化时，motion_status = 0（没动）
            qmi8658a_.setMotionStatusCallback(
                [this](int motion_status)
                {
                    // 只在"动了"时才输出日志
                    if (motion_status == 1)
                    {
                        ESP_LOGI(TAG, "运动状态回调: %d (动了)", motion_status);
                    }
                    // 通过以下方式获取当前运动状态：
                    // int status = qmi8658a_.getCurrentMotionStatus();
                });

            // 启动后台数据采集任务（每100ms采集一次）
            if (qmi8658a_.startDataCollection(100))
            {
                ESP_LOGI(TAG, "QMI8658A 陀螺仪数据采集任务已启动");
            }
            else
            {
                ESP_LOGW(TAG, "QMI8658A 陀螺仪数据采集任务启动失败");
            }
        }
        else
        {
            ESP_LOGW(TAG, "QMI8658A 陀螺仪未初始化，跳过启动");
        }

        // 初始化配置和变量
        logic_config_t config = init_logic_config();
        static int zero_streak = 0;
        // char line[64];
        // char cmd[16];
        // int  a, b, c, d;

        while (true)
        {
            // 读取串口输入
            // if (fgets(line, sizeof(line), stdin) != nullptr &&
            //     sscanf(line, "%15s %d %d %d %d", cmd, &a, &b, &c, &d) == 5 &&
            //     strcmp(cmd, "week") == 0)
            // {
                
            //     // int control = week(a, b, c, d, config, zero_streak, TAG);
            //     // ESP_LOGI(TAG, "control: %d", control);
            // }

            app::sys::task::TaskManager::delayMs(5000); // 5秒间隔
            ESP_LOGI(TAG, "================= 系统信息 ===================");
            logMemoryInfo();
            logWiFiInfo();
            logQMI8658AInfo();
            week(device::mpr121::MPR121::GetCurrentTouchStatus(),
            device::pressure::M0404::GetCurrentPressureStatus(),
            qmi8658a_.getCurrentMotionStatus(),
            device::apds9930::APDS9930::GetCurrentLightStatus(),
            config, zero_streak, TAG);
            // week(0,0,qmi8658a_.getCurrentMotionStatus(),0,config, zero_streak, TAG);
            // 打印系统信息
            ESP_LOGI(TAG, "==============================================");
        }
    }

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

    bool App::initAssets()
    {
        auto& assets = app::assets::Assets::getInstance();
        if (!assets.init())
        {
            ESP_LOGE(TAG, "Assets 初始化失败");
            return false;
        }
        return true;
    }

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

    i2c_master_bus_handle_t App::getI2CBusHandle() const
    {
        return i2c_.getBusHandle();
    }

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

    device::qmi8658a::Qmi8658a& App::getQMI8658A()
    {
        return qmi8658a_;
    }

    bool App::initAudio(i2c_master_bus_handle_t i2c_handle, int sample_rate)
    {
        if (!i2c_handle)
        {
            ESP_LOGE(TAG, "I2C 句柄无效");
            return false;
        }

        if (sample_rate <= 0)
        {
            ESP_LOGE(TAG, "采样率无效: %d", sample_rate);
            return false;
        }

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
        return true;
    }

    media::audio::Audio& App::getAudio()
    {
        return audio_;
    }

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

    bool App::initMPR121(i2c_master_bus_handle_t i2c_handle)
    {
        if (i2c_handle == nullptr)
        {
            ESP_LOGE(TAG, "I2C 总线句柄为空");
            return false;
        }

        // 使用默认 I2C 地址 0x5A，IRQ 引脚不使用（GPIO_NUM_NC）
        if (!mpr121_.init(i2c_handle, device::mpr121::MPR121_I2C_ADDR, GPIO_NUM_NC))
        {
            ESP_LOGE(TAG, "MPR121 初始化失败");
            return false;
        }

        ESP_LOGI(TAG, "MPR121 初始化成功");
        return true;
    }

    bool App::isMPR121Initialized() const
    {
        return mpr121_.isInitialized();
    }

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
            return false;
        }

        provision.setStatusCallback([this](app::network::ProvisionStatus status)
                                    { onProvisionStatus(status); });

        provision.setCompleteCallback([this](bool success, const char* ssid)
                                      { onProvisionComplete(success, ssid); });

        return true;
    }

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
        if (!success)
        {
            ESP_LOGE(TAG, "配网失败: SSID=%s", ssid ? ssid : "未知");
        }
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

} // namespace app