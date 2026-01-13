#pragma once

#include <cstdint>
#include <string>
#include <memory>
#include "device/qmi8658a/qmi8658a.hpp"
#include "device/APDS-9930/apds9930.hpp"
#include "device/mpr121/mpr121.hpp"
#include "device/pressure/m0404.hpp"
#include "system/task/task.hpp"

namespace app
{
    namespace data
    {
        namespace upload
        {
            /**
             * @brief 传感器数据上传配置
             */
            struct Config
            {
                std::string server_url;      // 服务器 URL，例如 "http://example.com/api/sensor"
                uint32_t    upload_interval_ms = 10000; // 上传间隔（毫秒），默认 10 秒
                bool        enable_qmi8658a    = true;  // 是否上传 QMI8658A 数据
                bool        enable_apds9930     = true;  // 是否上传 APDS9930 数据
                bool        enable_mpr121      = true;  // 是否上传 MPR121 数据
                bool        enable_m0404       = true;  // 是否上传 M0404 数据
                int32_t     timeout_ms          = 5000;  // HTTP 请求超时时间（毫秒）
            };

            /**
             * @brief 传感器数据上传管理器
             * 
             * 负责收集所有传感器数据，格式化为 JSON，并上传到服务器
             */
            class SensorUploader
            {
            public:
                /**
                 * @brief 获取单例实例
                 * @return SensorUploader 实例的引用
                 */
                static SensorUploader& getInstance()
                {
                    static SensorUploader instance;
                    return instance;
                }

                // 禁止拷贝和赋值
                SensorUploader(const SensorUploader&)            = delete;
                SensorUploader& operator=(const SensorUploader&) = delete;

                /**
                 * @brief 初始化上传器
                 * @param config 配置信息
                 * @return true 成功, false 失败
                 */
                bool init(const Config& config);

                /**
                 * @brief 反初始化
                 */
                void deinit();

                /**
                 * @brief 启动自动上传任务
                 * @return true 成功, false 失败
                 */
                bool start();

                /**
                 * @brief 停止自动上传任务
                 * @return true 成功, false 失败
                 */
                bool stop();

                /**
                 * @brief 手动上传一次数据
                 * @return true 成功, false 失败
                 */
                bool uploadOnce();

                /**
                 * @brief 检查是否已初始化
                 */
                bool isInitialized() const
                {
                    return initialized_;
                }

                /**
                 * @brief 检查是否正在运行
                 */
                bool isRunning() const
                {
                    return running_;
                }

                /**
                 * @brief 更新配置
                 * @param config 新配置
                 */
                void updateConfig(const Config& config);

            private:
                SensorUploader() = default;
                ~SensorUploader();

                /**
                 * @brief 收集所有传感器数据
                 * @param qmi8658a_data QMI8658A 数据（输出）
                 * @param apds9930_data APDS9930 数据（输出）
                 * @param mpr121_data MPR121 数据（输出）
                 * @param m0404_data M0404 数据（输出）
                 * @return true 至少有一个传感器数据可用
                 */
                bool collectSensorData(device::qmi8658a::SensorData& qmi8658a_data,
                                       float&                        apds9930_light,
                                       uint16_t&                     apds9930_proximity,
                                       uint16_t&                    mpr121_touches,
                                       device::pressure::PressureData& m0404_data);

                /**
                 * @brief 将传感器数据格式化为 JSON 字符串
                 * @param qmi8658a_data QMI8658A 数据
                 * @param apds9930_light APDS9930 环境光值
                 * @param apds9930_proximity APDS9930 接近值
                 * @param mpr121_touches MPR121 触摸状态位掩码
                 * @param m0404_data M0404 压力数据
                 * @return JSON 字符串，失败返回空字符串
                 */
                std::string formatJson(const device::qmi8658a::SensorData& qmi8658a_data,
                                       float                               apds9930_light,
                                       uint16_t                            apds9930_proximity,
                                       uint16_t                            mpr121_touches,
                                       const device::pressure::PressureData& m0404_data);

                /**
                 * @brief 上传 JSON 数据到服务器
                 * @param json_data JSON 字符串
                 * @return true 成功, false 失败
                 */
                bool uploadToServer(const std::string& json_data);

                /**
                 * @brief 自动上传任务函数
                 * @param param 任务参数
                 */
                void uploadTaskFunction(void* param);

                Config config_;
                bool   initialized_ = false;
                bool   running_    = false;

                // 上传任务
                std::unique_ptr<app::sys::task::Task> upload_task_;
            };

        } // namespace upload
    }     // namespace data
} // namespace app

