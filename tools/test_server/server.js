#!/usr/bin/env node
/**
 * WebSocket 测试服务器
 * 支持：JPEG图片接收保存、音频接收播放、JSON消息收发
 */

const WebSocket = require('ws');
const http = require('http');
const fs = require('fs');
const path = require('path');

const WS_PORT = 8080;
const HTTP_PORT = 3000;
const HOST = '0.0.0.0';

// 数据存储目录
const DATA_DIR = path.join(__dirname, 'data');
const IMAGES_DIR = path.join(DATA_DIR, 'images');
const AUDIO_DIR = path.join(DATA_DIR, 'audio');

// 确保目录存在
function ensureDir(dir) {
    if (!fs.existsSync(dir)) {
        fs.mkdirSync(dir, { recursive: true });
    }
}

ensureDir(DATA_DIR);
ensureDir(IMAGES_DIR);
ensureDir(AUDIO_DIR);

// 存储收到的消息
let receivedMessages = [];
let sentMessages = [];
const MAX_MESSAGES = 100; // 最多保存100条消息

// 存储图片和音频文件信息
let imageFiles = [];
let audioFiles = [];
const MAX_FILES = 50; // 最多保存50个文件

// 当前连接的客户端
let currentClient = null;

// 获取 ISO 8601 格式的时间戳
function getTimestamp() {
    return new Date().toISOString().replace(/\.\d{3}Z$/, 'Z');
}

// 获取当前时间字符串（用于文件名）
function getTimeString() {
    const now = new Date();
    return now.toISOString().replace(/[:.]/g, '-').replace('T', '_').slice(0, -5);
}

// 清理旧文件
function cleanupOldFiles(files, maxFiles) {
    if (files.length > maxFiles) {
        const toDelete = files.splice(0, files.length - maxFiles);
        toDelete.forEach(file => {
            try {
                fs.unlinkSync(file.path);
            } catch (err) {
                console.error(`删除文件失败: ${file.path}`, err);
            }
        });
    }
}

// 判断是否为JPEG图片（检查文件头）
function isJPEG(buffer) {
    return buffer.length >= 3 && 
           buffer[0] === 0xFF && 
           buffer[1] === 0xD8 && 
           buffer[2] === 0xFF;
}

// 保存JPEG图片
function saveJPEG(data) {
    const filename = `image_${getTimeString()}.jpg`;
    const filepath = path.join(IMAGES_DIR, filename);
    
    fs.writeFileSync(filepath, data);
    
    const fileInfo = {
        filename: filename,
        path: filepath,
        url: `/data/images/${filename}`,
        size: data.length,
        timestamp: getTimestamp()
    };
    
    imageFiles.push(fileInfo);
    cleanupOldFiles(imageFiles, MAX_FILES);
    
    console.log(`📷 保存JPEG图片: ${filename} (${data.length} 字节)`);
    return fileInfo;
}

// 保存音频文件
function saveAudio(data) {
    const filename = `audio_${getTimeString()}.opus`;
    const filepath = path.join(AUDIO_DIR, filename);
    
    fs.writeFileSync(filepath, data);
    
    const fileInfo = {
        filename: filename,
        path: filepath,
        url: `/data/audio/${filename}`,
        size: data.length,
        timestamp: getTimestamp()
    };
    
    audioFiles.push(fileInfo);
    cleanupOldFiles(audioFiles, MAX_FILES);
    
    console.log(`🎵 保存音频文件: ${filename} (${data.length} 字节)`);
    return fileInfo;
}

// 解析JSON消息
function parseJSON(data) {
    try {
        const message = JSON.parse(data.toString());
        return { success: true, message };
    } catch (error) {
        return { success: false, error: error.message };
    }
}

// 打印JSON消息
function printMessage(direction, message) {
    console.log('\n' + '='.repeat(60));
    console.log(`📨 ${direction}`);
    console.log('='.repeat(60));
    console.log(JSON.stringify(message, null, 2));
    console.log('='.repeat(60));
}

// 保存收到的消息
function saveReceivedMessage(message) {
    receivedMessages.push({
        message: message,
        timestamp: getTimestamp()
    });
    if (receivedMessages.length > MAX_MESSAGES) {
        receivedMessages.shift();
    }
}

// 保存发送的消息
function saveSentMessage(message) {
    sentMessages.push({
        message: message,
        timestamp: getTimestamp()
    });
    if (sentMessages.length > MAX_MESSAGES) {
        sentMessages.shift();
    }
}

// 处理接收到的消息
function handleMessage(ws, data) {
    const result = parseJSON(data);
    
    if (!result.success) {
        console.error('❌ JSON 解析失败:', result.error);
        console.error('原始数据:', data.toString('hex').slice(0, 100) + '...');
        return;
    }
    
    const message = result.message;
    printMessage('接收 (RECV)', message);
    saveReceivedMessage(message);
    
    // 根据消息类型处理
    switch (message.type) {
        case 'transport_info':
            console.log('✅ 收到数据上传消息');
            break;
            
        case 'bluetooth_info':
            console.log('✅ 收到蓝牙信息消息');
            break;
            
        case 'recv_info':
            console.log('✅ 收到数据接收控制消息');
            break;
            
        case 'mov_info':
            console.log('✅ 收到运动控制消息');
            break;
            
        case 'listen':
            console.log('✅ 收到音频监听消息');
            break;
            
        case 'play':
            console.log('✅ 收到音频播放消息');
            break;
            
        case 'error':
            console.log('⚠️  收到错误消息');
            break;
            
        default:
            console.log(`⚠️  未知消息类型: ${message.type}`);
    }
}

// 处理二进制数据
function handleBinary(ws, data) {
    console.log(`\n📦 收到二进制数据: ${data.length} 字节`);
    
    // 判断是否为JPEG图片
    if (isJPEG(data)) {
        const fileInfo = saveJPEG(data);
        console.log(`   ✅ 已保存为: ${fileInfo.filename}`);
    } else {
        // 其他二进制数据视为音频（OPUS格式）
        const fileInfo = saveAudio(data);
        console.log(`   ✅ 已保存为: ${fileInfo.filename}`);
    }
}

// 发送JSON消息
function sendJSON(ws, message) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        console.error('❌ WebSocket 未连接，无法发送消息');
        return false;
    }
    
    const jsonStr = JSON.stringify(message);
    ws.send(jsonStr);
    printMessage('发送 (SEND)', message);
    saveSentMessage(message);
    return true;
}

// 创建WebSocket服务器
const wss = new WebSocket.Server({ 
    host: HOST,
    port: WS_PORT 
});

wss.on('listening', () => {
    console.log(`\n🚀 WebSocket 服务器已启动`);
    console.log(`   地址: ws://${HOST}:${WS_PORT}`);
});

wss.on('connection', (ws, req) => {
    const clientIp = req.socket.remoteAddress;
    console.log(`\n✅ 新客户端连接: ${clientIp}`);
    
    if (currentClient && currentClient.readyState === WebSocket.OPEN) {
        console.log('⚠️  已有客户端连接，关闭旧连接');
        currentClient.close();
    }
    
    currentClient = ws;
    
    ws.on('message', (data) => {
        if (Buffer.isBuffer(data)) {
            // 二进制数据
            handleBinary(ws, data);
        } else {
            // 文本数据（JSON）
            handleMessage(ws, data);
        }
    });
    
    ws.on('close', () => {
        console.log(`\n❌ 客户端断开连接: ${clientIp}`);
        if (currentClient === ws) {
            currentClient = null;
        }
    });
    
    ws.on('error', (error) => {
        console.error(`\n❌ WebSocket 错误:`, error);
    });
});

// 创建HTTP服务器（用于Web UI）
const server = http.createServer((req, res) => {
    const url = req.url;
    
    // 提供静态文件（图片和音频）
    if (url.startsWith('/data/images/')) {
        const filename = path.basename(url);
        const filepath = path.join(IMAGES_DIR, filename);
        
        if (fs.existsSync(filepath)) {
            res.writeHead(200, { 'Content-Type': 'image/jpeg' });
            fs.createReadStream(filepath).pipe(res);
            return;
        }
    }
    
    if (url.startsWith('/data/audio/')) {
        const filename = path.basename(url);
        const filepath = path.join(AUDIO_DIR, filename);
        
        if (fs.existsSync(filepath)) {
            res.writeHead(200, { 'Content-Type': 'audio/opus' });
            fs.createReadStream(filepath).pipe(res);
            return;
        }
    }
    
    // API: 获取收到的消息
    if (url === '/api/messages/received') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(receivedMessages));
        return;
    }
    
    // API: 获取发送的消息
    if (url === '/api/messages/sent') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(sentMessages));
        return;
    }
    
    // API: 获取图片列表
    if (url === '/api/images') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(imageFiles));
        return;
    }
    
    // API: 获取音频列表
    if (url === '/api/audio') {
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(audioFiles));
        return;
    }
    
    // API: 发送消息
    if (url === '/api/send' && req.method === 'POST') {
        let body = '';
        req.on('data', chunk => {
            body += chunk.toString();
        });
        req.on('end', () => {
            try {
                const message = JSON.parse(body);
                if (currentClient && currentClient.readyState === WebSocket.OPEN) {
                    sendJSON(currentClient, message);
                    res.writeHead(200, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: true }));
                } else {
                    res.writeHead(400, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: false, error: 'WebSocket未连接' }));
                }
            } catch (error) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: false, error: error.message }));
            }
        });
        return;
    }
    
    // 提供Web UI
    if (url === '/' || url === '/index.html') {
        res.writeHead(200, { 'Content-Type': 'text/html; charset=utf-8' });
        res.end(generateHTML());
        return;
    }
    
    // 404
    res.writeHead(404, { 'Content-Type': 'text/plain' });
    res.end('404 Not Found');
});

// 生成HTML页面
function generateHTML() {
    return `<!DOCTYPE html>
<html lang="zh-CN">
<head>
    <meta charset="UTF-8">
    <meta name="viewport" content="width=device-width, initial-scale=1.0">
    <title>WebSocket 测试服务器</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }
        
        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, sans-serif;
            background: #f5f5f5;
            padding: 20px;
        }
        
        .container {
            max-width: 1400px;
            margin: 0 auto;
        }
        
        h1 {
            color: #333;
            margin-bottom: 20px;
        }
        
        .tabs {
            display: flex;
            gap: 10px;
            margin-bottom: 20px;
            border-bottom: 2px solid #ddd;
        }
        
        .tab {
            padding: 10px 20px;
            cursor: pointer;
            background: #fff;
            border: none;
            border-bottom: 2px solid transparent;
            font-size: 16px;
            transition: all 0.3s;
        }
        
        .tab:hover {
            background: #f0f0f0;
        }
        
        .tab.active {
            border-bottom-color: #007bff;
            color: #007bff;
            font-weight: bold;
        }
        
        .tab-content {
            display: none;
            background: #fff;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
        }
        
        .tab-content.active {
            display: block;
        }
        
        .message-list {
            max-height: 600px;
            overflow-y: auto;
            border: 1px solid #ddd;
            border-radius: 4px;
            padding: 10px;
            background: #fafafa;
        }
        
        .message-item {
            margin-bottom: 15px;
            padding: 10px;
            background: #fff;
            border-radius: 4px;
            border-left: 4px solid #007bff;
        }
        
        .message-item.sent {
            border-left-color: #28a745;
        }
        
        .message-header {
            display: flex;
            justify-content: space-between;
            margin-bottom: 8px;
            font-size: 12px;
            color: #666;
        }
        
        .message-body {
            font-family: 'Courier New', monospace;
            font-size: 13px;
            white-space: pre-wrap;
            word-break: break-all;
        }
        
        .image-grid {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(200px, 1fr));
            gap: 15px;
        }
        
        .image-item {
            border: 1px solid #ddd;
            border-radius: 4px;
            overflow: hidden;
            background: #fff;
        }
        
        .image-item img {
            width: 100%;
            height: auto;
            display: block;
        }
        
        .image-info {
            padding: 10px;
            font-size: 12px;
            color: #666;
        }
        
        .audio-list {
            display: flex;
            flex-direction: column;
            gap: 10px;
        }
        
        .audio-item {
            padding: 15px;
            background: #fff;
            border: 1px solid #ddd;
            border-radius: 4px;
            display: flex;
            align-items: center;
            gap: 15px;
        }
        
        .audio-item audio {
            flex: 1;
        }
        
        .audio-info {
            font-size: 12px;
            color: #666;
            min-width: 200px;
        }
        
        .send-form {
            display: flex;
            flex-direction: column;
            gap: 10px;
        }
        
        .send-form textarea {
            width: 100%;
            min-height: 200px;
            padding: 10px;
            border: 1px solid #ddd;
            border-radius: 4px;
            font-family: 'Courier New', monospace;
            font-size: 13px;
        }
        
        .send-form button {
            padding: 10px 20px;
            background: #007bff;
            color: #fff;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 16px;
        }
        
        .send-form button:hover {
            background: #0056b3;
        }
        
        .status {
            padding: 10px;
            background: #28a745;
            color: #fff;
            border-radius: 4px;
            margin-bottom: 20px;
        }
        
        .status.disconnected {
            background: #dc3545;
        }
        
        .refresh-btn {
            padding: 8px 16px;
            background: #6c757d;
            color: #fff;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            margin-bottom: 10px;
        }
        
        .refresh-btn:hover {
            background: #5a6268;
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🔌 WebSocket 测试服务器</h1>
        
        <div class="status" id="status">连接状态: 检查中...</div>
        
        <div class="tabs">
            <button class="tab active" onclick="switchTab('messages')">📨 消息</button>
            <button class="tab" onclick="switchTab('images')">📷 图片</button>
            <button class="tab" onclick="switchTab('audio')">🎵 音频</button>
            <button class="tab" onclick="switchTab('send')">📤 发送</button>
        </div>
        
        <div id="messages" class="tab-content active">
            <button class="refresh-btn" onclick="loadMessages()">🔄 刷新</button>
            <h2>收到的消息</h2>
            <div class="message-list" id="receivedMessages"></div>
            <h2 style="margin-top: 20px;">发送的消息</h2>
            <div class="message-list" id="sentMessages"></div>
        </div>
        
        <div id="images" class="tab-content">
            <button class="refresh-btn" onclick="loadImages()">🔄 刷新</button>
            <div class="image-grid" id="imageGrid"></div>
        </div>
        
        <div id="audio" class="tab-content">
            <button class="refresh-btn" onclick="loadAudio()">🔄 刷新</button>
            <div class="audio-list" id="audioList"></div>
        </div>
        
        <div id="send" class="tab-content">
            <h2>发送JSON消息</h2>
            <form class="send-form" onsubmit="sendMessage(event)">
                <textarea id="messageInput" placeholder='输入JSON消息，例如:\n{\n  "type": "recv_info",\n  "from": "server",\n  "to": "xx:xx:xx:xx:xx:xx",\n  "timestamp": "2025-01-01T00:00:00Z",\n  "command": "11111"\n}'></textarea>
                <button type="submit">发送</button>
            </form>
        </div>
    </div>
    
    <script>
        function switchTab(tabName) {
            // 隐藏所有标签页
            document.querySelectorAll('.tab-content').forEach(tab => {
                tab.classList.remove('active');
            });
            document.querySelectorAll('.tab').forEach(tab => {
                tab.classList.remove('active');
            });
            
            // 显示选中的标签页
            document.getElementById(tabName).classList.add('active');
            event.target.classList.add('active');
            
            // 加载对应数据
            if (tabName === 'messages') loadMessages();
            else if (tabName === 'images') loadImages();
            else if (tabName === 'audio') loadAudio();
        }
        
        function loadMessages() {
            // 加载收到的消息
            fetch('/api/messages/received')
                .then(res => res.json())
                .then(data => {
                    const container = document.getElementById('receivedMessages');
                    container.innerHTML = data.length === 0 ? '<p>暂无消息</p>' : 
                        data.map(item => \`
                            <div class="message-item">
                                <div class="message-header">
                                    <span>时间: \${item.timestamp}</span>
                                </div>
                                <div class="message-body">\${JSON.stringify(item.message, null, 2)}</div>
                            </div>
                        \`).join('');
                });
            
            // 加载发送的消息
            fetch('/api/messages/sent')
                .then(res => res.json())
                .then(data => {
                    const container = document.getElementById('sentMessages');
                    container.innerHTML = data.length === 0 ? '<p>暂无消息</p>' : 
                        data.map(item => \`
                            <div class="message-item sent">
                                <div class="message-header">
                                    <span>时间: \${item.timestamp}</span>
                                </div>
                                <div class="message-body">\${JSON.stringify(item.message, null, 2)}</div>
                            </div>
                        \`).join('');
                });
        }
        
        function loadImages() {
            fetch('/api/images')
                .then(res => res.json())
                .then(data => {
                    const container = document.getElementById('imageGrid');
                    container.innerHTML = data.length === 0 ? '<p>暂无图片</p>' : 
                        data.map(item => \`
                            <div class="image-item">
                                <img src="\${item.url}" alt="\${item.filename}">
                                <div class="image-info">
                                    <div>文件名: \${item.filename}</div>
                                    <div>大小: \${(item.size / 1024).toFixed(2)} KB</div>
                                    <div>时间: \${item.timestamp}</div>
                                </div>
                            </div>
                        \`).join('');
                });
        }
        
        function loadAudio() {
            fetch('/api/audio')
                .then(res => res.json())
                .then(data => {
                    const container = document.getElementById('audioList');
                    container.innerHTML = data.length === 0 ? '<p>暂无音频</p>' : 
                        data.map(item => \`
                            <div class="audio-item">
                                <div class="audio-info">
                                    <div><strong>\${item.filename}</strong></div>
                                    <div>大小: \${(item.size / 1024).toFixed(2)} KB</div>
                                    <div>时间: \${item.timestamp}</div>
                                </div>
                                <audio controls>
                                    <source src="\${item.url}" type="audio/opus">
                                    您的浏览器不支持音频播放
                                </audio>
                            </div>
                        \`).join('');
                });
        }
        
        function sendMessage(event) {
            event.preventDefault();
            const input = document.getElementById('messageInput');
            const messageText = input.value.trim();
            
            if (!messageText) {
                alert('请输入JSON消息');
                return;
            }
            
            try {
                const message = JSON.parse(messageText);
                fetch('/api/send', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(message)
                })
                .then(res => res.json())
                .then(data => {
                    if (data.success) {
                        alert('消息发送成功');
                        input.value = '';
                        loadMessages();
                    } else {
                        alert('发送失败: ' + data.error);
                    }
                });
            } catch (error) {
                alert('JSON格式错误: ' + error.message);
            }
        }
        
        // 检查连接状态
        function checkStatus() {
            fetch('/api/messages/received')
                .then(() => {
                    document.getElementById('status').textContent = '连接状态: 服务器运行中';
                    document.getElementById('status').classList.remove('disconnected');
                })
                .catch(() => {
                    document.getElementById('status').textContent = '连接状态: 服务器未响应';
                    document.getElementById('status').classList.add('disconnected');
                });
        }
        
        // 初始化
        checkStatus();
        loadMessages();
        setInterval(checkStatus, 5000);
        setInterval(loadMessages, 2000); // 每2秒刷新消息
    </script>
</body>
</html>`;
}

// 启动HTTP服务器
server.listen(HTTP_PORT, HOST, () => {
    console.log(`\n🌐 HTTP 服务器已启动`);
    console.log(`   Web UI: http://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${HTTP_PORT}`);
});

console.log('\n' + '='.repeat(60));
console.log('🚀 WebSocket 测试服务器');
console.log('='.repeat(60));
console.log(`WebSocket: ws://${HOST}:${WS_PORT}`);
console.log(`HTTP UI:  http://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${HTTP_PORT}`);
console.log('='.repeat(60));

