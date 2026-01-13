#pragma once

#include "assets/assets.hpp"
#include "common/i2c/i2c.hpp"
#include "device/qmi8658a/qmi8658a.hpp"
#include "device/APDS-9930/apds9930.hpp"
#include "device/mpr121/mpr121.hpp"
#include "device/pressure/m0404.hpp"
#include "media/audio/audio.hpp"
#include "network/network.hpp"
#include <string>

namespace app
{
    class App
    {
    public:
        App()  = default;
        ~App() = default;

        bool setup();
        void run();

        // 获取 I2C 总线句柄
        i2c_master_bus_handle_t getI2CBusHandle() const;

        // 获取 QMI8658A 实例
        device::qmi8658a::Qmi8658a& getQMI8658A();

        // 获取 Audio 实例
        media::audio::Audio& getAudio();

        // 初始化 APDS-9930
        bool initAPDS9930(i2c_master_bus_handle_t i2c_handle);

        // 获取 APDS-9930 实例
        device::apds9930::APDS9930& getAPDS9930();

        // 开启 APDS-9930 传感器数据获取
        bool startAPDS9930(bool light_interrupts = false, bool proximity_interrupts = false);

        // 关闭 APDS-9930 传感器数据获取
        bool stopAPDS9930();

        // 初始化 MPR121 触摸传感器
        bool initMPR121(i2c_master_bus_handle_t i2c_handle);

        // 获取 MPR121 是否已初始化
        bool isMPR121Initialized() const;

    private:
        // 初始化 NVS
        bool initNVS();

        // 初始化事件系统
        bool initEvent();

        // 初始化 Assets
        bool initAssets();

        // 初始化 I2C
        bool initI2C(gpio_num_t sda, gpio_num_t scl, i2c_port_t port);

        // 初始化QMI8658A
        bool initQMI8658A(i2c_master_bus_handle_t i2c_handle);

        // 初始化音频
        bool initAudio(i2c_master_bus_handle_t i2c_handle, int sample_rate = 16000);

        // 初始化配网管理器
        bool initProvision();

        // 配网状态回调
        void onProvisionStatus(app::network::ProvisionStatus status);

        // 配网完成回调
        void onProvisionComplete(bool success, const char* ssid);

        // 打印内存信息
        void logMemoryInfo();

        // 打印 WiFi 信息
        void logWiFiInfo();

        // 打印 QMI8658A 信息
        void logQMI8658AInfo();

        // 成员变量
        common::i2c::I2c                 i2c_;
        device::qmi8658a::Qmi8658a qmi8658a_;
        media::audio::Audio              audio_;
    };

} // namespace app
