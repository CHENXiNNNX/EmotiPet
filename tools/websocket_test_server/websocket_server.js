#!/usr/bin/env node
/**
 * WebSocket 测试服务器
 * 用于验证设备端的 WebSocket JSON 消息通信
 */

const WebSocket = require('ws');
const readline = require('readline');

const PORT = 8080;
const HOST = '0.0.0.0';

// 创建 WebSocket 服务器
const wss = new WebSocket.Server({ 
    host: HOST,
    port: PORT 
});

// 当前连接的客户端
let currentClient = null;

// 获取 ISO 8601 格式的时间戳
function getTimestamp() {
    return new Date().toISOString().replace(/\.\d{3}Z$/, 'Z');
}

// 打印 JSON 消息
function printMessage(direction, message) {
    console.log('\n' + '='.repeat(60));
    console.log(`[${direction}] ${new Date().toLocaleString()}`);
    console.log('-'.repeat(60));
    try {
        const jsonStr = JSON.stringify(message, null, 2);
        console.log(jsonStr);
    } catch (e) {
        console.log(message);
    }
    console.log('='.repeat(60) + '\n');
}

// 验证并解析 JSON
function parseJSON(data) {
    try {
        const message = JSON.parse(data);
        return { success: true, message };
    } catch (e) {
        return { success: false, error: e.message };
    }
}

// 发送 hello_ack 消息
function sendHelloAck(ws) {
    const helloAck = {
        type: "hello_ack",
        version: 1.0,
        transport: "websocket",
        features: {
            aec: false,
            mcp: false
        }
    };
    
    printMessage('发送 (SEND)', helloAck);
    ws.send(JSON.stringify(helloAck));
}

// 发送命令消息
function sendCommand(ws, deviceId, cmd, soundId, reason) {
    const command = {
        type: "command",
        from: "server",
        to: deviceId || "xxx",
        timestamp: getTimestamp(),
        data: {
            cmd: cmd || "play_sound",
            sound_id: soundId || "4",
            reason: reason || "识别不出情绪，使用默认音效"
        }
    };
    
    printMessage('发送 (SEND)', command);
    ws.send(JSON.stringify(command));
}

// 发送资源同步消息
function sendResSync(ws, deviceId, data) {
    const resSync = {
        type: "res_sync",
        from: "server",
        to: deviceId || "xxx",
        timestamp: getTimestamp(),
        data: data || {}
    };
    
    printMessage('发送 (SEND)', resSync);
    ws.send(JSON.stringify(resSync));
}

// 发送错误消息
function sendError(ws, deviceId, code, message) {
    const error = {
        type: "error",
        from: "server",
        to: deviceId || "xxx",
        timestamp: getTimestamp(),
        data: {
            code: code || 1000,
            message: message || "错误描述信息"
        }
    };
    
    printMessage('发送 (SEND)', error);
    ws.send(JSON.stringify(error));
}

// 处理接收到的消息
function handleMessage(ws, data) {
    const result = parseJSON(data);
    
    if (!result.success) {
        console.error('❌ JSON 解析失败:', result.error);
        console.error('原始数据:', data.toString());
        return;
    }
    
    const message = result.message;
    printMessage('接收 (RECV)', message);
    
    // 根据消息类型处理
    switch (message.type) {
        case 'hello':
            console.log('✅ 收到 hello 消息，发送 hello_ack 回应...');
            sendHelloAck(ws);
            break;
            
        case 'res_sync':
            console.log('✅ 收到资源同步消息');
            // 可以在这里添加处理逻辑
            break;
            
        case 'error':
            console.log('⚠️  收到错误消息');
            break;
            
        case 'command':
            console.log('✅ 收到命令消息（通常不会从设备端收到）');
            break;
            
        default:
            console.log(`⚠️  未知消息类型: ${message.type}`);
    }
}

// WebSocket 连接处理
wss.on('connection', (ws, req) => {
    const clientAddr = req.socket.remoteAddress;
    console.log(`\n🔗 新客户端连接: ${clientAddr}`);
    
    // 如果已有连接，关闭旧连接
    if (currentClient && currentClient.readyState === WebSocket.OPEN) {
        console.log('⚠️  关闭旧连接');
        currentClient.close();
    }
    
    currentClient = ws;
    
    // 接收消息
    ws.on('message', (data) => {
        handleMessage(ws, data);
    });
    
    // 连接关闭
    ws.on('close', () => {
        console.log(`\n🔌 客户端断开连接: ${clientAddr}`);
        if (currentClient === ws) {
            currentClient = null;
        }
    });
    
    // 错误处理
    ws.on('error', (error) => {
        console.error('❌ WebSocket 错误:', error);
    });
    
    console.log(`✅ 客户端已连接，等待消息...\n`);
});

// 命令行交互界面
const rl = readline.createInterface({
    input: process.stdin,
    output: process.stdout,
    prompt: '> '
});

function showHelp() {
    console.log('\n可用命令:');
    console.log('  help              - 显示帮助');
    console.log('  hello_ack         - 发送 hello_ack 消息');
    console.log('  command [id]      - 发送命令消息 (play_sound)');
    console.log('  res_sync [id]     - 发送资源同步消息');
    console.log('  error [id] [code] - 发送错误消息');
    console.log('  status            - 显示连接状态');
    console.log('  exit              - 退出服务器\n');
}

function handleCommand(line) {
    const parts = line.trim().split(' ');
    const cmd = parts[0].toLowerCase();
    
    if (!currentClient || currentClient.readyState !== WebSocket.OPEN) {
        console.log('❌ 没有活动的客户端连接');
        return;
    }
    
    switch (cmd) {
        case 'help':
            showHelp();
            break;
            
        case 'hello_ack':
            sendHelloAck(currentClient);
            break;
            
        case 'command':
            const deviceId1 = parts[1] || 'xxx';
            sendCommand(currentClient, deviceId1);
            break;
            
        case 'res_sync':
            const deviceId2 = parts[1] || 'xxx';
            sendResSync(currentClient, deviceId2);
            break;
            
        case 'error':
            const deviceId3 = parts[1] || 'xxx';
            const code = parseInt(parts[2]) || 1000;
            sendError(currentClient, deviceId3, code);
            break;
            
        case 'status':
            console.log(`连接状态: ${currentClient ? '已连接' : '未连接'}`);
            break;
            
        case 'exit':
        case 'quit':
            console.log('正在关闭服务器...');
            wss.close();
            rl.close();
            process.exit(0);
            break;
            
        default:
            console.log(`未知命令: ${cmd}，输入 help 查看帮助`);
    }
}

// 启动服务器
console.log('🚀 WebSocket 测试服务器启动');
console.log(`📡 监听地址: ws://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${PORT}`);
console.log('\n输入 "help" 查看可用命令\n');

rl.on('line', (line) => {
    handleCommand(line);
    rl.prompt();
});

rl.on('close', () => {
    console.log('\n服务器关闭');
    process.exit(0);
});

rl.prompt();

// 优雅退出
process.on('SIGINT', () => {
    console.log('\n\n正在关闭服务器...');
    wss.close(() => {
        rl.close();
        process.exit(0);
    });
});

