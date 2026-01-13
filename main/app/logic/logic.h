#pragma once
#include <cstdint>

// 配置表
// 每个参数占 4bit，取值范围 0b0000 ~ 0b1111
typedef struct
{
    uint8_t touch    : 4; // 4-bit 触摸配置
    uint8_t pressure : 4; // 4-bit 压力配置
    uint8_t gyro     : 4; // 4-bit 陀螺仪配置
    uint8_t sitive   : 4; // 4-bit 灵敏度配置
    uint8_t extra    : 4; // 4-bit 预留/额外配置
} logic_config_t;

// 初始化默认配置
inline logic_config_t init_logic_config()
{
    logic_config_t cfg{};
    cfg.touch    = 0b0010;
    cfg.pressure = 0b0100;
    cfg.gyro     = 0b1100;
    cfg.sitive   = 0b1010;
    cfg.extra    = 0b0001;
    return cfg;
}

// 函数声明：计算 control 值
// config: 配置参数
// zero_streak: 连续 0 的计数（通过引用传递，可以修改）
// tag: 日志标签
int week(int a, int b, int c, int d, const logic_config_t& config, int& zero_streak, const char* tag);