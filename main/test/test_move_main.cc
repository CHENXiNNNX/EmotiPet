#include "app/app.hpp"
#include "app/move/move.hpp"
#include "esp_log.h"
#include "freertos/task.h"

static const char* const TAG = "Main";

/**
 * @brief 示例：从JSON字符串执行舵机运动
 * @param json_str JSON格式的运动数据字符串
 */
static void executeMovementFromJson(const std::string& json_str)
{
    std::vector<app::move::ServoMotion> motions;
    
    if (!app::move::parseMovementJson(json_str, motions))
    {
        ESP_LOGE(TAG, "解析运动JSON失败");
        return;
    }

    ESP_LOGI(TAG, "开始执行运动序列，共 %zu 个动作", motions.size());
    app::move::executeMovements(motions, 20); // 20ms更新间隔，可调整
}

extern "C" void app_main(void)
{
    app::App app;
    if (!app.setup())
    {
        ESP_LOGE(TAG, "应用初始化失败，程序退出");
        return;
    }

    // 初始化 PCA9685 舵机驱动
    auto bus_handle = app.getI2CBusHandle();
    if (bus_handle == nullptr)
    {
        ESP_LOGE(TAG, "I2C 总线句柄为空，无法初始化 PCA9685");
        return;
    }

    if (!app::move::PCA9685::init(bus_handle))
    {
        ESP_LOGE(TAG, "PCA9685 初始化失败");
        return;
    }

    ESP_LOGI(TAG, "PCA9685 初始化成功，开始测试JSON运动控制");

    // 等待初始化完成
    vTaskDelay(pdMS_TO_TICKS(500));
    
    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "开始执行JSON运动控制测试");
    ESP_LOGI(TAG, "servo_01, servo_02, ... 代表第1步、第2步等");
    ESP_LOGI(TAG, "支持data中任意数量的步数（servo_XX）");
    ESP_LOGI(TAG, "========================================");

    // 循环测试：可以修改JSON数据来测试不同的运动序列
    // data中可以包含任意数量的servo_XX配置，系统会自动解析所有配置
    while (true)
    {

        // b2 电机快转和慢转测试序列
        // angle 映射：0=最大反转(-100%), 90=停止(0%), 180=最大正转(+100%)
        // 慢转：angle=120 (约+33%速度) 或 angle=60 (约-33%速度)
        // 快转：angle=170 (约+89%速度) 或 angle=10 (约-89%速度)
        std::string test_json_4 = R"({
            "type": "mov_info",
            "from": "server",
            "to": "xxx",
            "timestamp": "2025-03-12T19:00:00Z",
            "data": {
                "servo_04": { "move_part": "h1", "start_time": "3500", "angle": 45, "duration": 1000 },
                "servo_05": { "move_part": "h1", "start_time": "4500", "angle": 135,  "duration": 1000 },
                "servo_06": { "move_part": "h1", "start_time": "5500", "angle": 90,  "duration": 1000 },
                "servo_07": { "move_part": "h2", "start_time": "6500", "angle": 45,  "duration": 1000 },
                "servo_08": { "move_part": "h2", "start_time": "7500", "angle": 135, "duration": 1000 },
                "servo_09": { "move_part": "h2", "start_time": "8500", "angle": 90, "duration": 1000 },
                "servo_10": { "move_part": "h2", "start_time": "9500", "angle": 60, "duration": 1000 },
                "servo_11": { "move_part": "b1", "start_time": "10500", "angle": 120, "duration": 1000 },
                "servo_12": { "move_part": "b1", "start_time": "11500", "angle": 90, "duration": 1000 },
                "servo_13": { "move_part": "b2", "start_time": "12500", "angle": 120, "duration": 1000 }
            }
        })";

        ESP_LOGI(TAG, "执行测试序列：b2 电机快转和慢转测试");
        executeMovementFromJson(test_json_4);
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
}
