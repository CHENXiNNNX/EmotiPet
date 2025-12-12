#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 OTA 升级服务器
提供 Web 界面和 API 接口用于 OTA 固件升级
"""

import os
import json
import hashlib
import zipfile
import logging
from datetime import datetime
from pathlib import Path
from flask import Flask, render_template, request, jsonify, send_file, send_from_directory
from werkzeug.utils import secure_filename
from werkzeug.exceptions import RequestEntityTooLarge

app = Flask(__name__)
app.config['MAX_CONTENT_LENGTH'] = 16 * 1024 * 1024
app.config['UPLOAD_FOLDER'] = 'firmware'
app.config['ALLOWED_EXTENSIONS'] = {'bin', 'bin.gz', 'zip'}

logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

os.makedirs(app.config['UPLOAD_FOLDER'], exist_ok=True)


def log_json_message(direction, endpoint, data):
    """打印JSON消息（请求或响应）"""
    try:
        json_str = json.dumps(data, ensure_ascii=False, indent=2)
        logger.info(f"[{direction}] {endpoint}\n{json_str}")
    except Exception as e:
        logger.warning(f"无法格式化JSON消息: {e}")


def allowed_file(filename):
    """检查文件扩展名是否允许"""
    return '.' in filename and \
           filename.rsplit('.', 1)[1].lower() in app.config['ALLOWED_EXTENSIONS']


def is_zip_file(filepath):
    """检查文件是否为zip格式"""
    try:
        return zipfile.is_zipfile(filepath)
    except:
        return False


def extract_bin_from_zip(zip_path, output_dir=None):
    """从zip文件中提取bin文件（如果存在）"""
    if not is_zip_file(zip_path):
        return None
    
    try:
        output_dir = output_dir or app.config['UPLOAD_FOLDER']
        extracted_files = []
        
        with zipfile.ZipFile(zip_path, 'r') as zip_ref:
            # 查找zip中的bin文件，优先查找主应用bin文件
            all_bin_files = [f for f in zip_ref.namelist() if f.endswith('.bin')]
            
            # 优先查找 EmotiPet.bin，否则使用第一个bin文件
            bin_file = None
            for f in all_bin_files:
                if 'EmotiPet.bin' in f or 'emotipet.bin' in f.lower():
                    bin_file = f
                    break
            
            if not bin_file and all_bin_files:
                bin_file = all_bin_files[0]
            
            if bin_file:
                # 只提取bin文件，不保留目录结构
                filename = os.path.basename(bin_file)
                # 如果zip文件名包含版本信息，尝试保留版本信息
                zip_basename = os.path.splitext(os.path.basename(zip_path))[0]
                if '_v' in zip_basename:
                    # 从zip文件名提取版本，生成新的bin文件名
                    try:
                        version = zip_basename.split('_v')[1]
                        name, ext = os.path.splitext(filename)
                        filename = f"{name}_v{version}{ext}"
                    except:
                        pass
                
                output_path = os.path.join(output_dir, filename)
                
                # 如果文件已存在，添加时间戳避免覆盖
                if os.path.exists(output_path):
                    name, ext = os.path.splitext(filename)
                    timestamp = datetime.now().strftime('%Y%m%d_%H%M%S')
                    output_path = os.path.join(output_dir, f"{name}_{timestamp}{ext}")
                
                with zip_ref.open(bin_file) as source, open(output_path, 'wb') as target:
                    target.write(source.read())
                
                extracted_files.append(output_path)
                logger.info(f"从zip提取bin文件: {bin_file} -> {output_path}")
        
        return extracted_files[0] if extracted_files else None
    except Exception as e:
        logger.warning(f"解压zip文件失败 {zip_path}: {e}")
        return None


def get_file_info(filepath):
    """获取文件信息"""
    stat = os.stat(filepath)
    return {
        'name': os.path.basename(filepath),
        'size': stat.st_size,
        'modified': datetime.fromtimestamp(stat.st_mtime).strftime('%Y-%m-%d %H:%M:%S'),
        'md5': calculate_md5(filepath)
    }


def get_file_info_with_version(filepath, version=None):
    """获取文件信息（包含版本号）"""
    info = get_file_info(filepath)
    # 如果没有提供版本号，尝试从文件名提取（格式：firmware_v1.0.0.bin 或 EmotiPet_v1.0.0.zip）
    if version is None:
        filename = info['name']
        if '_v' in filename:
            try:
                # 支持 .bin 和 .zip 格式
                if filename.endswith('.zip'):
                    version = filename.split('_v')[1].split('.zip')[0]
                else:
                    version = filename.split('_v')[1].split('.bin')[0]
            except:
                version = "1.0.0"
        else:
            version = "1.0.0"
    
    info['version'] = version
    info['info'] = f"固件版本 {version}"
    info['time'] = datetime.fromtimestamp(os.stat(filepath).st_mtime).strftime('%Y-%m-%dT%H:%M:%SZ')
    info['is_zip'] = filename.endswith('.zip')
    return info


def get_latest_firmware():
    """获取最新的固件文件"""
    firmware_dir = Path(app.config['UPLOAD_FOLDER'])
    files = []
    
    # 查找所有bin和zip文件
    for pattern in ['*.bin*', '*.zip']:
        for file_path in firmware_dir.glob(pattern):
            if file_path.is_file():
                files.append(str(file_path))
    
    if not files:
        return None
    
    # 按修改时间排序，返回最新的
    files.sort(key=lambda x: os.path.getmtime(x), reverse=True)
    return files[0]


def get_timestamp():
    """生成 ISO 8601 格式时间戳"""
    return datetime.utcnow().strftime('%Y-%m-%dT%H:%M:%SZ')


def calculate_md5(filepath):
    """计算文件的 MD5 值"""
    hash_md5 = hashlib.md5()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(4096), b""):
            hash_md5.update(chunk)
    return hash_md5.hexdigest()


@app.route('/')
def index():
    """主页面"""
    return render_template('index.html')


@app.route('/api/firmware/list', methods=['GET'])
def list_firmware():
    """获取固件列表"""
    firmware_dir = Path(app.config['UPLOAD_FOLDER'])
    files = []
    
    # 查找所有bin和zip文件
    for pattern in ['*.bin*', '*.zip']:
        for file_path in firmware_dir.glob(pattern):
            if file_path.is_file():
                files.append(get_file_info(str(file_path)))
    
    # 按修改时间排序
    files.sort(key=lambda x: x['modified'], reverse=True)
    
    return jsonify({
        'success': True,
        'files': files,
        'count': len(files)
    })


@app.route('/api/firmware/upload', methods=['POST'])
def upload_firmware():
    """上传固件文件"""
    if 'file' not in request.files:
        logger.warning("上传请求：没有文件")
        return jsonify({'success': False, 'error': '没有文件'}), 400
    
    file = request.files['file']
    if file.filename == '':
        logger.warning("上传请求：文件名为空")
        return jsonify({'success': False, 'error': '文件名为空'}), 400
    
    if not allowed_file(file.filename):
        logger.warning(f"上传请求：不支持的文件类型 - {file.filename}")
        return jsonify({'success': False, 'error': '不支持的文件类型，仅支持 .bin、.bin.gz 和 .zip'}), 400
    
    try:
        filename = secure_filename(file.filename)
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        file.save(filepath)
        logger.info(f"文件上传成功: {filename}")
        
        file_info = get_file_info(filepath)
        
        extracted_info = None
        if filename.endswith('.zip') and is_zip_file(filepath):
            extracted_bin = extract_bin_from_zip(filepath)
            if extracted_bin:
                extracted_info = get_file_info(extracted_bin)
                file_info['extracted_bin'] = extracted_info
        
        response = {
            'success': True,
            'message': '上传成功',
            'file': file_info,
            'extracted': extracted_info
        }
        return jsonify(response)
    except Exception as e:
        logger.error(f"上传文件失败: {e}", exc_info=True)
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/firmware/delete', methods=['POST'])
def delete_firmware():
    """删除固件文件"""
    data = request.get_json()
    filename = data.get('filename')
    
    if not filename:
        logger.warning("删除请求：文件名为空")
        return jsonify({'success': False, 'error': '文件名不能为空'}), 400
    
    filename = secure_filename(filename)
    filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
    
    if not os.path.exists(filepath):
        logger.warning(f"删除请求：文件不存在 - {filename}")
        return jsonify({'success': False, 'error': '文件不存在'}), 404
    
    try:
        os.remove(filepath)
        logger.info(f"文件删除成功: {filename}")
        return jsonify({'success': True, 'message': '删除成功'})
    except Exception as e:
        logger.error(f"删除文件失败: {e}", exc_info=True)
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/api/firmware/info', methods=['GET'])
def get_firmware_info():
    """获取固件文件信息"""
    filename = request.args.get('filename')
    
    if not filename:
        return jsonify({'success': False, 'error': '文件名不能为空'}), 400
    
    filename = secure_filename(filename)
    filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
    
    if not os.path.exists(filepath):
        return jsonify({'success': False, 'error': '文件不存在'}), 404
    
    try:
        file_info = get_file_info(filepath)
        return jsonify({'success': True, 'file': file_info})
    except Exception as e:
        return jsonify({'success': False, 'error': str(e)}), 500


@app.route('/firmware/<filename>')
def download_firmware(filename):
    """下载固件文件"""
    filename = secure_filename(filename)
    return send_from_directory(app.config['UPLOAD_FOLDER'], filename)


# ==================== 消息格式 API（设备端使用）====================

@app.route('/api/ota/check', methods=['POST'])
def ota_check_update():
    """检查更新（消息格式）"""
    try:
        data = request.get_json()
        log_json_message('REQUEST', '/api/ota/check', data)
        
        if not data:
            response = {
                'type': 'error',
                'from': 'ota_server',
                'to': 'unknown',
                'timestamp': get_timestamp(),
                'error': {
                    'code': 1000,
                    'message': '无效的请求格式'
                }
            }
            log_json_message('RESPONSE', '/api/ota/check', response)
            return jsonify(response), 400
        
        device_id = data.get('from', 'unknown')
        current_version = data.get('current_version', '1.0.0')
        
        latest_file = get_latest_firmware()
        if not latest_file:
            response = {
                'type': 'reply_update',
                'from': 'ota_server',
                'to': device_id,
                'timestamp': get_timestamp(),
                'respond': 0
            }
            log_json_message('RESPONSE', '/api/ota/check', response)
            return jsonify(response)
        
        latest_info = get_file_info_with_version(latest_file)
        latest_version = latest_info.get('version', '1.0.0')
        
        if latest_version > current_version:
            base_url = request.host_url.rstrip('/')
            download_filename = latest_info['name']
            file_type = 'bin'
            
            if latest_info.get('is_zip', False):
                extracted_bin = extract_bin_from_zip(latest_file)
                if extracted_bin:
                    download_filename = os.path.basename(extracted_bin)
                    file_type = 'bin'
                else:
                    download_filename = latest_info['name']
                    file_type = 'zip'
            
            download_url = f"{base_url}firmware/{download_filename}"
            response = {
                'type': 'reply_update',
                'from': 'ota_server',
                'to': device_id,
                'timestamp': get_timestamp(),
                'respond': 1,
                'download_url': download_url,
                'file_type': file_type
            }
            log_json_message('RESPONSE', '/api/ota/check', response)
            return jsonify(response)
        else:
            response = {
                'type': 'reply_update',
                'from': 'ota_server',
                'to': device_id,
                'timestamp': get_timestamp(),
                'respond': 0
            }
            log_json_message('RESPONSE', '/api/ota/check', response)
            return jsonify(response)
            
    except Exception as e:
        logger.error(f"检查更新失败: {e}", exc_info=True)
        response = {
            'type': 'error',
            'from': 'ota_server',
            'to': request.json.get('from', 'unknown') if request.json else 'unknown',
            'timestamp': get_timestamp(),
            'error': {
                'code': 1000,
                'message': str(e)
            }
        }
        log_json_message('RESPONSE', '/api/ota/check', response)
        return jsonify(response), 500


@app.route('/api/ota/info', methods=['POST'])
def ota_get_firmware_info():
    """获取固件信息（消息格式）"""
    try:
        data = request.get_json()
        log_json_message('REQUEST', '/api/ota/info', data)
        
        if not data:
            response = {
                'type': 'error',
                'from': 'ota_server',
                'to': 'unknown',
                'timestamp': get_timestamp(),
                'error': {
                    'code': 1000,
                    'message': '无效的请求格式'
                }
            }
            log_json_message('RESPONSE', '/api/ota/info', response)
            return jsonify(response), 400
        
        device_id = data.get('from', 'unknown')
        latest_file = get_latest_firmware()
        if not latest_file:
            response = {
                'type': 'error',
                'from': 'ota_server',
                'to': device_id,
                'timestamp': get_timestamp(),
                'error': {
                    'code': 1001,
                    'message': '文件不存在'
                }
            }
            log_json_message('RESPONSE', '/api/ota/info', response)
            return jsonify(response), 404
        
        file_info = get_file_info_with_version(latest_file)
        response = {
            'type': 'firmware_info',
            'from': 'ota_server',
            'to': device_id,
            'timestamp': get_timestamp(),
            'file': {
                'version': file_info['version'],
                'name': file_info['name'],
                'size': file_info['size'],
                'info': file_info['info'],
                'md5': file_info['md5'],
                'time': file_info['time']
            }
        }
        log_json_message('RESPONSE', '/api/ota/info', response)
        return jsonify(response)
        
    except Exception as e:
        logger.error(f"获取固件信息失败: {e}", exc_info=True)
        response = {
            'type': 'error',
            'from': 'ota_server',
            'to': request.json.get('from', 'unknown') if request.json else 'unknown',
            'timestamp': get_timestamp(),
            'error': {
                'code': 1000,
                'message': str(e)
            }
        }
        log_json_message('RESPONSE', '/api/ota/info', response)
        return jsonify(response), 500


@app.route('/api/ota/request', methods=['POST'])
def ota_request_firmware():
    """请求下载固件（消息格式）"""
    try:
        data = request.get_json()
        log_json_message('REQUEST', '/api/ota/request', data)
        
        if not data:
            response = {
                'type': 'error',
                'from': 'ota_server',
                'to': 'unknown',
                'timestamp': get_timestamp(),
                'error': {
                    'code': 1000,
                    'message': '无效的请求格式'
                }
            }
            log_json_message('RESPONSE', '/api/ota/request', response)
            return jsonify(response), 400
        
        device_id = data.get('from', 'unknown')
        request_data = data.get('data', {})
        filename = request_data.get('name')
        
        if not filename:
            response = {
                'type': 'error',
                'from': 'ota_server',
                'to': device_id,
                'timestamp': get_timestamp(),
                'error': {
                    'code': 1001,
                    'message': '文件名不能为空'
                }
            }
            log_json_message('RESPONSE', '/api/ota/request', response)
            return jsonify(response), 400
        
        filename = secure_filename(filename)
        filepath = os.path.join(app.config['UPLOAD_FOLDER'], filename)
        
        if not os.path.exists(filepath):
            response = {
                'type': 'error',
                'from': 'ota_server',
                'to': device_id,
                'timestamp': get_timestamp(),
                'error': {
                    'code': 1001,
                    'message': '文件不存在'
                }
            }
            log_json_message('RESPONSE', '/api/ota/request', response)
            return jsonify(response), 404
        
        base_url = request.host_url.rstrip('/')
        download_url = f"{base_url}firmware/{filename}"
        response = {
            'type': 'reply_update',
            'from': 'ota_server',
            'to': device_id,
            'timestamp': get_timestamp(),
            'respond': 2,
            'download_url': download_url
        }
        log_json_message('RESPONSE', '/api/ota/request', response)
        return jsonify(response)
        
    except Exception as e:
        logger.error(f"请求下载固件失败: {e}", exc_info=True)
        response = {
            'type': 'error',
            'from': 'ota_server',
            'to': request.json.get('from', 'unknown') if request.json else 'unknown',
            'timestamp': get_timestamp(),
            'error': {
                'code': 1000,
                'message': str(e)
            }
        }
        log_json_message('RESPONSE', '/api/ota/request', response)
        return jsonify(response), 500


@app.route('/api/ota/status', methods=['POST'])
def ota_report_status():
    """报告升级状态（消息格式）"""
    try:
        data = request.get_json()
        log_json_message('REQUEST', '/api/ota/status', data)
        
        if not data:
            response = {
                'type': 'error',
                'from': 'ota_server',
                'to': 'unknown',
                'timestamp': get_timestamp(),
                'error': {
                    'code': 1000,
                    'message': '无效的请求格式'
                }
            }
            log_json_message('RESPONSE', '/api/ota/status', response)
            return jsonify(response), 400
        
        device_id = data.get('from', 'unknown')
        status_data = data.get('data', {})
        status = status_data.get('status', 0)
        progress = status_data.get('progress', 0)
        current_version = status_data.get('current_version', '')
        target_version = status_data.get('target_version', '')
        
        status_names = {
            0: 'checking',
            1: 'downloading',
            2: 'verifying',
            3: 'completed',
            4: 'failed'
        }
        status_name = status_names.get(status, 'unknown')
        logger.info(f"OTA状态报告 - 设备: {device_id}, 状态: {status_name}, 进度: {progress}%, "
                   f"当前版本: {current_version}, 目标版本: {target_version}")
        
        response = {
            'type': 'reply_update',
            'from': 'ota_server',
            'to': device_id,
            'timestamp': get_timestamp(),
            'respond': 0
        }
        log_json_message('RESPONSE', '/api/ota/status', response)
        return jsonify(response)
        
    except Exception as e:
        logger.error(f"报告状态失败: {e}", exc_info=True)
        response = {
            'type': 'error',
            'from': 'ota_server',
            'to': request.json.get('from', 'unknown') if request.json else 'unknown',
            'timestamp': get_timestamp(),
            'error': {
                'code': 1000,
                'message': str(e)
            }
        }
        log_json_message('RESPONSE', '/api/ota/status', response)
        return jsonify(response), 500


@app.errorhandler(RequestEntityTooLarge)
def handle_file_too_large(e):
    """处理文件过大错误"""
    return jsonify({'success': False, 'error': '文件太大，最大支持 16MB'}), 413


@app.errorhandler(404)
def not_found(e):
    """处理 404 错误"""
    return jsonify({'success': False, 'error': '接口不存在'}), 404


@app.errorhandler(500)
def internal_error(e):
    """处理 500 错误"""
    return jsonify({'success': False, 'error': '服务器内部错误'}), 500


if __name__ == '__main__':
    logger.info("=" * 60)
    logger.info("ESP32 OTA 升级服务器")
    logger.info("=" * 60)
    logger.info(f"固件目录: {os.path.abspath(app.config['UPLOAD_FOLDER'])}")
    logger.info(f"访问地址: http://localhost:5000")
    logger.info("=" * 60)
    
    app.run(host='0.0.0.0', port=5000, debug=True)

