#!/usr/bin/env node
/**
 * 多媒体消息处理服务器
 * 处理机器人通信协议的JSON消息、OPUS音频和JPEG图片
 */

const WebSocket = require('ws');
const http = require('http');
const fs = require('fs');
const path = require('path');

// 服务器配置
const WS_PORT = 8080;
const HTTP_PORT = 3000;
const HOST = '0.0.0.0';

// 媒体存储配置
const MEDIA_DIR = path.join(__dirname, 'media');
const AUDIO_DIR = path.join(MEDIA_DIR, 'audio');
const IMAGES_DIR = path.join(MEDIA_DIR, 'images');

// 消息存储
let receivedMessages = [];
let sentMessages = [];
const MAX_MESSAGES = 500; // 保存更多消息用于调试

// 媒体文件存储
let audioFiles = [];
let imageFiles = [];
const MAX_MEDIA_FILES = 100; // 每个类型最多保存的文件数

// 当前连接的客户端
let currentClient = null;
let activeAudioRecording = null; // 追踪当前正在追加的音频文件信息
let audioPacketCount = 0; // 音频包计数器
let lastAudioLogTime = 0; // 上次音频日志输出时间
const AUDIO_LOG_INTERVAL = 1000; // 音频日志输出间隔（毫秒）
const AUDIO_LOG_PACKET_INTERVAL = 10; // 音频日志输出包间隔

// --- Ogg Opus 封装辅助函数 ---
const OGG_CRC_TABLE = new Uint32Array(256);
for (let i = 0; i < 256; i++) {
    let r = i << 24;
    for (let j = 0; j < 8; j++) r = (r & 0x80000000) ? (r << 1) ^ 0x04c11db7 : (r << 1);
    OGG_CRC_TABLE[i] = r;
}
function oggCrc(buf) {
    let crc = 0;
    for (let i = 0; i < buf.length; i++) crc = (crc << 8) ^ OGG_CRC_TABLE[((crc >>> 24) ^ buf[i]) & 0xff];
    return crc >>> 0;
}

// 根据 Opus 帧数据计算样本数
// ESP32 端配置：16kHz 采样率，20ms 帧时长（固定）
// 因此每个 Opus 帧 = 16000 * 0.02 = 320 样本
// 注意：帧大小可能因 VBR（可变比特率）而变化，但帧时长固定为 20ms
function getOpusFrameSamples(frameData) {
    if (!frameData || frameData.length === 0) {
        return 320; // 默认 20ms @ 16kHz
    }
    
    // 解析 Opus TOC (Table of Contents) 字节
    // TOC 字节结构：
    // bit 0-2: 配置编号 (0-7)
    // bit 3: 立体声标志
    // bit 4-7: 编码模式
    
    // 对于单声道 16kHz Opus，帧时长通常是固定的
    // 但我们可以通过检查帧大小来验证
    
    // ESP32 配置：20ms 帧 @ 16kHz = 320 样本
    // 单个 20ms Opus 帧大小通常在 20-80 字节之间（取决于比特率）
    
    const frameSize = frameData.length;
    
    // 如果帧大小异常小（< 10 字节），可能不是有效的 Opus 帧
    if (frameSize < 10) {
        console.warn(`⚠️ 警告: Opus 帧大小异常小 (${frameSize} 字节)，使用默认值`);
        return 320;
    }
    
    // 如果帧大小 > 200 字节，可能包含多个帧
    // 但根据 ESP32 配置，应该是单个 20ms 帧
    if (frameSize > 200) {
        console.warn(`⚠️ 警告: Opus 帧大小异常大 (${frameSize} 字节)，可能包含多个帧`);
        // 估算帧数：每 40-50 字节一个帧
        const estimatedFrames = Math.max(1, Math.floor(frameSize / 45));
        return 320 * estimatedFrames;
    }
    
    // 正常情况：单个 20ms 帧
    return 320;
}

function createOggPage(serial, seq, granule, packets, isHeader = 0) {
    // 构建段表和分割数据包
    // Ogg 格式要求：每个段最多 255 字节，如果包 > 255 字节需要分割
    const segments = [];
    const pageData = [];
    
    for (const packet of packets) {
        if (packet.length <= 255) {
            // 包 <= 255 字节，直接作为一个段
            segments.push(packet.length);
            pageData.push(packet);
        } else {
            // 包 > 255 字节，需要分割成多个段
            let offset = 0;
            while (offset < packet.length) {
                const segmentSize = Math.min(packet.length - offset, 255);
                segments.push(segmentSize);
                pageData.push(packet.slice(offset, offset + segmentSize));
                offset += segmentSize;
            }
        }
    }
    
    const segmentTable = Buffer.from(segments);
    const header = Buffer.alloc(27);
    header.write('OggS', 0);
    header.writeUInt8(0, 4); // version
    header.writeUInt8(isHeader, 5); // header type
    header.writeBigUInt64LE(BigInt(granule), 6);
    header.writeUInt32LE(serial, 14);
    header.writeUInt32LE(seq, 18);
    header.writeUInt32LE(0, 22); // checksum placeholder
    header.writeUInt8(segments.length, 26);
    
    const page = Buffer.concat([header, segmentTable, ...pageData]);
    page.writeUInt32LE(oggCrc(page), 22);
    return page;
}
// ----------------------------

// 初始化媒体存储目录
function initMediaDirectories() {
    try {
        if (!fs.existsSync(MEDIA_DIR)) {
            fs.mkdirSync(MEDIA_DIR, { recursive: true });
            console.log(`📁 创建媒体目录: ${MEDIA_DIR}`);
        }
        if (!fs.existsSync(AUDIO_DIR)) {
            fs.mkdirSync(AUDIO_DIR, { recursive: true });
            console.log(`🎵 创建音频目录: ${AUDIO_DIR}`);
        }
        if (!fs.existsSync(IMAGES_DIR)) {
            fs.mkdirSync(IMAGES_DIR, { recursive: true });
            console.log(`📷 创建图片目录: ${IMAGES_DIR}`);
        }
    } catch (error) {
        console.error('❌ 创建媒体目录失败:', error);
    }
}

// 获取ISO 8601格式的时间戳
function getTimestamp() {
    return new Date().toISOString().replace(/\.\d{3}Z$/, 'Z');
}

// 保存收到的消息
function saveReceivedMessage(message) {
    receivedMessages.push({
        message: message,
        timestamp: getTimestamp(),
        direction: 'received'
    });

    // 只保留最新的消息
    if (receivedMessages.length > MAX_MESSAGES) {
        receivedMessages = receivedMessages.slice(-MAX_MESSAGES);
    }
}

// 保存发送的消息
function saveSentMessage(message) {
    sentMessages.push({
        message: message,
        timestamp: getTimestamp(),
        direction: 'sent'
    });

    // 只保留最新的消息
    if (sentMessages.length > MAX_MESSAGES) {
        sentMessages = sentMessages.slice(-MAX_MESSAGES);
    }
}

// 保存音频文件
function saveAudioFile(data, filename = null) {
    try {
        // 确保目录存在
        if (!fs.existsSync(AUDIO_DIR)) {
            fs.mkdirSync(AUDIO_DIR, { recursive: true });
        }

        const timestamp = Date.now();
        const audioFilename = filename || `audio_${timestamp}.opus`;
        const filepath = path.join(AUDIO_DIR, audioFilename);

        // 生成随机序列号用于 Ogg 流
        const serial = Math.floor(Math.random() * 0x7FFFFFFF);
        
        // 1. 创建 OpusHead 页 (ID Header)
        const opusHead = Buffer.alloc(19);
        opusHead.write('OpusHead', 0);
        opusHead.writeUInt8(1, 8); // version
        opusHead.writeUInt8(1, 9); // channels (单声道)
        opusHead.writeUInt16LE(80, 10); // pre-skip: 80 样本 = 5ms @ 16kHz (Opus 编码器延迟)
        opusHead.writeUInt32LE(16000, 12); // original sample rate
        opusHead.writeUInt16LE(0, 16); // gain
        opusHead.writeUInt8(0, 18); // mapping family
        const page1 = createOggPage(serial, 0, 0, [opusHead], 0x02);

        // 2. 创建 OpusTags 页 (Comment Header)
        const opusTags = Buffer.alloc(16);
        opusTags.write('OpusTags', 0);
        opusTags.writeUInt32LE(0, 8); // vendor length
        opusTags.writeUInt32LE(0, 12); // user comment list length
        const page2 = createOggPage(serial, 1, 0, [opusTags]);

        // 3. 写入文件头和第一个数据包
        const firstFrameSamples = getOpusFrameSamples(data);
        const page3 = createOggPage(serial, 2, firstFrameSamples, [data]);
        fs.writeFileSync(filepath, Buffer.concat([page1, page2, page3]));

        const fileInfo = {
            filename: audioFilename,
            filepath: filepath,
            size: data.length,
            timestamp: getTimestamp(),
            type: 'audio',
            format: 'opus',
            serial: serial,   // 记录序列号供后续追加
            seq: 3,           // 记录页序号
            granule: firstFrameSamples  // 记录总样本数
        };

        audioFiles.push(fileInfo);

        // 只保留最新的文件
        if (audioFiles.length > MAX_MEDIA_FILES) {
            // 删除最旧的文件
            const oldFile = audioFiles.shift();
            try {
                fs.unlinkSync(oldFile.filepath);
            } catch (error) {
                console.warn('删除旧音频文件失败:', error);
            }
        }

        console.log(`🎵 保存音频文件: ${audioFilename} (${data.length} 字节)`);
        return fileInfo;
    } catch (error) {
        console.error('❌ 保存音频文件失败:', error);
        return null;
    }
}

// 保存图片文件
function saveImageFile(data, filename = null) {
    try {
        // 确保目录存在
        if (!fs.existsSync(IMAGES_DIR)) {
            fs.mkdirSync(IMAGES_DIR, { recursive: true });
        }

        const timestamp = Date.now();
        const imageFilename = filename || `image_${timestamp}.jpg`;
        const filepath = path.join(IMAGES_DIR, imageFilename);

        fs.writeFileSync(filepath, data);

        const fileInfo = {
            filename: imageFilename,
            filepath: filepath,
            size: data.length,
            timestamp: getTimestamp(),
            type: 'image',
            format: 'jpeg'
        };

        imageFiles.push(fileInfo);

        // 只保留最新的文件
        if (imageFiles.length > MAX_MEDIA_FILES) {
            // 删除最旧的文件
            const oldFile = imageFiles.shift();
            try {
                fs.unlinkSync(oldFile.filepath);
            } catch (error) {
                console.warn('删除旧图片文件失败:', error);
            }
        }

        console.log(`📷 保存图片文件: ${imageFilename} (${data.length} 字节)`);
        return fileInfo;
    } catch (error) {
        console.error('❌ 保存图片文件失败:', error);
        return null;
    }
}

// 获取媒体文件列表
function getMediaFiles() {
    return {
        audio: audioFiles,
        images: imageFiles
    };
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

// 格式化消息类型显示
function getMessageTypeDescription(type) {
    const descriptions = {
        'transport_info': '📊 数据上传',
        'bluetooth_info': '📱 蓝牙信息',
        'recv_info': '⚙️ 数据接收控制',
        'mov_info': '🎮 运动控制',
        'play': '🔊 音频播放',
        'emotion': '😊 情绪反馈',
        'error': '❌ 错误信息'
    };
    return descriptions[type] || `❓ 未知类型 (${type})`;
}

// 打印JSON消息到控制台
function printMessage(direction, message) {
    console.log('\n' + '='.repeat(80));
    console.log(`${direction} ${getMessageTypeDescription(message.type)}`);
    console.log('='.repeat(80));
    console.log(`时间: ${getTimestamp()}`);
    console.log(`类型: ${message.type}`);
    console.log(`来源: ${message.from || 'N/A'}`);
    console.log(`目标: ${message.to || 'N/A'}`);
    console.log(`时间戳: ${message.timestamp || 'N/A'}`);

    // 根据消息类型显示特定字段
    switch (message.type) {
        case 'transport_info':
            if (message.command) {
                console.log(`控制位: ${message.command} (${parseControlBits(message.command)})`);
            }
            if (message.data) {
                console.log(`触摸: ${message.data.touch}`);

                // 处理压力传感器阵列（16个点位）
                if (Array.isArray(message.data.pressure) && message.data.pressure.length === 16) {
                    const activePoints = message.data.pressure
                        .map((value, index) => ({ index, value }))
                        .filter(p => p.value > 0);

                    if (activePoints.length > 0) {
                        console.log(`压力阵列: ${activePoints.length}个点位有压力`);
                        activePoints.forEach(p => {
                            console.log(`  点位${p.index}: ${p.value.toFixed(2)} Pa`);
                        });
                    } else {
                        console.log(`压力阵列: 无压力数据`);
                    }
                } else {
                    console.log(`压力: ${message.data.pressure}`);
                }

                console.log(`陀螺仪: [${message.data.gyroscope?.x?.toFixed(2)}, ${message.data.gyroscope?.y?.toFixed(2)}, ${message.data.gyroscope?.z?.toFixed(2)}]`);
                console.log(`光敏: ${message.data.photosensitive}`);
            }
            break;

        case 'recv_info':
            if (message.command) {
                console.log(`控制位: ${message.command} (${parseControlBits(message.command)})`);
            }
            break;

        case 'mov_info':
            if (message.data) {
                console.log(`舵机控制:`);
                Object.keys(message.data).forEach(servoName => {
                    const servo = message.data[servoName];
                    console.log(`  ${servoName}: ${servo.move_part}, 角度:${servo.angle}°, 持续时间:${servo.duration}ms`);
                });
            }
            break;

        case 'bluetooth_info':
            if (message.data) {
                console.log(`蓝牙数据: ${JSON.stringify(message.data)}`);
            }
            break;

        case 'play':
            if (message.audio_format) {
                console.log(`音频格式: ${message.audio_format}`);
            }
            break;

        case 'emotion':
            if (message.code !== undefined) {
                const emotions = {
                    '0': '开心的',
                    '1': '伤心的',
                    '2': '生气的',
                    '3': '平淡的',
                    '4': '恐惧的',
                    '5': '惊讶的',
                    '6': '未知的'
                };
                console.log(`情绪代码: ${message.code} (${emotions[message.code] || '未知'})`);
            }
            break;

        case 'error':
            if (message.data) {
                console.log(`错误码: ${message.data.code}`);
                console.log(`错误信息: ${message.data.message}`);
            }
            break;
    }

    console.log('='.repeat(80));
}

// 解析控制位
function parseControlBits(command) {
    if (!command || typeof command !== 'string') return '无效';

    const bits = command.split('').map(b => parseInt(b));
    const descriptions = ['触摸', '压力', '陀螺仪', '光敏', '摄像头'];

    let result = '';
    bits.forEach((bit, index) => {
        if (bit === 1 && descriptions[index]) {
            result += descriptions[index] + ' ';
        }
    });

    return result.trim() || '无';
}

// 处理接收到的消息
function handleMessage(ws, data, isBinary = false) {
    // 如果是二进制数据，需要先判断类型
    if (isBinary || Buffer.isBuffer(data)) {
        // 检查是否是JPEG图片（以FFD8FF开头）
        if (data.length >= 3 && data[0] === 0xFF && data[1] === 0xD8 && data[2] === 0xFF) {
            console.log(`📷 收到JPEG图片 (${data.length} 字节)`);
            saveImageFile(data);
            return;
        }

        // 其他二进制数据当作音频处理（OPUS格式）
        if (activeAudioRecording) {
            // 如果已有活跃录制，则将数据封装为 Ogg 页并追加到文件末尾
            try {
                // 根据 Opus 帧大小计算实际的样本数
                const frameSamples = getOpusFrameSamples(data);
                activeAudioRecording.granule += frameSamples;
                
                const page = createOggPage(
                    activeAudioRecording.serial, 
                    activeAudioRecording.seq++, 
                    activeAudioRecording.granule, 
                    [data]
                );
                fs.appendFileSync(activeAudioRecording.filepath, page);
                activeAudioRecording.size += page.length;
                audioPacketCount++;
                
                // 定期输出日志：每N个包或每N秒输出一次
                const now = Date.now();
                const shouldLog = (audioPacketCount % AUDIO_LOG_PACKET_INTERVAL === 0) || 
                                 (now - lastAudioLogTime >= AUDIO_LOG_INTERVAL);
                
                if (shouldLog) {
                    const duration = ((activeAudioRecording.granule / 16000) * 1000).toFixed(0); // 毫秒
                    const frameSamples = getOpusFrameSamples(data);
                    const toc = data.length > 0 ? data[0] : 0;
                    const config = toc & 0x07;
                    console.log(`🎵 音频录制中: ${audioPacketCount} 包 | ${(activeAudioRecording.size / 1024).toFixed(1)} KB | ${duration}ms | 最新帧: ${data.length}B, TOC=0x${toc.toString(16)}, 配置${config}, ${frameSamples}样本, granule=${activeAudioRecording.granule}`);
                    lastAudioLogTime = now;
                }
            } catch (err) {
                console.error('❌ 追加音频数据失败:', err);
            }
        } else {
            // 如果没有活跃录制，创建一个新文件作为起始包
            console.log(`🎵 收到新音轨首包 (${data.length} 字节)`);
            activeAudioRecording = saveAudioFile(data);
            audioPacketCount = 0;
            lastAudioLogTime = Date.now();
        }
        return;
    }

    // 处理文本数据（JSON）
    const result = parseJSON(data);

    if (!result.success) {
        console.error('❌ JSON 解析失败:', result.error);
        console.error('原始数据:', data.toString());
        return;
    }

    const message = result.message;

    // 收到 listen 消息时，结束当前录制，准备下次二进制包创建新文件
    if (message.type === 'listen') {
        console.log('🎤 收到 listen 指令：准备开始新一轮音频采集');
        activeAudioRecording = null;
        audioPacketCount = 0;
        lastAudioLogTime = 0;
    }

    // 直接打印原始JSON字符串
    console.log('\n📥 接收 JSON:');
    console.log(JSON.stringify(message, null, 2));

    saveReceivedMessage(message);
}

// 发送JSON消息
function sendJSON(ws, message) {
    if (!ws || ws.readyState !== WebSocket.OPEN) {
        console.error('❌ WebSocket 未连接，无法发送消息');
        return false;
    }

    try {
        const jsonStr = JSON.stringify(message);
        ws.send(jsonStr);
        
        // 直接打印原始JSON字符串
        console.log('\n📤 发送 JSON:');
        console.log(JSON.stringify(message, null, 2));
        
        saveSentMessage(message);
        return true;
    } catch (error) {
        console.error('❌ JSON 序列化失败:', error);
        return false;
    }
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
        console.log('⚠️ 已有客户端连接，关闭旧连接');
        currentClient.close();
    }

    currentClient = ws;

    ws.on('message', (data, isBinary) => {
        // ESP32的WebSocket客户端即使用sendText发送，在Node.js中也可能被当作Buffer接收
        // 所以我们需要尝试将Buffer转换为字符串并解析JSON

        if (Buffer.isBuffer(data)) {
            // 尝试将Buffer转换为字符串
            try {
                const textData = data.toString('utf8');

                // 检查是否是有效的JSON（简单检查：以{或[开头）
                const trimmed = textData.trim();
                if (trimmed.startsWith('{') || trimmed.startsWith('[')) {
                    // 看起来是JSON文本，尝试解析
                    handleMessage(ws, textData, false);
                    return;
                }

                // 检查是否是JPEG图片（二进制数据）
                if (data.length >= 3 && data[0] === 0xFF && data[1] === 0xD8 && data[2] === 0xFF) {
                    // 直接处理为图片
                    handleMessage(ws, data, true);
                    return;
                }

                // 其他二进制数据当作音频处理
                handleMessage(ws, data, true);
            } catch (error) {
                console.error('❌ 处理消息时出错:', error);
            }
        } else if (typeof data === 'string') {
            // 已经是字符串，直接处理
            handleMessage(ws, data, false);
        } else {
            console.error(`❌ 未知的数据类型: ${typeof data}`);
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

// 创建HTTP服务器
const server = http.createServer((req, res) => {
    const url = req.url;

    // CORS 头
    res.setHeader('Access-Control-Allow-Origin', '*');
    res.setHeader('Access-Control-Allow-Methods', 'GET, POST, OPTIONS');
    res.setHeader('Access-Control-Allow-Headers', 'Content-Type');

    if (req.method === 'OPTIONS') {
        res.writeHead(200);
        res.end();
        return;
    }

    // API: 获取所有消息（收发合并）
    if (url === '/api/messages') {
        const allMessages = [
            ...receivedMessages.map(m => ({ ...m, direction: 'received' })),
            ...sentMessages.map(m => ({ ...m, direction: 'sent' }))
        ].sort((a, b) => new Date(a.timestamp) - new Date(b.timestamp));

        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(allMessages));
        return;
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
                    if (sendJSON(currentClient, message)) {
                        res.writeHead(200, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ success: true, message: '消息发送成功' }));
                    } else {
                        res.writeHead(500, { 'Content-Type': 'application/json' });
                        res.end(JSON.stringify({ success: false, error: '发送失败' }));
                    }
                } else {
                    res.writeHead(400, { 'Content-Type': 'application/json' });
                    res.end(JSON.stringify({ success: false, error: 'WebSocket未连接' }));
                }
            } catch (error) {
                res.writeHead(400, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({ success: false, error: 'JSON格式错误: ' + error.message }));
            }
        });
        return;
    }

    // API: 获取服务器状态
    if (url === '/api/status') {
        const status = {
            websocket: {
                port: WS_PORT,
                connected: currentClient && currentClient.readyState === WebSocket.OPEN,
                client_count: wss.clients.size
            },
            messages: {
                received: receivedMessages.length,
                sent: sentMessages.length,
                total: receivedMessages.length + sentMessages.length
            },
            media: {
                audio: audioFiles.length,
                images: imageFiles.length,
                total: audioFiles.length + imageFiles.length
            },
            timestamp: getTimestamp()
        };
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(status));
        return;
    }

    // API: 清空消息历史
    if (url === '/api/clear' && req.method === 'POST') {
        receivedMessages = [];
        sentMessages = [];
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify({ success: true, message: '消息历史已清空' }));
        return;
    }

    // API: 获取媒体文件列表
    if (url === '/api/media') {
        const mediaFiles = getMediaFiles();
        res.writeHead(200, { 'Content-Type': 'application/json' });
        res.end(JSON.stringify(mediaFiles));
        return;
    }

    // API: 下载媒体文件
    if (url.startsWith('/api/media/')) {
        const filePath = url.replace('/api/media/', '');
        const fullPath = path.join(MEDIA_DIR, filePath);

        // 安全检查：确保文件在媒体目录内
        if (!fullPath.startsWith(MEDIA_DIR)) {
            res.writeHead(403, { 'Content-Type': 'text/plain' });
            res.end('Forbidden');
            return;
        }

        try {
            if (fs.existsSync(fullPath)) {
                const stat = fs.statSync(fullPath);
                const ext = path.extname(fullPath).toLowerCase();

                // 设置正确的Content-Type
                let contentType = 'application/octet-stream';
                if (ext === '.opus') {
                    contentType = 'audio/opus';
                } else if (ext === '.jpg' || ext === '.jpeg') {
                    contentType = 'image/jpeg';
                }

                res.writeHead(200, {
                    'Content-Type': contentType,
                    'Content-Length': stat.size,
                    'Cache-Control': 'public, max-age=3600'
                });

                const stream = fs.createReadStream(fullPath);
                stream.pipe(res);
            } else {
                res.writeHead(404, { 'Content-Type': 'text/plain' });
                res.end('File not found');
            }
        } catch (error) {
            console.error('读取媒体文件失败:', error);
            res.writeHead(500, { 'Content-Type': 'text/plain' });
            res.end('Internal server error');
        }
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
    <title>JSON消息服务器</title>
    <style>
        * {
            margin: 0;
            padding: 0;
            box-sizing: border-box;
        }

        body {
            font-family: -apple-system, BlinkMacSystemFont, 'Segoe UI', Roboto, 'Microsoft YaHei', sans-serif;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            min-height: 100vh;
            padding: 20px;
        }

        .container {
            max-width: 1400px;
            margin: 0 auto;
            background: #fff;
            border-radius: 12px;
            box-shadow: 0 10px 30px rgba(0,0,0,0.3);
            overflow: hidden;
        }

        .header {
            background: linear-gradient(135deg, #4facfe 0%, #00f2fe 100%);
            color: white;
            padding: 30px;
            text-align: center;
        }

        .header h1 {
            font-size: 2.5em;
            margin-bottom: 10px;
            text-shadow: 0 2px 4px rgba(0,0,0,0.3);
        }

        .header p {
            font-size: 1.1em;
            opacity: 0.9;
        }

        .status-bar {
            padding: 15px 30px;
            background: #f8f9fa;
            border-bottom: 1px solid #e9ecef;
            display: flex;
            justify-content: space-between;
            align-items: center;
        }

        .status-item {
            display: flex;
            align-items: center;
            gap: 8px;
        }

        .status-dot {
            width: 12px;
            height: 12px;
            border-radius: 50%;
            background: #dc3545;
        }

        .status-dot.connected {
            background: #28a745;
        }

        .tabs {
            display: flex;
            background: #f8f9fa;
            border-bottom: 1px solid #e9ecef;
            flex-wrap: wrap;
        }

        .tab {
            padding: 15px 25px;
            cursor: pointer;
            background: transparent;
            border: none;
            border-bottom: 3px solid transparent;
            font-size: 16px;
            font-weight: 500;
            transition: all 0.3s;
            position: relative;
        }

        .tab:hover {
            background: rgba(0,123,255,0.1);
        }

        .tab.active {
            border-bottom-color: #007bff;
            color: #007bff;
            background: rgba(0,123,255,0.05);
        }

        .tab-content {
            display: none;
            padding: 30px;
            min-height: 600px;
        }

        .tab-content.active {
            display: block;
        }

        .messages-container {
            display: grid;
            grid-template-columns: 1fr 1fr;
            gap: 30px;
            height: 600px;
        }

        .message-section {
            display: flex;
            flex-direction: column;
        }

        .message-section h3 {
            margin-bottom: 15px;
            color: #495057;
            border-bottom: 2px solid #e9ecef;
            padding-bottom: 8px;
        }

        .message-list {
            flex: 1;
            overflow-y: auto;
            border: 1px solid #e9ecef;
            border-radius: 8px;
            background: #f8f9fa;
            padding: 15px;
        }

        .message-item {
            margin-bottom: 15px;
            padding: 15px;
            background: #fff;
            border-radius: 8px;
            border-left: 4px solid #007bff;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            transition: transform 0.2s;
        }

        .message-item:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0,0,0,0.15);
        }

        .message-item.sent {
            border-left-color: #28a745;
        }

        .message-item.error {
            border-left-color: #dc3545;
        }

        .message-header {
            display: flex;
            justify-content: space-between;
            align-items: center;
            margin-bottom: 10px;
        }

        .message-type {
            font-weight: bold;
            font-size: 14px;
            color: #007bff;
        }

        .message-item.sent .message-type {
            color: #28a745;
        }

        .message-item.error .message-type {
            color: #dc3545;
        }

        .message-time {
            font-size: 12px;
            color: #6c757d;
        }

        .message-body {
            font-family: 'JetBrains Mono', 'Fira Code', 'Courier New', monospace;
            font-size: 13px;
            background: #f8f9fa;
            padding: 10px;
            border-radius: 4px;
            border: 1px solid #e9ecef;
            white-space: pre-wrap;
            word-break: break-all;
            max-height: 200px;
            overflow-y: auto;
        }

        .send-section {
            max-width: 800px;
            margin: 0 auto;
        }

        .send-section h3 {
            margin-bottom: 20px;
            color: #495057;
            text-align: center;
        }

        .message-templates {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(200px, 1fr));
            gap: 15px;
            margin-bottom: 30px;
        }

        .template-btn {
            padding: 12px;
            background: #007bff;
            color: white;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-size: 14px;
            transition: background 0.3s;
        }

        .template-btn:hover {
            background: #0056b3;
        }

        .send-form {
            display: flex;
            flex-direction: column;
            gap: 15px;
        }

        .send-form textarea {
            width: 100%;
            min-height: 300px;
            padding: 15px;
            border: 2px solid #e9ecef;
            border-radius: 8px;
            font-family: 'JetBrains Mono', 'Fira Code', 'Courier New', monospace;
            font-size: 14px;
            line-height: 1.5;
            resize: vertical;
            transition: border-color 0.3s;
        }

        .send-form textarea:focus {
            outline: none;
            border-color: #007bff;
            box-shadow: 0 0 0 3px rgba(0,123,255,0.1);
        }

        .form-actions {
            display: flex;
            gap: 15px;
            justify-content: center;
        }

        .btn {
            padding: 12px 24px;
            border: none;
            border-radius: 6px;
            cursor: pointer;
            font-size: 16px;
            font-weight: 500;
            transition: all 0.3s;
        }

        .btn-primary {
            background: #007bff;
            color: white;
        }

        .btn-primary:hover {
            background: #0056b3;
            transform: translateY(-1px);
        }

        .btn-secondary {
            background: #6c757d;
            color: white;
        }

        .btn-secondary:hover {
            background: #545b62;
        }

        .btn-success {
            background: #28a745;
            color: white;
        }

        .btn-success:hover {
            background: #218838;
        }

        .stats-section {
            padding: 30px;
        }

        .stats-grid {
            display: grid;
            grid-template-columns: repeat(auto-fit, minmax(250px, 1fr));
            gap: 20px;
        }

        .stat-card {
            background: #fff;
            padding: 20px;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            text-align: center;
        }

        .stat-number {
            font-size: 2.5em;
            font-weight: bold;
            color: #007bff;
            margin-bottom: 5px;
        }

        .stat-label {
            color: #6c757d;
            font-size: 14px;
        }

        .refresh-indicator {
            display: inline-block;
            width: 20px;
            height: 20px;
            border: 2px solid #f3f3f3;
            border-top: 2px solid #007bff;
            border-radius: 50%;
            animation: spin 1s linear infinite;
            margin-left: 10px;
        }

        @keyframes spin {
            0% { transform: rotate(0deg); }
            100% { transform: rotate(360deg); }
        }

        .empty-state {
            text-align: center;
            color: #6c757d;
            padding: 40px;
        }

        .empty-state i {
            font-size: 3em;
            margin-bottom: 15px;
            display: block;
        }

        .media-tabs {
            display: flex;
            margin-bottom: 20px;
            border-bottom: 1px solid #e9ecef;
        }

        .media-tab {
            padding: 10px 20px;
            background: transparent;
            border: none;
            border-bottom: 2px solid transparent;
            cursor: pointer;
            font-size: 16px;
            font-weight: 500;
            transition: all 0.3s;
        }

        .media-tab:hover {
            background: rgba(0,123,255,0.1);
        }

        .media-tab.active {
            border-bottom-color: #007bff;
            color: #007bff;
            background: rgba(0,123,255,0.05);
        }

        .media-content {
            display: none;
        }

        .media-content.active {
            display: block;
        }

        .media-list {
            display: grid;
            grid-template-columns: repeat(auto-fill, minmax(250px, 1fr));
            gap: 15px;
        }

        .media-item {
            background: #fff;
            border-radius: 8px;
            box-shadow: 0 2px 4px rgba(0,0,0,0.1);
            overflow: hidden;
            transition: transform 0.2s;
        }

        .media-item:hover {
            transform: translateY(-2px);
            box-shadow: 0 4px 8px rgba(0,0,0,0.15);
        }

        .media-preview {
            width: 100%;
            height: 150px;
            object-fit: cover;
            background: #f8f9fa;
        }

        .audio-preview {
            display: flex;
            align-items: center;
            justify-content: center;
            height: 150px;
            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
            color: white;
            font-size: 48px;
        }

        .media-info {
            padding: 15px;
        }

        .media-filename {
            font-weight: bold;
            margin-bottom: 5px;
            font-size: 14px;
            color: #495057;
            word-break: break-all;
        }

        .media-meta {
            font-size: 12px;
            color: #6c757d;
            margin-bottom: 3px;
        }

        .media-actions {
            margin-top: 10px;
            display: flex;
            gap: 5px;
        }

        .media-btn {
            padding: 5px 10px;
            border: none;
            border-radius: 4px;
            cursor: pointer;
            font-size: 12px;
            transition: all 0.2s;
        }

        .media-btn.download {
            background: #28a745;
            color: white;
        }

        .media-btn.download:hover {
            background: #218838;
        }

        .media-btn.play {
            background: #007bff;
            color: white;
        }

        .media-btn.play:hover {
            background: #0056b3;
        }

        /* 压力传感器阵列样式 */
        .pressure-section {
            margin: 20px 0;
            padding: 20px;
            background: #f8f9fa;
            border-radius: 8px;
        }

        .pressure-section h3 {
            margin-bottom: 15px;
            color: #495057;
            text-align: center;
        }

        .pressure-grid-container {
            display: grid;
            grid-template-columns: repeat(4, 1fr);
            grid-template-rows: repeat(4, 1fr);
            gap: 10px;
            max-width: 500px;
            margin: 0 auto;
            aspect-ratio: 1 / 1;
            padding: 20px;
            background-color: #f0f0f0;
            border-radius: 10px;
        }

        .pressure-grid-item {
            display: flex;
            flex-direction: column;
            justify-content: center;
            align-items: center;
            background-color: #ffffff;
            border: 2px solid #ddd;
            border-radius: 8px;
            font-size: 14px;
            font-weight: bold;
            color: #333;
            transition: all 0.3s ease;
            position: relative;
            overflow: hidden;
        }

        .pressure-grid-item .value {
            font-size: 20px;
            margin-bottom: 5px;
        }

        .pressure-grid-item .index {
            font-size: 11px;
            color: #666;
        }

        .pressure-info {
            text-align: center;
            margin-top: 15px;
            color: #6c757d;
            font-size: 14px;
        }

        @media (max-width: 768px) {
            .messages-container {
                grid-template-columns: 1fr;
                height: auto;
            }

            .header h1 {
                font-size: 2em;
            }

            .tabs {
                flex-wrap: wrap;
            }

            .tab {
                padding: 10px 15px;
                font-size: 14px;
            }

            .media-list {
                grid-template-columns: 1fr;
            }

            .media-preview, .audio-preview {
                height: 120px;
            }
        }
    </style>
</head>
<body>
    <div class="container">
        <div class="header">
            <h1>🤖 JSON消息服务器</h1>
            <p>机器人通信协议消息处理服务器</p>
        </div>

        <div class="status-bar">
            <div class="status-item">
                <div class="status-dot" id="wsStatus"></div>
                <span id="wsStatusText">WebSocket: 检查中...</span>
            </div>
            <div class="status-item">
                <span id="messageCount">消息: 0</span>
            </div>
            <div class="status-item">
                <button class="btn btn-secondary" onclick="clearMessages()">清空消息</button>
            </div>
        </div>

        <div class="tabs">
            <button class="tab active" onclick="switchTab('messages')">📨 消息监控</button>
            <button class="tab" onclick="switchTab('send')">📤 发送消息</button>
            <button class="tab" onclick="switchTab('media')">🎵 媒体文件</button>
            <button class="tab" onclick="switchTab('stats')">📊 统计信息</button>
        </div>

        <div id="messages" class="tab-content active">
            <div class="pressure-section">
                <h3>📊 压力传感器阵列（16宫格）</h3>
                <div class="pressure-grid-container" id="pressureGrid">
                    <!-- 动态生成16个压力传感器点位 -->
                </div>
                <div class="pressure-info">
                    <p>压力值单位：Pa，颜色从白色（无压力）到黑色（最大压力）渐变</p>
                    <p id="pressureInfo">等待数据...</p>
                </div>
            </div>
            <div class="messages-container">
                <div class="message-section">
                    <h3>📥 收到的消息</h3>
                    <div class="message-list" id="receivedMessages"></div>
                </div>
                <div class="message-section">
                    <h3>📤 发送的消息</h3>
                    <div class="message-list" id="sentMessages"></div>
                </div>
            </div>
        </div>

        <div id="send" class="tab-content">
            <div class="send-section">
                <h3>发送JSON消息</h3>

                <div class="message-templates">
                    <button class="template-btn" onclick="loadTemplate('recv_info')">数据接收控制</button>
                    <button class="template-btn" onclick="loadTemplate('mov_info')">运动控制</button>
                    <button class="template-btn" onclick="loadTemplate('play')">音频播放</button>
                    <button class="template-btn" onclick="loadTemplate('emotion')">情绪反馈</button>
                    <button class="template-btn" onclick="loadTemplate('error')">错误信息</button>
                </div>

                <form class="send-form" onsubmit="sendMessage(event)">
                    <textarea id="messageInput" placeholder='输入JSON消息，例如:
// 数据接收控制
{
  "type": "recv_info",
  "from": "server",
  "to": "xx:xx:xx:xx:xx:xx",
  "timestamp": "2025-01-01T00:00:00Z",
  "command": "11111"
}

// 运动控制
{
  "type": "mov_info",
  "from": "server",
  "to": "xx:xx:xx:xx:xx:xx",
  "timestamp": "2025-01-01T00:00:00Z",
  "data": {
    "servo_01": {
      "move_part": "h1",
      "start_time": "0",
      "angle": 90,
      "duration": 1000
    }
  }
}

// 情绪反馈
{
  "type": "emotion",
  "from": "server",
  "to": "xx:xx:xx:xx:xx:xx",
  "timestamp": "2025-01-01T00:00:00Z",
  "code": "0"
}'></textarea>
                    <div class="form-actions">
                        <button type="submit" class="btn btn-primary">发送消息</button>
                        <button type="button" class="btn btn-secondary" onclick="clearInput()">清空</button>
                    </div>
                </form>
            </div>
        </div>

        <div id="media" class="tab-content">
            <div class="stats-section">
                <h3 style="text-align: center; margin-bottom: 30px;">🎵 媒体文件管理</h3>

                <div class="media-tabs">
                    <button class="media-tab active" onclick="switchMediaTab('audio')">🎵 音频文件</button>
                    <button class="media-tab" onclick="switchMediaTab('images')">📷 图片文件</button>
                </div>

                <div id="audioTab" class="media-content active">
                    <h4>音频文件列表</h4>
                    <div id="audioList" class="media-list">
                        <!-- 动态生成音频文件列表 -->
                    </div>
                </div>

                <div id="imagesTab" class="media-content">
                    <h4>图片文件列表</h4>
                    <div id="imagesList" class="media-list">
                        <!-- 动态生成图片文件列表 -->
                    </div>
                </div>
            </div>
        </div>

        <div id="stats" class="tab-content">
            <div class="stats-section">
                <h3 style="text-align: center; margin-bottom: 30px;">服务器统计信息</h3>
                <div class="stats-grid" id="statsGrid">
                    <!-- 动态生成统计信息 -->
                </div>
            </div>
        </div>
    </div>

    <script>
        let refreshInterval;
        let lastPressureData = null;

        // 初始化压力传感器16宫格
        function initPressureGrid() {
            const gridContainer = document.getElementById('pressureGrid');
            if (!gridContainer) return;
            
            gridContainer.innerHTML = '';
            for (let i = 0; i < 16; i++) {
                const gridItem = document.createElement('div');
                gridItem.className = 'pressure-grid-item';
                gridItem.id = 'pressure-grid-' + i;
                gridItem.innerHTML = '<div class="value">0</div><div class="index">点位' + i + '</div>';
                gridContainer.appendChild(gridItem);
            }
        }

        // 根据压力值获取颜色（从白色到黑色的渐变）
        function getColorByPressure(value) {
            // 计算压力值归一化（0-1），假设最大压力为1000Pa
            const normalizedValue = Math.min(value / 1000, 1);
            
            // 根据压力值调整黑色参数：0=白色（无压力），1=黑色（最大压力）
            const blackLevel = Math.floor(normalizedValue * 255);
            
            // 计算最终颜色（从白色到黑色的渐变）
            const r = 255 - blackLevel;
            const g = 255 - blackLevel;
            const b = 255 - blackLevel;
            
            return 'rgb(' + r + ', ' + g + ', ' + b + ')';
        }

        // 更新压力传感器阵列显示
        function updatePressureGrid(pressureData, messageData) {
            if (!Array.isArray(pressureData) || pressureData.length !== 16) {
                return;
            }

            lastPressureData = {
                pressure: pressureData,
                timestamp: messageData && messageData.timestamp ? messageData.timestamp : new Date().toISOString(),
                from: messageData && messageData.from ? messageData.from : 'unknown'
            };

            // 更新16宫格
            for (let i = 0; i < 16; i++) {
                const value = pressureData[i] || 0;
                const gridItem = document.getElementById('pressure-grid-' + i);
                if (gridItem) {
                    // 更新数值
                    const valueElement = gridItem.querySelector('.value');
                    if (valueElement) {
                        valueElement.textContent = Math.round(value);
                    }
                    
                    // 更新背景颜色
                    const color = getColorByPressure(value);
                    gridItem.style.backgroundColor = color;
                    
                    // 更新文字颜色（根据背景色调整，确保可读性）
                    const rgbMatch = color.match(/\\d+/g);
                    if (rgbMatch && rgbMatch.length === 3) {
                        const r = parseInt(rgbMatch[0]);
                        const g = parseInt(rgbMatch[1]);
                        const b = parseInt(rgbMatch[2]);
                        const brightness = (r * 299 + g * 587 + b * 114) / 1000;
                        gridItem.style.color = brightness > 128 ? '#333' : '#fff';
                    }
                }
            }

            // 更新信息显示
            const pressureInfo = document.getElementById('pressureInfo');
            if (pressureInfo) {
                const activePoints = pressureData.filter(function(v) { return v > 0; }).length;
                const maxPressure = Math.max.apply(null, pressureData);
                const timestamp = messageData && messageData.timestamp ? new Date(messageData.timestamp).toLocaleString('zh-CN') : '未知';
                const deviceFrom = messageData && messageData.from ? messageData.from : '未知';
                pressureInfo.innerHTML = '设备: ' + deviceFrom + ' | 活跃点位: ' + activePoints + '/16 | 最大压力: ' + Math.round(maxPressure) + ' Pa | 更新时间: ' + timestamp;
            }
        }

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
            if (tabName === 'messages') {
                initPressureGrid();
                // 如果有上次的压力数据，恢复显示
                if (lastPressureData) {
                    updatePressureGrid(lastPressureData.pressure, lastPressureData);
                }
                loadMessages();
                startAutoRefresh();
            } else if (tabName === 'media') {
                loadMedia();
                stopAutoRefresh();
            } else if (tabName === 'stats') {
                loadStats();
                stopAutoRefresh();
            } else {
                stopAutoRefresh();
            }
        }

        function startAutoRefresh() {
            stopAutoRefresh();
            refreshInterval = setInterval(loadMessages, 1000); // 每秒刷新
        }

        function stopAutoRefresh() {
            if (refreshInterval) {
                clearInterval(refreshInterval);
                refreshInterval = null;
            }
        }

        function loadMessages() {
            // 加载收到的消息
            fetch('/api/messages/received')
                .then(res => res.json())
                .then(data => {
                    updateMessageList('receivedMessages', data, false);
                })
                .catch(err => console.error('加载收到的消息失败:', err));

            // 加载发送的消息
            fetch('/api/messages/sent')
                .then(res => res.json())
                .then(data => {
                    updateMessageList('sentMessages', data, true);
                })
                .catch(err => console.error('加载发送的消息失败:', err));
        }

        function updateMessageList(containerId, messages, isSent) {
            const container = document.getElementById(containerId);

            if (messages.length === 0) {
                container.innerHTML = '<div class="empty-state"><i>📭</i><div>暂无消息</div></div>';
                return;
            }

            // 查找最新的transport_info消息（仅处理收到的消息）
            if (!isSent && containerId === 'receivedMessages') {
                const transportMessages = messages.filter(item => {
                    const msg = item.message || item;
                    return msg.type === 'transport_info' && msg.data && Array.isArray(msg.data.pressure);
                });
                
                if (transportMessages.length > 0) {
                    // 获取最新的transport_info消息
                    const latestMessage = transportMessages[transportMessages.length - 1];
                    const message = latestMessage.message || latestMessage;
                    if (message.data && Array.isArray(message.data.pressure)) {
                        updatePressureGrid(message.data.pressure, message);
                    }
                }
            }

            container.innerHTML = messages.slice(-50).reverse().map(item => {
                const message = item.message || item;
                const timestamp = item.timestamp || new Date().toISOString();
                const typeClass = message.type === 'error' ? 'error' : (isSent ? 'sent' : '');

                return \`
                    <div class="message-item \${typeClass}">
                        <div class="message-header">
                            <span class="message-type">\${getMessageTypeText(message.type)}</span>
                            <span class="message-time">\${formatTime(timestamp)}</span>
                        </div>
                        <div class="message-body">\${JSON.stringify(message, null, 2)}</div>
                    </div>
                \`;
            }).join('');

            // 滚动到底部
            container.scrollTop = container.scrollHeight;
        }

        function getMessageTypeText(type) {
            const types = {
                'transport_info': '数据上传',
                'bluetooth_info': '蓝牙信息',
                'recv_info': '数据接收控制',
                'mov_info': '运动控制',
                'play': '音频播放',
                'emotion': '情绪反馈',
                'error': '错误信息'
            };
            return types[type] || type;
        }

        function formatTime(timestamp) {
            const date = new Date(timestamp);
            return date.toLocaleString('zh-CN', {
                year: 'numeric',
                month: '2-digit',
                day: '2-digit',
                hour: '2-digit',
                minute: '2-digit',
                second: '2-digit'
            });
        }

        function sendMessage(event) {
            event.preventDefault();
            const input = document.getElementById('messageInput');
            const messageText = input.value.trim();

            if (!messageText) {
                showNotification('请输入JSON消息', 'error');
                return;
            }

            try {
                const message = JSON.parse(messageText);
                showNotification('发送中...', 'info');

                fetch('/api/send', {
                    method: 'POST',
                    headers: { 'Content-Type': 'application/json' },
                    body: JSON.stringify(message)
                })
                .then(res => res.json())
                .then(data => {
                    if (data.success) {
                        showNotification('消息发送成功', 'success');
                        input.value = '';
                        loadMessages();
                    } else {
                        showNotification('发送失败: ' + data.error, 'error');
                    }
                })
                .catch(error => {
                    showNotification('网络错误: ' + error.message, 'error');
                });
            } catch (error) {
                showNotification('JSON格式错误: ' + error.message, 'error');
            }
        }

        function loadTemplate(type) {
            const templates = {
                'recv_info': {
                    "type": "recv_info",
                    "from": "server",
                    "to": "xx:xx:xx:xx:xx:xx",
                    "timestamp": new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
                    "command": "11111"
                },
                'mov_info': {
                    "type": "mov_info",
                    "from": "server",
                    "to": "xx:xx:xx:xx:xx:xx",
                    "timestamp": new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
                    "data": {
                        "servo_01": {
                            "move_part": "h1",
                            "start_time": "0",
                            "angle": 90,
                            "duration": 1000
                        },
                        "servo_02": {
                            "move_part": "b1",
                            "start_time": "2000",
                            "angle": 45,
                            "duration": 1500
                        }
                    }
                },
                'play': {
                    "type": "play",
                    "from": "server",
                    "to": "xx:xx:xx:xx:xx:xx",
                    "timestamp": new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
                    "audio_format": "opus"
                },
                'emotion': {
                    "type": "emotion",
                    "from": "server",
                    "to": "xx:xx:xx:xx:xx:xx",
                    "timestamp": new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
                    "code": "0"
                },
                'error': {
                    "type": "error",
                    "from": "server",
                    "to": "xx:xx:xx:xx:xx:xx",
                    "timestamp": new Date().toISOString().replace(/\.\d{3}Z$/, 'Z'),
                    "data": {
                        "code": 1000,
                        "message": "测试错误信息"
                    }
                }
            };

            const template = templates[type];
            if (template) {
                document.getElementById('messageInput').value = JSON.stringify(template, null, 2);
            }
        }

        function clearInput() {
            document.getElementById('messageInput').value = '';
        }

        function clearMessages() {
            if (confirm('确定要清空所有消息历史吗？')) {
                fetch('/api/clear', { method: 'POST' })
                    .then(res => res.json())
                    .then(data => {
                        if (data.success) {
                            showNotification('消息历史已清空', 'success');
                            loadMessages();
                            loadStats();
                        } else {
                            showNotification('清空失败', 'error');
                        }
                    });
            }
        }

        function switchMediaTab(tabName) {
            // 隐藏所有媒体内容
            document.querySelectorAll('.media-content').forEach(content => {
                content.classList.remove('active');
            });
            document.querySelectorAll('.media-tab').forEach(tab => {
                tab.classList.remove('active');
            });

            // 显示选中的媒体内容
            document.getElementById(tabName + 'Tab').classList.add('active');
            event.target.classList.add('active');

            // 加载媒体数据
            loadMedia();
        }

        function loadMedia() {
            fetch('/api/media')
                .then(res => res.json())
                .then(data => {
                    updateAudioList(data.audio || []);
                    updateImagesList(data.images || []);
                })
                .catch(err => console.error('加载媒体文件失败:', err));
        }

        function updateAudioList(audioFiles) {
            const container = document.getElementById('audioList');

            if (audioFiles.length === 0) {
                container.innerHTML = '<div class="empty-state"><i>🎵</i><div>暂无音频文件</div></div>';
                return;
            }

            container.innerHTML = audioFiles.map(file => \`
                <div class="media-item">
                    <div class="audio-preview">🎵</div>
                    <div class="media-info">
                        <div class="media-filename">\${file.filename}</div>
                        <div class="media-meta">大小: \${formatFileSize(file.size)}</div>
                        <div class="media-meta">时间: \${formatTime(file.timestamp)}</div>
                        <div class="media-actions">
                            <button class="media-btn download" onclick="downloadFile('audio/\${file.filename}')">下载</button>
                            <button class="media-btn play" onclick="playAudio('audio/\${file.filename}')">播放</button>
                        </div>
                    </div>
                </div>
            \`).join('');
        }

        function updateImagesList(imageFiles) {
            const container = document.getElementById('imagesList');

            if (imageFiles.length === 0) {
                container.innerHTML = '<div class="empty-state"><i>📷</i><div>暂无图片文件</div></div>';
                return;
            }

            container.innerHTML = imageFiles.map(file => \`
                <div class="media-item">
                    <img class="media-preview" src="/api/media/images/\${file.filename}" alt="\${file.filename}" loading="lazy">
                    <div class="media-info">
                        <div class="media-filename">\${file.filename}</div>
                        <div class="media-meta">大小: \${formatFileSize(file.size)}</div>
                        <div class="media-meta">时间: \${formatTime(file.timestamp)}</div>
                        <div class="media-actions">
                            <button class="media-btn download" onclick="downloadFile('images/\${file.filename}')">下载</button>
                        </div>
                    </div>
                </div>
            \`).join('');
        }

        function downloadFile(filepath) {
            const link = document.createElement('a');
            link.href = '/api/media/' + filepath;
            link.download = filepath.split('/').pop();
            document.body.appendChild(link);
            link.click();
            document.body.removeChild(link);
        }

        function playAudio(filepath) {
            const audio = new Audio('/api/media/' + filepath);
            audio.play().catch(err => {
                console.error('播放音频失败:', err);
                showNotification('播放音频失败: ' + err.message, 'error');
            });
        }

        function formatFileSize(bytes) {
            if (bytes === 0) return '0 B';
            const k = 1024;
            const sizes = ['B', 'KB', 'MB', 'GB'];
            const i = Math.floor(Math.log(bytes) / Math.log(k));
            return parseFloat((bytes / Math.pow(k, i)).toFixed(1)) + ' ' + sizes[i];
        }

        function loadStats() {
            fetch('/api/status')
                .then(res => res.json())
                .then(data => {
                    const statsGrid = document.getElementById('statsGrid');
                    statsGrid.innerHTML = \`
                        <div class="stat-card">
                            <div class="stat-number">\${data.websocket.connected ? '✅' : '❌'}</div>
                            <div class="stat-label">WebSocket连接状态</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-number">\${data.websocket.client_count}</div>
                            <div class="stat-label">连接的客户端数</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-number">\${data.messages.received}</div>
                            <div class="stat-label">收到的消息数</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-number">\${data.messages.sent}</div>
                            <div class="stat-label">发送的消息数</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-number">\${data.messages.total}</div>
                            <div class="stat-label">消息总数</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-number">\${Math.round(data.messages.total / Math.max(1, (new Date() - new Date(data.timestamp.replace('Z', '+00:00'))) / 1000 / 60))}</div>
                            <div class="stat-label">每分钟消息数</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-number">\${data.media.audio}</div>
                            <div class="stat-label">音频文件数</div>
                        </div>
                        <div class="stat-card">
                            <div class="stat-number">\${data.media.images}</div>
                            <div class="stat-label">图片文件数</div>
                        </div>
                    \`;

                    // 更新状态栏
                    const wsStatus = document.getElementById('wsStatus');
                    const wsStatusText = document.getElementById('wsStatusText');
                    const messageCount = document.getElementById('messageCount');

                    wsStatus.classList.toggle('connected', data.websocket.connected);
                    wsStatusText.textContent = \`WebSocket: \${data.websocket.connected ? '已连接' : '未连接'}\`;
                    messageCount.textContent = \`消息: \${data.messages.total}\`;
                });
        }

        function showNotification(message, type = 'info') {
            // 创建通知元素
            const notification = document.createElement('div');
            notification.style.cssText = \`
                position: fixed;
                top: 20px;
                right: 20px;
                padding: 15px 20px;
                border-radius: 6px;
                color: white;
                font-weight: 500;
                z-index: 1000;
                animation: slideIn 0.3s ease-out;
                max-width: 300px;
            \`;

            const colors = {
                success: '#28a745',
                error: '#dc3545',
                info: '#007bff',
                warning: '#ffc107'
            };

            notification.style.backgroundColor = colors[type] || colors.info;
            notification.textContent = message;

            document.body.appendChild(notification);

            // 3秒后自动移除
            setTimeout(() => {
                notification.style.animation = 'slideOut 0.3s ease-in';
                setTimeout(() => {
                    if (notification.parentNode) {
                        notification.parentNode.removeChild(notification);
                    }
                }, 300);
            }, 3000);
        }

        // 添加CSS动画
        const style = document.createElement('style');
        style.textContent = \`
            @keyframes slideIn {
                from { transform: translateX(100%); opacity: 0; }
                to { transform: translateX(0); opacity: 1; }
            }
            @keyframes slideOut {
                from { transform: translateX(0); opacity: 1; }
                to { transform: translateX(100%); opacity: 0; }
            }
        \`;
        document.head.appendChild(style);

        // 初始化
        initPressureGrid();
        loadMessages();
        loadStats();
        startAutoRefresh();

        // 页面关闭时停止自动刷新
        window.addEventListener('beforeunload', stopAutoRefresh);
    </script>
</body>
</html>`;
}

// 初始化媒体目录
initMediaDirectories();

// 启动HTTP服务器
server.listen(HTTP_PORT, HOST, () => {
    console.log(`\n🌐 HTTP 服务器已启动`);
    console.log(`   Web UI: http://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${HTTP_PORT}`);
});

console.log('\n' + '='.repeat(80));
console.log('🤖 多媒体消息处理服务器');
console.log('='.repeat(80));
console.log(`WebSocket: ws://${HOST}:${WS_PORT}`);
console.log(`HTTP UI:  http://${HOST === '0.0.0.0' ? 'localhost' : HOST}:${HTTP_PORT}`);
console.log('支持 JSON消息、OPUS音频、JPEG图片');
console.log('='.repeat(80));
