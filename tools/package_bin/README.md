# ESP32 固件打包工具使用说明

## 简介

`package_bin.py` 是一个用于打包 ESP32 固件的 Python 脚本，可以从 `build` 目录收集 `.bin` 文件并生成发布包。

## 前置要求

1. 已完成项目编译（`idf.py build`）
2. Python 3.x

## 基本使用

### 1. 最简单的使用（使用默认设置）

```bash
cd tools/package_bin
python3 package_bin.py
```

这会：
- 从 `build` 目录读取固件文件
- 自动生成时间戳版本号（格式：`YYYYMMDD_HHMMSS`）
- 输出到 `release` 目录

### 2. 指定版本号

```bash
python3 package_bin.py --version 1.0.0
```

### 3. 指定 build 目录和输出目录

```bash
python3 package_bin.py --build-dir ../../build --output-dir ../ota_server/firmware
```

### 4. 打包并创建压缩包

```bash
python3 package_bin.py --compress
```

### 5. 完整示例

```bash
python3 package_bin.py --version 1.1.0 --build-dir ../../build --output-dir ../ota_server/firmware --compress
```

## 参数说明

| 参数 | 说明 | 默认值 |
|------|------|--------|
| `--build-dir` | build 目录路径 | `build` |
| `--output-dir` | 输出目录路径 | `release` |
| `--version` | 版本号 | 自动生成（时间戳） |
| `--compress` | 创建 ZIP 压缩包 | 否 |

## 输出内容

打包完成后，会在输出目录生成：

```
release/
└── EmotiPet_v{version}/
    ├── bootloader.bin
    ├── EmotiPet.bin
    ├── partition-table.bin
    ├── ota_data_initial.bin
    ├── manifest.json          # 文件清单（包含MD5、大小等信息）
    ├── flasher_args.json      # 烧录参数（如果存在）
    └── README.txt             # 使用说明
```

如果使用 `--compress` 参数，还会生成：
```
release/
└── EmotiPet_v{version}.zip    # ZIP 压缩包
```

## 快速开始

1. **编译项目**
   ```bash
   idf.py build
   ```

2. **打包固件**
   ```bash
   cd tools/package_bin
   python3 package_bin.py --version 1.0.0 --compress
   ```

3. **查看输出**
   ```bash
   ls -lh release/
   ```

## 注意事项

- 确保在运行脚本前已完成项目编译
- 版本号建议使用语义化版本（如 `1.0.0`）
- 压缩包文件名格式：`EmotiPet_v{version}.zip`

