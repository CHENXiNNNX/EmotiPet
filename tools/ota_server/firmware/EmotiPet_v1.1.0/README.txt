EmotiPet 固件发布包
版本: 1.1.0
创建时间: 2025-12-12 14:09:18
目标芯片: ESP32-S3

文件列表:

- bootloader.bin
  类型: bootloader
  大小: 16464 字节
  MD5: 7304584d00e834ce9069163b0c8209bd
  烧录地址: 0x0
  修改时间: 2025-12-11 18:17:50

- EmotiPet.bin
  类型: app
  大小: 211232 字节
  MD5: 3cb1f75058501afb310d04af978cd95b
  烧录地址: 0x20000
  修改时间: 2025-12-12 14:08:47

- partition-table.bin
  类型: partition-table
  大小: 3072 字节
  MD5: afe549bac5e55ac6de6bd0c047004a44
  烧录地址: 0x8000
  修改时间: 2025-12-11 18:17:33

- ota_data_initial.bin
  类型: otadata
  大小: 8192 字节
  MD5: 84d04c9d6cc8ef35bf825d51a5277699
  烧录地址: 0xd000
  修改时间: 2025-12-11 18:17:33

烧录配置:
  模式: dio
  大小: 16MB
  频率: 80m

使用方法:
1. 使用 esptool.py 烧录固件
2. 参考 flasher_args.json 中的地址信息
3. 或使用 ESP-IDF 的 flash 命令

示例命令:
  esptool.py --chip esp32s3 --port /dev/ttyUSB0 \
    --baud 921600 write_flash \
    0x0 bootloader.bin \
    0x8000 partition-table.bin \
    0xd000 ota_data_initial.bin \
    0x20000 EmotiPet.bin
