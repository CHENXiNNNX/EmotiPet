#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
EmotiPet BLE WiFi 配网工具

使用方法:
    1. 安装依赖: pip install bleak
    2. 运行: python ble_wifi_config.py

功能:
    - 扫描 BLE 设备
    - 连接 EmotiPet 设备
    - 配置 WiFi 网络
    - 查看连接状态
"""

import asyncio
import sys
from typing import Optional

try:
    from bleak import BleakClient, BleakScanner
    from bleak.backends.device import BLEDevice
except ImportError:
    print("请先安装 bleak 库: pip install bleak")
    sys.exit(1)


# ==================== UUID 定义 ====================

# 配网服务
PROVISION_SERVICE_UUID = "12345678-1234-5678-1234-56789abcdef0"
WIFI_SSID_CHAR_UUID    = "12345678-1234-5678-1234-56789abcdef1"
WIFI_PASSWORD_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef2"
WIFI_STATUS_CHAR_UUID  = "12345678-1234-5678-1234-56789abcdef3"
WIFI_COMMAND_CHAR_UUID = "12345678-1234-5678-1234-56789abcdef4"

# 设备信息服务
DEVICE_INFO_SERVICE_UUID = "0000180a-0000-1000-8000-00805f9b34fb"
MANUFACTURER_CHAR_UUID   = "00002a29-0000-1000-8000-00805f9b34fb"
MODEL_NUMBER_CHAR_UUID   = "00002a24-0000-1000-8000-00805f9b34fb"
FIRMWARE_REV_CHAR_UUID   = "00002a26-0000-1000-8000-00805f9b34fb"

# 电池服务
BATTERY_SERVICE_UUID     = "0000180f-0000-1000-8000-00805f9b34fb"
BATTERY_LEVEL_CHAR_UUID  = "00002a19-0000-1000-8000-00805f9b34fb"


# ==================== 命令和状态定义 ====================

class ProvisionCommand:
    CONNECT      = 0x01
    DISCONNECT   = 0x02
    SCAN         = 0x03
    SAVE         = 0x04
    CLEAR        = 0x05
    GET_STATUS   = 0x10
    GET_IP       = 0x11


class ProvisionStatus:
    IDLE             = 0x00
    CONNECTING       = 0x01
    CONNECTED        = 0x02
    FAILED_TIMEOUT   = 0x10
    FAILED_WRONG_PWD = 0x11
    FAILED_NOT_FOUND = 0x12
    FAILED_UNKNOWN   = 0x1F

    @staticmethod
    def to_string(status: int) -> str:
        status_map = {
            0x00: "空闲",
            0x01: "连接中",
            0x02: "已连接",
            0x10: "连接超时",
            0x11: "密码错误",
            0x12: "网络未找到",
            0x1F: "未知错误",
        }
        return status_map.get(status, f"未知状态 ({status:#04x})")


# ==================== BLE WiFi 配置器 ====================

class BLEWiFiConfigurator:
    def __init__(self):
        self.client: Optional[BleakClient] = None
        self.device: Optional[BLEDevice] = None
        self.status_callback = None

    async def scan_devices(self, timeout: float = 5.0, name_filter: str = "EmotiPet") -> list:
        """扫描 BLE 设备"""
        print(f"正在扫描 BLE 设备 ({timeout} 秒)...")
        
        devices = await BleakScanner.discover(timeout=timeout)
        filtered = []
        
        for device in devices:
            if name_filter and device.name and name_filter.lower() in device.name.lower():
                filtered.append(device)
            elif not name_filter:
                filtered.append(device)
        
        return filtered

    async def connect(self, device: BLEDevice) -> bool:
        """连接到设备"""
        print(f"正在连接到 {device.name} ({device.address})...")
        
        try:
            self.client = BleakClient(device)
            await self.client.connect()
            self.device = device
            print(f"已连接到 {device.name}")
            return True
        except Exception as e:
            print(f"连接失败: {e}")
            return False

    async def disconnect(self) -> None:
        """断开连接"""
        if self.client and self.client.is_connected:
            await self.client.disconnect()
            print("已断开连接")
        self.client = None
        self.device = None

    async def read_device_info(self) -> dict:
        """读取设备信息"""
        if not self.client or not self.client.is_connected:
            return {}
        
        info = {}
        try:
            # 读取制造商
            data = await self.client.read_gatt_char(MANUFACTURER_CHAR_UUID)
            info["manufacturer"] = data.decode("utf-8")
        except:
            pass
        
        try:
            # 读取型号
            data = await self.client.read_gatt_char(MODEL_NUMBER_CHAR_UUID)
            info["model"] = data.decode("utf-8")
        except:
            pass
        
        try:
            # 读取固件版本
            data = await self.client.read_gatt_char(FIRMWARE_REV_CHAR_UUID)
            info["firmware"] = data.decode("utf-8")
        except:
            pass
        
        try:
            # 读取电池电量
            data = await self.client.read_gatt_char(BATTERY_LEVEL_CHAR_UUID)
            info["battery"] = data[0]
        except:
            pass
        
        return info

    async def read_wifi_status(self) -> int:
        """读取 WiFi 状态"""
        if not self.client or not self.client.is_connected:
            return -1
        
        try:
            data = await self.client.read_gatt_char(WIFI_STATUS_CHAR_UUID)
            return data[0] if data else -1
        except Exception as e:
            print(f"读取状态失败: {e}")
            return -1

    async def subscribe_status(self, callback) -> bool:
        """订阅状态通知"""
        if not self.client or not self.client.is_connected:
            return False
        
        def notification_handler(sender, data):
            if data:
                status = data[0]
                callback(status)
        
        try:
            await self.client.start_notify(WIFI_STATUS_CHAR_UUID, notification_handler)
            self.status_callback = callback
            return True
        except Exception as e:
            print(f"订阅状态失败: {e}")
            return False

    async def unsubscribe_status(self) -> None:
        """取消订阅状态通知"""
        if self.client and self.client.is_connected:
            try:
                await self.client.stop_notify(WIFI_STATUS_CHAR_UUID)
            except:
                pass

    async def configure_wifi(self, ssid: str, password: str) -> bool:
        """配置 WiFi"""
        if not self.client or not self.client.is_connected:
            print("未连接到设备")
            return False
        
        try:
            # 1. 写入 SSID
            print(f"设置 SSID: {ssid}")
            await self.client.write_gatt_char(
                WIFI_SSID_CHAR_UUID,
                ssid.encode("utf-8"),
                response=True
            )
            
            # 2. 写入密码
            print("设置密码: ********")
            await self.client.write_gatt_char(
                WIFI_PASSWORD_CHAR_UUID,
                password.encode("utf-8"),
                response=True
            )
            
            # 3. 发送连接命令
            print("发送连接命令...")
            await self.client.write_gatt_char(
                WIFI_COMMAND_CHAR_UUID,
                bytes([ProvisionCommand.CONNECT]),
                response=True
            )
            
            return True
        except Exception as e:
            print(f"配置 WiFi 失败: {e}")
            return False

    async def disconnect_wifi(self) -> bool:
        """断开 WiFi"""
        if not self.client or not self.client.is_connected:
            return False
        
        try:
            await self.client.write_gatt_char(
                WIFI_COMMAND_CHAR_UUID,
                bytes([ProvisionCommand.DISCONNECT]),
                response=True
            )
            return True
        except Exception as e:
            print(f"断开 WiFi 失败: {e}")
            return False


# ==================== 主程序 ====================

async def main():
    configurator = BLEWiFiConfigurator()
    
    print("=" * 50)
    print("      EmotiPet BLE WiFi 配网工具")
    print("=" * 50)
    
    # 1. 扫描设备
    devices = await configurator.scan_devices(timeout=5.0, name_filter="EmotiPet")
    
    if not devices:
        print("\n未找到 EmotiPet 设备，请确保设备已开启并在广播中")
        
        # 显示所有设备
        print("\n扫描到的所有设备:")
        all_devices = await configurator.scan_devices(timeout=5.0, name_filter="")
        for i, dev in enumerate(all_devices):
            print(f"  [{i}] {dev.name or '未知'} - {dev.address}")
        return
    
    # 2. 显示找到的设备
    print(f"\n找到 {len(devices)} 个 EmotiPet 设备:")
    for i, device in enumerate(devices):
        print(f"  [{i}] {device.name} - {device.address}")
    
    # 3. 选择设备
    if len(devices) == 1:
        selected_device = devices[0]
    else:
        try:
            idx = int(input("\n请输入设备编号: "))
            selected_device = devices[idx]
        except (ValueError, IndexError):
            print("无效的选择")
            return
    
    # 4. 连接设备
    if not await configurator.connect(selected_device):
        return
    
    try:
        # 5. 读取设备信息
        print("\n读取设备信息...")
        info = await configurator.read_device_info()
        if info:
            print(f"  制造商: {info.get('manufacturer', '未知')}")
            print(f"  型号: {info.get('model', '未知')}")
            print(f"  固件版本: {info.get('firmware', '未知')}")
            print(f"  电池电量: {info.get('battery', '未知')}%")
        
        # 6. 读取当前 WiFi 状态
        status = await configurator.read_wifi_status()
        print(f"\n当前 WiFi 状态: {ProvisionStatus.to_string(status)}")
        
        # 7. 订阅状态通知
        status_event = asyncio.Event()
        final_status = [None]
        
        def on_status_change(new_status):
            print(f"  WiFi 状态更新: {ProvisionStatus.to_string(new_status)}")
            final_status[0] = new_status
            if new_status in [ProvisionStatus.CONNECTED, 
                              ProvisionStatus.FAILED_TIMEOUT,
                              ProvisionStatus.FAILED_WRONG_PWD,
                              ProvisionStatus.FAILED_NOT_FOUND,
                              ProvisionStatus.FAILED_UNKNOWN]:
                status_event.set()
        
        await configurator.subscribe_status(on_status_change)
        
        # 8. 输入 WiFi 凭证
        print("\n" + "-" * 50)
        ssid = input("请输入 WiFi SSID: ").strip()
        if not ssid:
            print("SSID 不能为空")
            return
        
        password = input("请输入 WiFi 密码: ").strip()
        
        # 9. 配置 WiFi
        print("\n" + "-" * 50)
        if await configurator.configure_wifi(ssid, password):
            print("等待连接结果...")
            
            # 等待状态更新（最多 30 秒）
            try:
                await asyncio.wait_for(status_event.wait(), timeout=30.0)
                
                if final_status[0] == ProvisionStatus.CONNECTED:
                    print("\n✓ WiFi 配置成功！")
                else:
                    print(f"\n✗ WiFi 配置失败: {ProvisionStatus.to_string(final_status[0])}")
            except asyncio.TimeoutError:
                print("\n✗ 等待超时，请检查设备日志")
        
    finally:
        # 10. 断开连接
        await configurator.unsubscribe_status()
        await configurator.disconnect()


async def interactive_mode():
    """交互式模式"""
    configurator = BLEWiFiConfigurator()
    
    print("=" * 50)
    print("      EmotiPet BLE WiFi 配网工具 (交互模式)")
    print("=" * 50)
    print("\n命令:")
    print("  scan     - 扫描设备")
    print("  connect  - 连接设备")
    print("  info     - 读取设备信息")
    print("  status   - 读取 WiFi 状态")
    print("  wifi     - 配置 WiFi")
    print("  discon   - 断开 WiFi")
    print("  exit     - 退出")
    print()
    
    devices = []
    
    while True:
        try:
            cmd = input(">>> ").strip().lower()
            
            if cmd == "exit" or cmd == "quit":
                break
            
            elif cmd == "scan":
                devices = await configurator.scan_devices(timeout=5.0, name_filter="")
                print(f"找到 {len(devices)} 个设备:")
                for i, dev in enumerate(devices):
                    print(f"  [{i}] {dev.name or '未知'} - {dev.address}")
            
            elif cmd == "connect":
                if not devices:
                    print("请先扫描设备")
                    continue
                
                try:
                    idx = int(input("请输入设备编号: "))
                    await configurator.connect(devices[idx])
                except (ValueError, IndexError):
                    print("无效的选择")
            
            elif cmd == "info":
                info = await configurator.read_device_info()
                if info:
                    for k, v in info.items():
                        print(f"  {k}: {v}")
                else:
                    print("无法读取设备信息")
            
            elif cmd == "status":
                status = await configurator.read_wifi_status()
                print(f"WiFi 状态: {ProvisionStatus.to_string(status)}")
            
            elif cmd == "wifi":
                ssid = input("SSID: ").strip()
                password = input("密码: ").strip()
                if ssid:
                    await configurator.configure_wifi(ssid, password)
            
            elif cmd == "discon":
                await configurator.disconnect_wifi()
            
            else:
                print("未知命令")
        
        except KeyboardInterrupt:
            break
        except Exception as e:
            print(f"错误: {e}")
    
    await configurator.disconnect()
    print("再见!")


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "-i":
        asyncio.run(interactive_mode())
    else:
        asyncio.run(main())

