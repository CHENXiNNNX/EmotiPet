# WebSocket 测试服务器

用于测试设备端 WebSocket JSON 消息通信的简单测试服务器。

## 安装依赖

```bash
cd tools/websocket_test_server
npm install
```

## 运行服务器

```bash
npm start
# 或
node websocket_server.js
```

服务器默认监听 `ws://0.0.0.0:8080`

## 功能

1. **接收并打印消息**: 所有收到的 JSON 消息都会格式化打印
2. **验证 JSON 格式**: 自动验证收到的消息是否为有效的 JSON
3. **自动回应 hello**: 收到 hello 消息后自动发送 hello_ack
4. **手动发送消息**: 通过命令行交互发送测试消息

## 可用命令

- `help` - 显示帮助信息
- `hello_ack` - 发送 hello_ack 消息
- `command [device_id]` - 发送命令消息（play_sound）
- `res_sync [device_id]` - 发送资源同步消息
- `error [device_id] [code]` - 发送错误消息
- `status` - 显示连接状态
- `exit` - 退出服务器

## 示例

```
> help
> hello_ack        # 发送 hello_ack 消息
> command xxx      # 向设备 xxx 发送命令
> error xxx 1000   # 向设备 xxx 发送错误码 1000
```

## 消息格式

服务器支持以下消息类型：

- **hello** (设备端 → 服务器): 连接握手消息
- **hello_ack** (服务器 → 设备端): 握手回应
- **command** (服务器 → 设备端): 命令消息
- **res_sync** (双向): 资源同步消息
- **error** (双向): 错误消息

详细格式请参考 `docs/机器人通信方案.md`

