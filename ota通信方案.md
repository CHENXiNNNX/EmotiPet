1.设备端json通信格式
(1)检测更新
{
  "type": "check_update",
  "from": "device_01",
  "to": "ota_server",      
  "current_version": "1.0.0",
  "timestamp": "2025-03-12T19:00:00Z"
}
说明：
- from: 设备标识，如 device_01
- current_version: 当前固件版本，格式为 x.x.x，如 1.0.0
- timestamp: ISO 8601 格式时间戳，如 2025-03-12T19:00:00Z

(2)获取固件信息(最新)
{
  "type": "get_firmware_info",
  "from": "device_01",
  "to": "ota_server",     
  "timestamp": "2025-03-12T19:00:00Z"
}
说明：
- timestamp: ISO 8601 格式时间戳

(3)请求下载固件
{
  "type": "request_firmware",
  "from": "device_01",
  "to": "ota_server",      
  "timestamp": "2025-03-12T19:00:00Z",
  "data": {
    "name": "firmware.bin",
    "target_version": "1.1.0",
    "md5": "abc123def456..."
  }
}
说明：
- data.name: 固件文件名
- data.target_version: 目标版本号，格式为 x.x.x
- data.md5: 固件 MD5 校验值

(4)报告升级状态
{
  "type": "report_status",
  "from": "device_01",
  "to": "ota_server",    
  "timestamp": "2025-03-12T19:00:00Z",
  "data": {
    "status": 1,
    "progress": 45,
    "current_version": "1.0.0",
    "target_version": "1.1.0"
  }
}
说明：
- data.status: 状态码，0=checking, 1=downloading, 2=verifying, 3=completed, 4=failed
- data.progress: 进度百分比，整数 0-100
- data.current_version: 当前版本号
- data.target_version: 目标版本号

2.服务器json通信格式
(1)回复更新
{
  "type": "reply_update",
  "from": "ota_server",    
  "to": "device_01",
  "timestamp": "2025-03-12T19:00:00Z",
  "respond": 1,
  "download_url": "http://10.93.1.49:5000/firmware/firmware.bin"
}
说明：
- respond: 响应码，0=无更新, 1=有更新, 2=下载就绪
- download_url: 下载地址，仅在 respond=2 时提供

(2)固件信息
{
  "type": "firmware_info",
  "from": "ota_server",   
  "to": "device_01",
  "timestamp": "2025-03-12T19:00:00Z",
  "file": {
    "version": "1.1.0",
    "name": "firmware.bin",
    "size": 1234567,
    "info": "修复了xxx问题",
    "md5": "abc123def456...",
    "time": "2025-03-12T19:00:00Z"
  }
}
说明：
- file.version: 固件版本号，格式为 x.x.x
- file.name: 固件文件名
- file.size: 固件大小，数字类型，单位：字节
- file.info: 固件描述信息
- file.md5: MD5 校验值
- file.time: 固件更新时间，ISO 8601 格式

(3)错误响应
{
  "type": "error",
  "from": "ota_server",   
  "to": "device_01",
  "timestamp": "2025-03-12T19:00:00Z",
  "error": {
    "code": 1001,
    "message": "文件不存在"
  }
}
说明：
- error.code: 错误码，数字类型
- error.message: 错误描述信息

3.错误码定义
1000-1099: 通用错误
  1000: 未知错误
  1001: 文件不存在
  1002: 文件格式错误
1100-1199: 网络相关错误
  1100: 网络连接失败
  1101: 下载超时
1200-1299: 验证相关错误
  1200: MD5 校验失败
  1201: 版本不兼容
1300-1399: 存储相关错误
  1300: 存储空间不足
  1301: 写入失败
