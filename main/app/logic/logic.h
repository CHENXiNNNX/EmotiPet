#pragma once
#include <cstdint>

// 配置表
// 每个参数占 4bit，取值范围 0b0000 ~ 0b1111
typedef struct
{
    uint8_t touch : 5;    // 4-bit 触摸配置
    uint8_t pressure : 5; // 4-bit 压力配置
    uint8_t gyro : 5;     // 4-bit 陀螺仪配置
    uint8_t sitive : 5;   // 4-bit 灵敏度配置
    uint8_t camera : 5;   // 4-bit 预留/额外配置
} logic_config_t;

// 初始化默认配置
inline logic_config_t initLogicConfig()
{
    logic_config_t cfg{};
    cfg.touch    = 0b00100;
    cfg.pressure = 0b11111;
    cfg.gyro     = 0b11001;
    cfg.sitive   = 0b10101;
    cfg.camera   = 0b01110;
    return cfg;
}

// 函数声明：计算 control 值
// touch_status: 触摸状态 (0或1)
// pressure_status: 压力状态 (0或1)
// gyro_status: 陀螺仪状态 (0或1)
// light_status: 光敏状态 (0或1)
// config: 配置参数
// zero_streak: 连续 0 的计数（通过引用传递，可以修改）
// tag: 日志标签
int calculateControl(int touch_status, int pressure_status, int gyro_status, int light_status,
                     int voice_status, const logic_config_t& config, int& zero_streak,
                     const char* tag);