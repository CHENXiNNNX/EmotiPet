#pragma once

#include "network/network.hpp"
#include <string>

namespace app
{
    class App
    {
    public:
        App()  = default;
        ~App() = default;

        void setup();
        void run();

    private:
        // 初始化 NVS
        bool initNVS();

        // 初始化事件系统
        bool initEvent();

        // 初始化配网管理器
        bool initProvision();

        // 配网状态回调
        void onProvisionStatus(app::network::ProvisionStatus status);

        // 配网完成回调
        void onProvisionComplete(bool success, const char* ssid);

        // 打印系统状态
        void logSystemStatus();
    };

} // namespace app