# Assets 自动打包说明

## 概述

本项目实现了类似 `xiaozhi-esp32` 的自动打包机制，在编译时自动将自定义唤醒词模型打包成 `assets.bin`。

## 特性

- ✅ 支持中英文自定义唤醒词模型打包
- ✅ 编译时自动运行打包脚本
- ✅ 自动烧录到 `assets` 分区
- ✅ 支持多语言模型同时打包（如 `mn6_cn` + `mn6_en`）

## 使用方法

### 1. 默认配置

在 `main/CMakeLists.txt` 中已经配置了默认值：

- **Multinet 模型**: `mn6_cn` 和 `mn6_en`
- **中文唤醒词**: `你好小智`
- **英文唤醒词**: `hello`
- **检测阈值**: `0.2`

### 2. 自定义配置

#### 方法 1: 修改 CMakeLists.txt

直接修改 `main/CMakeLists.txt` 中的变量：

```cmake
set(MULTINET_MODELS "mn6_cn" "mn6_en")  # 修改模型列表
set(CN_WAKE_WORD "你的中文唤醒词")        # 修改中文唤醒词
set(EN_WAKE_WORD "your_english_wake_word")  # 修改英文唤醒词
set(WAKE_WORD_THRESHOLD "0.3")          # 修改阈值
```

#### 方法 2: 使用 CMake 变量（推荐）

在编译时通过 CMake 变量传递：

```bash
idf.py build -DMULTINET_MODELS="mn7_cn;mn7_en" \
             -DCN_WAKE_WORD="你好" \
             -DEN_WAKE_WORD="hi" \
             -DWAKE_WORD_THRESHOLD="0.25"
```

### 3. 可用的 Multinet 模型

在 `managed_components/espressif__esp-sr/model/multinet_model/` 目录下可用的模型：

- **中文模型**: `mn3_cn`, `mn4_cn`, `mn4cn`, `mn4q8_cn`, `mn5q8_cn`, `mn6_cn`, `mn6_cn_ac`, `mn7_cn`, `mn7_cn_ac`
- **英文模型**: `mn5q8_en`, `mn6_en`, `mn7_en`

### 4. 编译和烧录

```bash
# 编译（会自动运行打包脚本）
idf.py build

# 烧录（会自动将 assets.bin 烧录到 assets 分区）
idf.py flash
```

## 生成的文件结构

打包脚本会在 `build/generated_assets.bin` 生成最终文件，包含：

```
assets.bin
├── index.json          # 包含模型配置信息
└── srmodels.bin        # 包含所有打包的模型文件
```

## index.json 结构

生成的 `index.json` 支持多语言配置：

```json
{
    "version": 1,
    "srmodels": "srmodels.bin",
    "multinet_model": {
        "languages": ["cn", "en"],
        "duration": 3000,
        "threshold": 0.2,
        "commands": {
            "cn": [
                {
                    "command": "你好小智",
                    "text": "你好小智",
                    "action": "wake"
                }
            ],
            "en": [
                {
                    "command": "hello",
                    "text": "hello",
                    "action": "wake"
                }
            ]
        }
    }
}
```

## 工作原理

1. **CMake 配置阶段**: 检测 ESP-SR 组件和 assets 分区
2. **构建阶段**: 
   - 执行 `scripts/build_assets.py`
   - 复制 Multinet 模型到临时目录
   - 调用 `pack_models()` 生成 `srmodels.bin`
   - 生成 `index.json` 配置文件
   - 调用 `pack_assets_simple()` 打包成 `assets.bin`
3. **烧录阶段**: 使用 `esptool_py_flash_to_partition()` 自动烧录到 `assets` 分区

## 故障排查

### 1. 找不到 ESP-SR 组件

**错误信息**: `未找到 ESP-SR 组件，跳过 assets 打包`

**解决方案**: 确保已正确添加 `espressif__esp-sr` 组件到 `idf_component.yml`

### 2. 模型目录不存在

**错误信息**: `警告: 模型目录不存在: ...`

**解决方案**: 检查 `managed_components/espressif__esp-sr/model/multinet_model/` 中是否存在指定的模型目录

### 3. 未找到 assets 分区

**错误信息**: `未找到 assets 分区，跳过 assets 打包`

**解决方案**: 检查 `partition.csv` 中是否定义了 `assets` 分区

### 4. Python 脚本执行失败

**检查步骤**:
1. 确保 Python 3 已安装
2. 检查脚本权限: `chmod +x scripts/build_assets.py`
3. 查看详细错误信息

## 手动测试打包脚本

可以手动运行打包脚本进行测试：

```bash
python scripts/build_assets.py \
    --esp_sr_model_path managed_components/espressif__esp-sr/model \
    --output test_assets.bin \
    --multinet_models mn6_cn mn6_en \
    --cn_wake_word "你好小智" \
    --en_wake_word "hello" \
    --threshold 0.2
```

## 注意事项

1. **模型大小**: 多个模型会显著增加 `assets.bin` 的大小，确保 `assets` 分区足够大（建议至少 4MB）
2. **阈值设置**: 阈值越低，唤醒越敏感，但也可能增加误唤醒
3. **语言支持**: 当前实现支持同时使用中英文模型，但在运行时需要根据实际语言选择对应的命令词

