# BLE 客户端对接文档

## 一、设备信息

### 1.1 设备名称
- **默认名称**: `EmotiPet`
- **用途**: 用于 BLE 扫描和识别设备

### 1.2 设备地址
- **格式**: `XX:XX:XX:XX:XX:XX` (MAC 地址)
- **获取方式**: 连接后可通过 GAP 服务获取

---

## 二、服务 UUID 列表

### 2.1 标准服务

| 服务名称 | UUID | 说明 |
|---------|------|------|
| GAP Service | `1800` | 通用访问配置文件 |
| GATT Service | `1801` | GATT 服务 |
| Device Information | `180A` | 设备信息服务 |
| Battery Service | `180F` | 电池服务 |

### 2.2 自定义服务

| 服务名称 | UUID | 说明 |
|---------|------|------|
| Provision Service | `12345678-1234-5678-1234-56789abcdef0` | WiFi 配网服务 |

---

## 三、特征 UUID 和属性

### 3.1 配网服务 (Provision Service)

**服务 UUID**: `12345678-1234-5678-1234-56789abcdef0`

| 特征名称 | UUID | 属性 | 最大长度 | 说明 |
|---------|------|------|---------|------|
| WiFi SSID | `12345678-1234-5678-1234-56789abcdef1` | READ, WRITE | 32 字节 | WiFi 网络名称 |
| WiFi Password | `12345678-1234-5678-1234-56789abcdef2` | WRITE | 64 字节 | WiFi 密码 |
| WiFi Status | `12345678-1234-5678-1234-56789abcdef3` | READ, NOTIFY | 32 字节 | WiFi 连接状态 |
| WiFi Command | `12345678-1234-5678-1234-56789abcdef4` | WRITE | 16 字节 | 控制命令 |

### 3.2 设备信息服务 (Device Information Service)

**服务 UUID**: `180A`

| 特征名称 | UUID | 属性 | 说明 |
|---------|------|------|------|
| Manufacturer Name | `2A29` | READ | 制造商名称（字符串） |
| Model Number | `2A24` | READ | 型号（字符串） |
| Serial Number | `2A25` | READ | 序列号（字符串） |
| Firmware Revision | `2A26` | READ | 固件版本（字符串） |
| Hardware Revision | `2A27` | READ | 硬件版本（字符串） |
| Software Revision | `2A28` | READ | 软件版本（字符串） |

### 3.3 电池服务 (Battery Service)

**服务 UUID**: `180F`

| 特征名称 | UUID | 属性 | 说明 |
|---------|------|------|------|
| Battery Level | `2A19` | READ, NOTIFY | 电池电量（0-100，1 字节） |

---

## 四、数据格式

### 4.1 WiFi SSID 特征
- **格式**: UTF-8 字符串
- **长度**: 1-32 字节（不含结束符）
- **示例**: `"MyWiFi"` → `[0x4D, 0x79, 0x57, 0x69, 0x46, 0x69]`

### 4.2 WiFi Password 特征
- **格式**: UTF-8 字符串
- **长度**: 1-64 字节（不含结束符）
- **示例**: `"password123"` → `[0x70, 0x61, 0x73, 0x73, 0x77, 0x6F, 0x72, 0x64, 0x31, 0x32, 0x33]`

### 4.3 WiFi Status 特征
- **格式**: 单字节状态码
- **值**:
  - `0x00` - IDLE（空闲）
  - `0x01` - CONNECTING（正在连接）
  - `0x02` - CONNECTED（已连接）
  - `0x10` - FAILED_TIMEOUT（连接超时）
  - `0x11` - FAILED_WRONG_PWD（密码错误）
  - `0x12` - FAILED_NOT_FOUND（网络未找到）
  - `0x1F` - FAILED_UNKNOWN（未知错误）

### 4.4 WiFi Command 特征
- **格式**: 单字节命令码
- **命令列表**:
  - `0x01` - CONNECT（连接 WiFi，需要先设置 SSID 和 Password）
  - `0x02` - DISCONNECT（断开 WiFi）
  - `0x03` - SCAN（扫描 WiFi 网络）
  - `0x04` - SAVE（保存 WiFi 凭证）
  - `0x05` - CLEAR（清除 WiFi 凭证）
  - `0x10` - GET_STATUS（获取当前状态）
  - `0x11` - GET_IP（获取 IP 地址）

### 4.5 Battery Level 特征
- **格式**: 单字节无符号整数
- **范围**: 0-100（百分比）
- **示例**: `75` → `[0x4B]`

---

## 五、操作流程

### 5.1 WiFi 配网流程

```
1. 扫描并连接设备
   └─> 设备名称: "EmotiPet"

2. 发现服务
   └─> Provision Service: 12345678-1234-5678-1234-56789abcdef0

3. 写入 WiFi SSID
   └─> 特征: 12345678-1234-5678-1234-56789abcdef1
   └─> 数据: "MyWiFi" (UTF-8 字符串)

4. 写入 WiFi Password
   └─> 特征: 12345678-1234-5678-1234-56789abcdef2
   └─> 数据: "password123" (UTF-8 字符串)

5. 订阅 WiFi Status 通知
   └─> 特征: 12345678-1234-5678-1234-56789abcdef3
   └─> 启用通知 (CCCD = 0x0100)

6. 发送连接命令
   └─> 特征: 12345678-1234-5678-1234-56789abcdef4
   └─> 数据: [0x01] (CONNECT)

7. 监听状态变化
   └─> 接收通知: 0x01 (CONNECTING)
   └─> 接收通知: 0x02 (CONNECTED) 或 0x10/0x11/0x12/0x1F (失败)
```

### 5.2 读取设备信息流程

```
1. 连接设备

2. 发现服务
   └─> Device Information Service: 180A

3. 读取特征值
   └─> Manufacturer: 2A29 → "EmotiPet"
   └─> Model Number: 2A24 → "EP-001"
   └─> Firmware Revision: 2A26 → "1.0.0"
   └─> 等等...
```

### 5.3 读取电池电量流程

```
1. 连接设备

2. 发现服务
   └─> Battery Service: 180F

3. 读取当前电量
   └─> Battery Level: 2A19 → [0x4B] (75%)

4. 订阅电量通知（可选）
   └─> 启用通知 (CCCD = 0x0100)
   └─> 接收电量变化通知
```

---

## 六、特征属性说明

### 6.1 属性标志位

| 标志 | 值 | 说明 |
|------|-----|------|
| READ | 0x01 | 可读 |
| WRITE | 0x02 | 可写（需要响应） |
| WRITE_NR | 0x04 | 可写（无需响应） |
| NOTIFY | 0x08 | 可通知 |
| INDICATE | 0x10 | 可指示 |

### 6.2 通知/指示启用

- **CCCD UUID**: `2902` (Client Characteristic Configuration Descriptor)
- **启用通知**: 写入 `[0x01, 0x00]` (小端序，即 0x0100)
- **启用指示**: 写入 `[0x02, 0x00]` (小端序，即 0x0200)
- **禁用**: 写入 `[0x00, 0x00]`

---

## 七、错误处理

### 7.1 连接错误
- 连接超时：检查设备是否在广播
- 连接失败：检查设备是否支持连接

### 7.2 特征操作错误
- 读取失败：检查特征是否支持 READ
- 写入失败：检查特征是否支持 WRITE，数据长度是否超限
- 通知失败：检查是否已启用通知（写入 CCCD）

### 7.3 WiFi 配网错误
- 状态码 `0x10`: 连接超时，检查网络是否可用
- 状态码 `0x11`: 密码错误，检查密码是否正确
- 状态码 `0x12`: 网络未找到，检查 SSID 是否正确
- 状态码 `0x1F`: 未知错误，查看设备日志

---

## 八、示例代码（Python - bleak）

```python
import asyncio
from bleak import BleakClient, BleakScanner

# UUID 定义
PROVISION_SERVICE = "12345678-1234-5678-1234-56789abcdef0"
WIFI_SSID_CHAR = "12345678-1234-5678-1234-56789abcdef1"
WIFI_PASSWORD_CHAR = "12345678-1234-5678-1234-56789abcdef2"
WIFI_STATUS_CHAR = "12345678-1234-5678-1234-56789abcdef3"
WIFI_COMMAND_CHAR = "12345678-1234-5678-1234-56789abcdef4"

DEVICE_INFO_SERVICE = "180A"
MANUFACTURER_CHAR = "2A29"
MODEL_CHAR = "2A24"
FIRMWARE_CHAR = "2A26"

BATTERY_SERVICE = "180F"
BATTERY_LEVEL_CHAR = "2A19"

# 状态通知回调
def status_notification_handler(sender, data):
    status = data[0]
    status_map = {
        0x00: "IDLE",
        0x01: "CONNECTING",
        0x02: "CONNECTED",
        0x10: "FAILED_TIMEOUT",
        0x11: "FAILED_WRONG_PWD",
        0x12: "FAILED_NOT_FOUND",
        0x1F: "FAILED_UNKNOWN"
    }
    print(f"WiFi 状态: {status_map.get(status, 'UNKNOWN')}")

async def main():
    # 1. 扫描设备
    print("扫描设备...")
    devices = await BleakScanner.find_device_by_name("EmotiPet")
    if not devices:
        print("未找到设备")
        return
    
    # 2. 连接设备
    async with BleakClient(devices) as client:
        print(f"已连接: {client.address}")
        
        # 3. 读取设备信息
        manufacturer = await client.read_gatt_char(MANUFACTURER_CHAR)
        model = await client.read_gatt_char(MODEL_CHAR)
        firmware = await client.read_gatt_char(FIRMWARE_CHAR)
        print(f"制造商: {manufacturer.decode()}")
        print(f"型号: {model.decode()}")
        print(f"固件: {firmware.decode()}")
        
        # 4. 读取电池电量
        battery = await client.read_gatt_char(BATTERY_LEVEL_CHAR)
        print(f"电池电量: {battery[0]}%")
        
        # 5. WiFi 配网
        # 写入 SSID
        ssid = "MyWiFi"
        await client.write_gatt_char(WIFI_SSID_CHAR, ssid.encode())
        print(f"SSID 已设置: {ssid}")
        
        # 写入密码
        password = "password123"
        await client.write_gatt_char(WIFI_PASSWORD_CHAR, password.encode())
        print("密码已设置")
        
        # 订阅状态通知
        await client.start_notify(WIFI_STATUS_CHAR, status_notification_handler)
        
        # 发送连接命令
        await client.write_gatt_char(WIFI_COMMAND_CHAR, bytes([0x01]))
        print("已发送连接命令")
        
        # 等待状态变化
        await asyncio.sleep(10)
        
        # 停止通知
        await client.stop_notify(WIFI_STATUS_CHAR)

if __name__ == "__main__":
    asyncio.run(main())
```

---

## 九、快速参考表

### 9.1 关键 UUID

```
设备名称: "EmotiPet"

服务:
- 配网服务: 12345678-1234-5678-1234-56789abcdef0
- 设备信息: 180A
- 电池服务: 180F

配网特征:
- SSID:    12345678-1234-5678-1234-56789abcdef1 (读写)
- Password: 12345678-1234-5678-1234-56789abcdef2 (写)
- Status:   12345678-1234-5678-1234-56789abcdef3 (读+通知)
- Command:  12345678-1234-5678-1234-56789abcdef4 (写)
```

### 9.2 命令码

```
0x01 - CONNECT
0x02 - DISCONNECT
0x03 - SCAN
0x04 - SAVE
0x05 - CLEAR
0x10 - GET_STATUS
0x11 - GET_IP
```

### 9.3 状态码

```
0x00 - IDLE
0x01 - CONNECTING
0x02 - CONNECTED
0x10 - FAILED_TIMEOUT
0x11 - FAILED_WRONG_PWD
0x12 - FAILED_NOT_FOUND
0x1F - FAILED_UNKNOWN
```

