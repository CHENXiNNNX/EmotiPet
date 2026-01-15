# MC1081 驱动移植说明

## 概述

本目录包含已移植到 ESP-IDF 平台的 MC1081 系列驱动。驱动已适配 ESP-IDF 的 I2C 接口。

## 文件结构

- `mc1081.h/c` - MC1081 主驱动文件
- `mc1081_reg.h` - MC1081 寄存器定义
- `common.h/c` - 通用函数和初始化
- `i2c_adapter.h/c` - ESP-IDF I2C 适配层

## 使用方法

### 1. 初始化

```c
#include "mc1081.h"
#include "mc1081_reg.h"

// 初始化结构体
MC1081_InitStructure cap_init_structure;
cap_init_structure.MC1081_OSC_MODE = OSC1;  // 或 OSC2
cap_init_structure.MC1081_SHLD_CFG = SHLD_DIS;

// 初始化 MC1081
Cap_Afe_Init(&cap_init_structure);
```

### 2. 温度测量

```c
float temperature;
if (MC1081_T_Measure(&temperature) == 1) {
    printf("Temperature: %.1f C\n", temperature);
}
```

### 3. 单端模式测量 (OSC1)

```c
CAP_AFE_SingleEnded cap_structure;
MC1081_InitStructure cap_init_structure;

cap_init_structure.MC1081_OSC_MODE = OSC1;
cap_init_structure.MC1081_SHLD_CFG = SHLD_DIS;
Cap_Afe_Init(&cap_init_structure);

if (MC1081_OSC1_Measure(&cap_structure, &cap_init_structure) == 1) {
    // 读取通道数据
    for (int i = 0; i < 10; i++) {
        printf("Channel %d: %.3f pF\n", i, cap_structure.cap_ch[i]);
    }
}
```

### 4. 差分模式测量 (OSC2)

```c
CAP_AFE_DoubleEnded cap_structure;
MC1081_InitStructure cap_init_structure;

cap_init_structure.MC1081_OSC_MODE = OSC2;
Cap_Afe_Init(&cap_init_structure);

if (MC1081_OSC2_Measure(&cap_structure, &cap_init_structure) == 1) {
    // 读取通道数据
    for (int i = 0; i < 5; i++) {
        printf("Channel %d: %.3f pF\n", i, cap_structure.cap_ch[i]);
    }
}
```

## I2C 配置

驱动使用以下 I2C 配置（在 `i2c_adapter.c` 中定义）：
- I2C 地址: 0x70
- I2C 端口: I2C_NUM_1
- SDA 引脚: GPIO_NUM_17
- SCL 引脚: GPIO_NUM_18
- 时钟频率: 100kHz

如需修改 I2C 配置，请编辑 `i2c_adapter.c` 中的 `GPIOI2C_Bus_Init()` 函数。

## 注意事项

1. 确保 I2C 总线已正确配置，引脚未被其他设备占用
2. 首次使用前需要调用 `Cap_Afe_Init()` 进行初始化
3. 测量函数返回 1 表示成功，0 表示失败
4. 驱动已集成到项目的 CMakeLists.txt 中，无需额外配置

## 参考

更多信息请参考原始驱动文档：`MC1081/Readme.md`

