# Chatbot 回调机制使用说明

## 设计概述

`Chatbot` 类通过回调机制实现发送和接收的统一处理：

- **发送回调**：统管所有发送前的数据整合、字段填充、验证等逻辑
- **接收回调**：统管所有接收到的消息，按类型进行分类处理

## 架构设计

```
App
  ├─ 设置发送回调 (MessageSender)
  │   └─ 统一处理：字段填充、验证、日志、序列化
  │
  ├─ 设置接收回调 (MessageReceiver)
  │   └─ 统一处理：解析、验证、日志、类型分发
  │
  └─ 各类型消息处理函数
      ├─ handleRecvInfo
      ├─ handleMovInfo
      ├─ handleListen
      └─ ...
```

## 使用示例

### 1. 在 App 中设置回调

```cpp
// 在 App::initChatbot() 中

// 创建发送回调管理器
static chatbot::handle::MessageSender sender;
sender.setAutoFillBaseFields(true); // 自动填充 from 和 timestamp

// 设置发送回调
chatbot_.setSendCallback([&sender](chatbot::message::Message& msg) -> std::string {
    return sender.processMessage(msg);
});

// 创建接收回调管理器
static chatbot::handle::MessageReceiver receiver;

// 设置各类型消息的处理函数
receiver.setRecvInfoHandler([this](const chatbot::message::RecvInfoMessage& msg) {
    // 处理 recv_info 消息
    ESP_LOGI(TAG, "收到数据接收控制: command=%s", msg.command.c_str());
    // 保存控制状态，更新传感器上传配置
});

receiver.setMovInfoHandler([this](const chatbot::message::MovInfoMessage& msg) {
    // 处理 mov_info 消息
    ESP_LOGI(TAG, "收到运动控制消息");
    // 执行舵机控制
});

receiver.setPlayHandler([this](const chatbot::message::PlayMessage& msg) {
    // 处理 play 消息
    ESP_LOGI(TAG, "收到音频播放请求");
    // 准备接收音频数据
});

receiver.setErrorHandler([this](const chatbot::message::ErrorMessage& msg) {
    // 处理 error 消息
    ESP_LOGE(TAG, "收到错误消息: code=%d, msg=%s", 
             msg.error_code, msg.error_message.c_str());
});

// 设置接收回调
chatbot_.setReceiveCallback([&receiver](const std::string& json_str) -> bool {
    return receiver.handleMessage(json_str);
});
```

### 2. 发送消息（简化）

```cpp
// 在 App 中发送消息
chatbot::message::TransportInfoMessage msg;
msg.base.type = chatbot::message::MessageType::TRANSPORT_INFO;
msg.base.to = "server";
// from 和 timestamp 会自动填充（通过发送回调）
msg.command = command_str;
msg.data = sensor_data;

// 直接发送，发送回调会自动处理
chatbot_.sendMessage(msg);
```

## 优势

1. **统一处理**：所有发送/接收逻辑集中管理
2. **自动填充**：基础字段自动填充，减少重复代码
3. **类型分发**：接收回调自动按类型分发，代码清晰
4. **易于扩展**：新增消息类型只需添加处理函数
5. **解耦合**：Chatbot 只负责通信，业务逻辑在 App 中

