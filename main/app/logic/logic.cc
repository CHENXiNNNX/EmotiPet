#include "logic.h"
#include "esp_log.h"

int week(int a, int b, int c, int d, const logic_config_t& config, int& zero_streak, const char* tag)
{
    int value = ((a & 0x1) << 3) |
                ((b & 0x1) << 2) |
                ((c & 0x1) << 1) |
                ((d & 0x1) << 0);

    uint8_t control = 0;
    // 假设 control 的 bit0~bit4 分别对应 touch/pressure/gyro/sitive/extra
    if (value & config.touch) {
        control |= (1 << 4);  // 对应 touch
    }
    if (value & config.pressure) {
        control |= (1 << 3);  // 对应 pressure
    }
    if (value & config.gyro) {
        control |= (1 << 2);  // 对应 gyro
    }
    if (value & config.sitive) {
        control |= (1 << 1);  // 对应 sitive
    }
    if (value & config.extra) {
        control |= (1 << 0);  // 对应 extra
    }

    if (control == 0)
    {
        zero_streak = zero_streak + 1;
        if (zero_streak >= 5)
        {
            ESP_LOGI(tag, "week(%d,%d,%d,%d) -> control: 0 (已连续 5 次为 0)", a, b, c, d);
            zero_streak = 0; // 清零重新计数
            return 0;
        }
        // 未满 5 次，不输出，返回 0 但不打印
        return 0;
    }
    else
    {
        zero_streak = 0; // 重置连续 0 计数
        ESP_LOGI(tag, "week(%d,%d,%d,%d) -> control: %d", a, b, c, d, control);
        return control;
    }
}

