#include "app/app.hpp"
#include "i2c/i2c.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h> // 用于fmax函数

// MC1081 驱动头文件（C 代码，需要 extern "C"）
extern "C"
{
#include "device/mc1081s/mc1081.h"
#include "device/mc1081s/mc1081_reg.h"
#include "device/mc1081s/common.h"
#include "device/mc1081s/i2c_adapter.h"
}

using namespace app::i2c;

static const char* TAG = "MC1081_Test";

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "========== MC1081 驱动测试 ==========");

    // 初始化项目中的 I2C
    ESP_LOGI(TAG, "正在初始化 I2C...");
    I2c    i2c;
    Config i2c_cfg;
    i2c_cfg.port    = I2C_NUM_1;
    i2c_cfg.sda_pin = GPIO_NUM_17;
    i2c_cfg.scl_pin = GPIO_NUM_18;

    if (!i2c.init(&i2c_cfg))
    {
        ESP_LOGE(TAG, "I2C 初始化失败");
        return;
    }
    ESP_LOGI(TAG, "I2C 初始化成功");

    // 等待I2C稳定
    vTaskDelay(pdMS_TO_TICKS(100));

    // 扫描I2C总线上的所有设备
    ESP_LOGI(TAG, "========== 扫描I2C总线上的设备 ==========");
    int device_count = i2c.scan(200);
    ESP_LOGI(TAG, "扫描完成，共找到 %d 个设备", device_count);
    ESP_LOGI(TAG, "==========================================");

    // 获取 I2C 总线句柄
    i2c_master_bus_handle_t i2c_bus_handle = i2c.getBusHandle();
    if (i2c_bus_handle == nullptr)
    {
        ESP_LOGE(TAG, "I2C 总线句柄为空");
        return;
    }

    // 初始化 MC1081（使用项目中的 I2C 总线）
    MC1081_InitStructure cap_init_structure;
    cap_init_structure.MC1081_OSC_MODE = OSC1; // 使用单端模式
    cap_init_structure.MC1081_SHLD_CFG = SHLD_DIS;

    ESP_LOGI(TAG, "正在初始化 MC1081...");
    Cap_Afe_Init_WithHandle(&cap_init_structure, i2c_bus_handle);
    ESP_LOGI(TAG, "MC1081 初始化完成");

    // 测试读取芯片ID以验证I2C通信
    ESP_LOGI(TAG, "测试读取MC1081芯片ID...");
    {
        unsigned char chip_id_msb = 0, chip_id_lsb = 0;
        unsigned char ret_msb = CAP_AFE_Receive(0x70, CHIP_ID_MSB, &chip_id_msb, 1);
        unsigned char ret_lsb = CAP_AFE_Receive(0x70, CHIP_ID_LSB, &chip_id_lsb, 1);
        if (ret_msb == GPIOI2C_XFER_LASTNACK && ret_lsb == GPIOI2C_XFER_LASTNACK)
        {
            ESP_LOGI(TAG, "芯片ID读取成功: MSB=0x%02X, LSB=0x%02X (期望: 0x10 0x81)", chip_id_msb,
                     chip_id_lsb);
            if (chip_id_msb == 0x10 && chip_id_lsb == 0x81)
            {
                ESP_LOGI(TAG, "✓ 芯片ID验证通过，MC1081通信正常");
            }
            else
            {
                ESP_LOGW(TAG, "✗ 芯片ID不匹配，可能不是MC1081或通信有问题");
            }
        }
        else
        {
            ESP_LOGE(TAG, "芯片ID读取失败: ret_msb=0x%02X, ret_lsb=0x%02X", ret_msb, ret_lsb);
        }
    }

    // 注意：Cap_Afe_Init 已经自动配置了所有通道（包括参考通道）
    // 通道1对应索引0，参考通道会自动启用用于计算电容值
    ESP_LOGI(TAG, "通道配置: 所有通道已启用（包括参考通道）");

    // 等待初始化稳定
    vTaskDelay(pdMS_TO_TICKS(200));

    ESP_LOGI(TAG, "开始实时监测通道1（压力）、通道2（触摸）和通道5（压力）电容值...");
    ESP_LOGI(TAG, "只在按下或松开时显示数据");
    ESP_LOGI(TAG, "按 Ctrl+C 停止监测");

    CAP_AFE_SingleEnded cap_structure;

    // 状态记录：记录上一次的状态
    bool prev_ch1_pressed = false; // 通道1（压力）上一次是否按下
    bool prev_ch2_pressed = false; // 通道2（触摸）上一次是否按下
    bool prev_ch5_pressed = false; // 通道5（压力）上一次是否按下

    // 阈值设置：根据实际测试调整
    // 如果电容值超过阈值，认为按下；低于阈值，认为松开
    // 使用相对阈值：相对于基准值的百分比变化，更敏感
    float ch1_threshold_percent  = 5.0f; // 通道1（压力）阈值：基准值的5%（可根据实际情况调整）
    float ch2_threshold_percent  = 5.0f; // 通道2（触摸）阈值：基准值的5%（可根据实际情况调整）
    float ch5_threshold_percent  = 5.0f; // 通道5（压力）阈值：基准值的5%（可根据实际情况调整）
    float ch1_threshold_absolute = 1.0f; // 通道1（压力）绝对阈值：1.0 pF（最小变化量）
    float ch2_threshold_absolute = 1.0f; // 通道2（触摸）绝对阈值：1.0 pF（最小变化量）
    float ch5_threshold_absolute = 2.0f; // 通道5（压力）绝对阈值：1.0 pF（最小变化量）
    // 先读取几次数据，用于校准基准值
    ESP_LOGI(TAG, "正在校准基准值...");
    float     ch1_baseline     = 0.0f;
    float     ch2_baseline     = 0.0f;
    float     ch5_baseline     = 0.0f;
    int       baseline_count   = 0;
    const int baseline_samples = 5; // 采样5次计算基准值

    for (int i = 0; i < baseline_samples; i++)
    {
        int ret = MC1081_OSC1_Measure(&cap_structure, &cap_init_structure);
        if (ret == 1)
        {
            if (cap_structure.data_ch[1] > 0 && cap_structure.data_ch[1] < 65535 &&
                cap_structure.data_ref > 0)
            {
                ch1_baseline += cap_structure.cap_ch[1];
                baseline_count++;
            }
            if (cap_structure.data_ch[2] > 0 && cap_structure.data_ch[2] < 65535 &&
                cap_structure.data_ref > 0)
            {
                ch2_baseline += cap_structure.cap_ch[2];
            }
            if (cap_structure.data_ch[5] > 0 && cap_structure.data_ch[5] < 65535 &&
                cap_structure.data_ref > 0)
            {
                ch5_baseline += cap_structure.cap_ch[5];
            }
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    if (baseline_count > 0)
    {
        ch1_baseline /= baseline_count;
        ch2_baseline /= baseline_count;
        ch5_baseline /= baseline_count;
        ESP_LOGI(TAG, "基准值校准完成: 通道1=%.3f pF, 通道2=%.3f pF, 通道5=%.3f pF", ch1_baseline,
                 ch2_baseline, ch5_baseline);
    }
    else
    {
        ESP_LOGW(TAG, "基准值校准失败，使用默认阈值");
        ch1_baseline = 0.0f;
        ch2_baseline = 0.0f;
        ch5_baseline = 0.0f;
    }

    // 循环读取通道1和通道2的电容值
    while (1)
    {
        int ret = MC1081_OSC1_Measure(&cap_structure, &cap_init_structure);
        if (ret == 1)
        {
            // 通道1（压力）对应索引1，通道2（触摸）对应索引2
            unsigned short data_ch1 = cap_structure.data_ch[1]; // 通道1（压力）原始数据
            unsigned short data_ch2 = cap_structure.data_ch[2]; // 通道2（触摸）原始数据
            unsigned short data_ch5 = cap_structure.data_ch[5]; // 通道5（压力）原始数据
            unsigned short data_ref = cap_structure.data_ref;   // 参考通道原始数据

            float cap_ch1  = cap_structure.cap_ch[1];  // 通道1（压力）电容值
            float cap_ch2  = cap_structure.cap_ch[2];  // 通道2（触摸）电容值
            float cap_ch5  = cap_structure.cap_ch[5];  // 通道5（压力）电容值
            float freq_ch1 = cap_structure.freq_ch[1]; // 通道1（压力）频率
            float freq_ch2 = cap_structure.freq_ch[2]; // 通道2（触摸）频率
            float freq_ch5 = cap_structure.freq_ch[5]; // 通道5（压力）频率

            // 检查数据有效性
            bool ch1_valid = (data_ch1 > 0 && data_ch1 < 65535 && data_ref > 0);
            bool ch2_valid = (data_ch2 > 0 && data_ch2 < 65535 && data_ref > 0);
            bool ch5_valid = (data_ch5 > 0 && data_ch5 < 65535 && data_ref > 0);

            // 判断当前状态：使用相对阈值和绝对阈值中较大的一个
            // 相对阈值：基准值的百分比变化（适用于基准值较大的情况）
            // 绝对阈值：最小变化量（适用于基准值较小的情况）
            float ch1_threshold =
                ch1_baseline > 0
                    ? fmax(ch1_baseline * ch1_threshold_percent / 100.0f, ch1_threshold_absolute)
                    : ch1_threshold_absolute;
            float ch2_threshold =
                ch2_baseline > 0
                    ? fmax(ch2_baseline * ch2_threshold_percent / 100.0f, ch2_threshold_absolute)
                    : ch2_threshold_absolute;
            float ch5_threshold =
                ch5_baseline > 0
                    ? fmax(ch5_baseline * ch5_threshold_percent / 100.0f, ch5_threshold_absolute)
                    : ch5_threshold_absolute;

            bool ch1_pressed = ch1_valid && (cap_ch1 > (ch1_baseline + ch1_threshold));
            bool ch2_pressed = ch2_valid && (cap_ch2 > (ch2_baseline + ch2_threshold));
            bool ch5_pressed = ch5_valid && (cap_ch5 > (ch5_baseline + ch5_threshold));

            // 检测状态变化
            bool ch1_state_changed = (ch1_pressed != prev_ch1_pressed);
            bool ch2_state_changed = (ch2_pressed != prev_ch2_pressed);
            bool ch5_state_changed = (ch5_pressed != prev_ch5_pressed);

            // 调试信息：显示阈值比较（仅在状态变化或按下时显示，避免输出过多）
            if ((ch1_state_changed || ch1_pressed) && ch1_valid)
            {
                float diff_ch1 = cap_ch1 - ch1_baseline;
                ESP_LOGI(
                    TAG,
                    "[调试] CH1: 电容=%.3f pF, 基准=%.3f pF, 差值=%.3f pF, 阈值=%.3f pF, 状态=%s",
                    cap_ch1, ch1_baseline, diff_ch1, ch1_threshold, ch1_pressed ? "按下" : "松开");
            }
            if ((ch2_state_changed || ch2_pressed) && ch2_valid)
            {
                float diff_ch2 = cap_ch2 - ch2_baseline;
                ESP_LOGI(
                    TAG,
                    "[调试] CH2: 电容=%.3f pF, 基准=%.3f pF, 差值=%.3f pF, 阈值=%.3f pF, 状态=%s",
                    cap_ch2, ch2_baseline, diff_ch2, ch2_threshold, ch2_pressed ? "按下" : "松开");
            }
            if ((ch5_state_changed || ch5_pressed) && ch5_valid)
            {
                float diff_ch5 = cap_ch5 - ch5_baseline;
                ESP_LOGI(
                    TAG,
                    "[调试] CH5: 电容=%.3f pF, 基准=%.3f pF, 差值=%.3f pF, 阈值=%.3f pF, 状态=%s",
                    cap_ch5, ch5_baseline, diff_ch5, ch5_threshold, ch5_pressed ? "按下" : "松开");
            }

            // 显示逻辑：
            // 1. 状态改变时（按下或松开）显示
            // 2. 按着的时候持续显示
            // 3. 松开且状态未改变时不显示
            bool should_display  = false;
            bool is_state_change = (ch1_state_changed || ch2_state_changed || ch5_state_changed);
            bool is_pressed      = (ch1_pressed || ch2_pressed || ch5_pressed);

            if (is_state_change || is_pressed)
            {
                should_display = true;
            }

            if (should_display)
            {
                if (is_state_change)
                {
                    ESP_LOGI(TAG, "========== 状态变化 ==========");
                }
                else
                {
                    ESP_LOGI(TAG, "========== 持续监测 ==========");
                }

                if (ch1_valid)
                {
                    if (ch1_state_changed)
                    {
                        if (ch1_pressed)
                        {
                            ESP_LOGI(
                                TAG,
                                "  [按下] 通道1(压力): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                cap_ch1, freq_ch1, data_ch1);
                        }
                        else
                        {
                            ESP_LOGI(
                                TAG,
                                "  [松开] 通道1(压力): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                cap_ch1, freq_ch1, data_ch1);
                        }
                    }
                    else if (ch1_pressed)
                    {
                        // 按着的时候持续显示
                        ESP_LOGI(TAG,
                                 "  [按住] 通道1(压力): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                 cap_ch1, freq_ch1, data_ch1);
                    }
                }

                if (ch2_valid)
                {
                    if (ch2_state_changed)
                    {
                        if (ch2_pressed)
                        {
                            ESP_LOGI(
                                TAG,
                                "  [按下] 通道2(触摸): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                cap_ch2, freq_ch2, data_ch2);
                        }
                        else
                        {
                            ESP_LOGI(
                                TAG,
                                "  [松开] 通道2(触摸): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                cap_ch2, freq_ch2, data_ch2);
                        }
                    }
                    else if (ch2_pressed)
                    {
                        // 按着的时候持续显示
                        ESP_LOGI(TAG,
                                 "  [按住] 通道2(触摸): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                 cap_ch2, freq_ch2, data_ch2);
                    }
                }

                if (ch5_valid)
                {
                    if (ch5_state_changed)
                    {
                        if (ch5_pressed)
                        {
                            ESP_LOGI(
                                TAG,
                                "  [按下] 通道5(压力): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                cap_ch5, freq_ch5, data_ch5);
                        }
                        else
                        {
                            ESP_LOGI(
                                TAG,
                                "  [松开] 通道5(压力): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                cap_ch5, freq_ch5, data_ch5);
                        }
                    }
                    else if (ch5_pressed)
                    {
                        // 按着的时候持续显示
                        ESP_LOGI(TAG,
                                 "  [按住] 通道5(压力): %.3f pF | 频率: %.3f MHz | 原始数据: %d",
                                 cap_ch5, freq_ch5, data_ch5);
                    }
                }

                ESP_LOGI(TAG, "  参考通道: %.3f MHz | 原始数据: %d", cap_structure.freq_ref,
                         data_ref);
            }

            // 更新上一次的状态
            prev_ch1_pressed = ch1_pressed;
            prev_ch2_pressed = ch2_pressed;
            prev_ch5_pressed = ch5_pressed;
        }

        // 延时100ms，提高响应速度
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
