#include "logic.h"
#include "esp_log.h"

int calculateControl(int touch_status, int pressure_status, int gyro_status, int light_status,int voice_status,
                     const logic_config_t& config, int& zero_streak, const char* tag)
{
    int value = ((touch_status & 0x1) << 3) | ((pressure_status & 0x1) << 2) |
                ((gyro_status & 0x1) << 1) | ((light_status & 0x1) << 0) | ((voice_status & 0x1) << 0);

    uint8_t control = 0;
    if (value & config.touch)
    {
        control |= (1 << 4); // 对应 touch
    }
    if (value & config.pressure)
    {
        control |= (1 << 3); // 对应 pressure
    }
    if (value & config.gyro)
    {
        control |= (1 << 2); // 对应 gyro
    }
    if (value & config.sitive)
    {
        control |= (1 << 1); // 对应 sitive
    }
    if (value & config.camera)
    {
        control |= (1 << 0); // 对应 camera
    }

    if (control == 0)
    {
        zero_streak = zero_streak + 1;
        if (zero_streak >= 5)
        {
            ESP_LOGI(tag, "calculateControl(%d,%d,%d,%d,%d) -> control: 0 (已连续 5 次为 0)",
                     touch_status, pressure_status, gyro_status, light_status, voice_status);
            zero_streak = 0; // 清零重新计数
            return 0;
        }
        // 未满 5 次，不输出，返回 0 但不打印
        return 0;
    }
    else
    {
        zero_streak = 0; // 重置连续 0 计数
        ESP_LOGI(tag, "calculateControl(%d,%d,%d,%d,%d) -> control: %d", touch_status, pressure_status,
                 gyro_status, light_status, voice_status, control);
        // 将 control 转化为二进制，分别对应触摸、压力、陀螺仪、光敏、摄像头
        int touch_result    = (control >> 4) & 0x1; // bit4: 触摸
        int pressure_result = (control >> 3) & 0x1; // bit3: 压力
        int gyro_result     = (control >> 2) & 0x1; // bit2: 陀螺仪
        int light_result    = (control >> 1) & 0x1; // bit1: 光敏
        int camera_result   = (control >> 0) & 0x1; // bit0: 摄像头

        ESP_LOGI(tag, "模块状态 - 触摸:%d 压力:%d 陀螺仪:%d 光敏:%d 摄像头:%d", touch_result,
                 pressure_result, gyro_result, light_result, camera_result);
        return control;
    }
}
