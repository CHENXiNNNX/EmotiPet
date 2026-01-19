#include "app.hpp"

#include "config/config.hpp"
#include "network/wifi/wifi.hpp"
#include "system/event/event.hpp"
#include "system/info/info.hpp"
#include "system/task/task.hpp"
#include "esp_log.h"
#include "nvs_flash.h"
#include "logic/logic.h"
#include "device/led/led.hpp"

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

        // 初始化 APDS-9930 传感器
        if (!initAPDS9930(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "APDS-9930 初始化失败（可能未连接）");
        }

        // 初始化 MPR121 触摸传感器
        if (!initMPR121(getI2CBusHandle()))
        {
            ESP_LOGW(TAG, "MPR121 触摸传感器初始化失败（可能未连接）");

        }

        // 初始化 M0404 压力传感器
        // 传感器TX -> ESP32 RX (GPIO1), 传感器RX -> ESP32 TX (GPIO2)
        if (!initM0404(UART_NUM_2, GPIO_NUM_2, GPIO_NUM_1, 115200))
        {
            ESP_LOGW(TAG, "M0404 压力传感器初始化失败（可能未连接）");
        }

        // 初始化 LED
        if (!initLED())
        {
            ESP_LOGW(TAG, "LED 初始化失败（可能未连接）");
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
            apds9930_.setLightStatusCallback(
                [](int light_status)
                {
                    const char* status_str = (light_status == 1) ? "亮" : "灭";
                    ESP_LOGI(TAG, "环境光状态回调: %d (%s)", light_status, status_str);
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
            // 执行零点标定（将当前状态作为零点）
            // 采集100次数据，每次间隔100ms，取平均值作为零点
            ESP_LOGI(TAG, "开始执行M0404零点标定...");
            if (m0404_.calibrateZeroPoint(100, 100))
            {
                ESP_LOGI(TAG, "M0404零点标定完成");
            }
            else
            {
                ESP_LOGW(TAG, "M0404零点标定失败");
            }

            m0404_.setPressureStatusCallback(
                [](int pressure_status)
                {
                    // 只在有压力时才输出日志
                    // if (pressure_status == 1)
                    // {
                    //     ESP_LOGI(TAG, "压力状态回调: %d (有压力)", pressure_status);
                    // }
                    // int status = m0404_.getCurrentPressureStatus();
                });

            // 启用默认触摸状态日志输出
            m0404_.enableDefaultTouchStateLogging();

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
            mpr121_.setTouchStatusCallback(
                [](int touch_status)
                {
                    // 只在触摸时才输出日志
                    // if (touch_status == 1)
                    // {
                    //     ESP_LOGI(TAG, "触摸状态回调: %d (触摸)", touch_status);
                    // }
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
            qmi8658a_.setMotionStatusCallback(
                [this](int motion_status)
                {
                    // 只在"动了"时才输出日志
                    //if (motion_status == 1)
                    //{
                    //    ESP_LOGI(TAG, "运动状态回调: %d (动了)", motion_status);
                    //}
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

        // 初始化呼吸灯
        initBreathingLED();

        while (true)
        {
            app::sys::task::TaskManager::delayMs(5000); // 5秒间隔

            // 更新呼吸灯颜色（每次使用下一个颜色）
            updateBreathingLEDColor();

            // 打印系统信息
            ESP_LOGI(TAG, "================= 系统信息 ===================");
            logMemoryInfo();
            logWiFiInfo();
            logQMI8658AInfo();
            // 获取 M0404 压力传感器的16个压力值（从后台数据采集任务获取最新数据）
            //app::device::pressure::PressureData data;
            //if (m0404_.getLatestPressureData(data)) {
            //    ESP_LOGI(TAG, "M0404 压力值 (16个传感器):");
            //    for (size_t i = 0; i < 16; i++) {
            //        uint16_t raw_value = data.pressures[i];  // 原始值范围：0-65535
            //        ESP_LOGI(TAG, "  传感器[%2lu]: %5u", (unsigned long)i, raw_value);
            //    }
            //}
            week(mpr121_.getCurrentTouchStatus(),
            m0404_.getCurrentPressureStatus(),
            qmi8658a_.getCurrentMotionStatus(),
            apds9930_.getCurrentLightStatus(),
            config, zero_streak, TAG);
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

    bool App::initLED()
    {
        // LED 初始化实际上在使用时才会初始化 RMT 通道
        // 这里只是验证配置是否正确
        ESP_LOGI(TAG, "LED 初始化完成，GPIO: %d", app::config::LED_GPIO);
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
        // 暂时关闭陀螺仪输出信息
        // device::qmi8658a::SensorData data;
        // // 读取传感器数据并计算姿态角
        // if (qmi8658a_.read(data, device::qmi8658a::READ_ALL))  
        // {
        //     ESP_LOGI(TAG, "QMI8658A 加速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f m/s²", data.accel_x,
        //              data.accel_y, data.accel_z);
        //     ESP_LOGI(TAG, "QMI8658A 角速度: X=%+7.2f  Y=%+7.2f  Z=%+7.2f rad/s", data.gyro_x,
        //              data.gyro_y, data.gyro_z);
        //     ESP_LOGI(TAG, "QMI8658A 姿态:   Roll=%+7.1f°  Pitch=%+7.1f°  Yaw=%+7.1f°", data.angle_x,
        //              data.angle_y, data.angle_z);
        // }
    }

    void App::blinkLEDWithNextColor()
    {
        // 定义颜色数组（彩虹色序列）
        static const app::device::led::Color color_sequence[] = {
            app::device::led::Color(255, 0, 0),     // 红色
            app::device::led::Color(255, 127, 0),   // 橙色
            app::device::led::Color(255, 255, 0),   // 黄色
            app::device::led::Color(0, 255, 0),     // 绿色
            app::device::led::Color(0, 0, 255),     // 蓝色
            app::device::led::Color(75, 0, 130),    // 靛蓝色
            app::device::led::Color(148, 0, 211),   // 紫色
            app::device::led::Color(255, 255, 255), // 白色
        };
        static const size_t color_count = sizeof(color_sequence) / sizeof(color_sequence[0]);
        static size_t color_index = 0;

        // 每5秒亮一次，每次使用不同颜色（两个级联的WS2812同时亮）
        app::device::led::Color current_color = color_sequence[color_index];
        app::device::led::Color colors[2] = {
            current_color, // 第一个LED
            current_color  // 第二个LED
        };
        led_.setColors(app::config::LED_GPIO, colors, 2);
        app::sys::task::TaskManager::delayMs(1000); // 亮1000ms
        // 熄灭两个LED
        colors[0] = app::device::led::Color(0, 0, 0);
        colors[1] = app::device::led::Color(0, 0, 0);
        led_.setColors(app::config::LED_GPIO, colors, 2);

        // 切换到下一个颜色（循环）
        color_index = (color_index + 1) % color_count;
    }

    void App::initBreathingLED()
    {
        // 定义颜色数组（彩虹色序列）
        static const app::device::led::Color color_sequence[] = {
            app::device::led::Color(255, 0, 0),     // 红色
            app::device::led::Color(255, 127, 0),   // 橙色
            app::device::led::Color(255, 255, 0),   // 黄色
            app::device::led::Color(0, 255, 0),     // 绿色
            app::device::led::Color(0, 0, 255),     // 蓝色
            app::device::led::Color(75, 0, 130),    // 靛蓝色
            app::device::led::Color(148, 0, 211),   // 紫色
            app::device::led::Color(255, 255, 255), // 白色
        };

        // 启动呼吸灯（周期2秒，两个LED，使用第一个颜色）
        led_.startBreathing(app::config::LED_GPIO, color_sequence[0], 2000, 2);
    }

    void App::updateBreathingLEDColor()
    {
        // 定义颜色数组（彩虹色序列）
        static const app::device::led::Color color_sequence[] = {
            app::device::led::Color(255, 0, 0),     // 红色
            app::device::led::Color(255, 127, 0),   // 橙色
            app::device::led::Color(255, 255, 0),   // 黄色
            app::device::led::Color(0, 255, 0),     // 绿色
            app::device::led::Color(0, 0, 255),     // 蓝色
            app::device::led::Color(75, 0, 130),    // 靛蓝色
            app::device::led::Color(148, 0, 211),   // 紫色
            app::device::led::Color(255, 255, 255), // 白色
        };
        static const size_t color_count = sizeof(color_sequence) / sizeof(color_sequence[0]);
        static size_t color_index = 0;

        // 切换到下一个颜色（循环）
        color_index = (color_index + 1) % color_count;
        
        // 更新呼吸灯颜色（通过更新颜色实现，无需停止重启）
        led_.updateBreathingColor(color_sequence[color_index]);
    }

} // namespace app