#!/usr/bin/env python3
"""
构建 assets.bin，支持多种 ESP-SR 模型打包

支持的模型类型：
- WakeNet: 唤醒词检测模型 (wn9_*, wn8_* 等)
- VADNet: 语音活动检测模型 (vadnet*)
- NSNet: 噪声抑制模型 (nsnet*)
- MultiNet: 语音命令识别模型 (mn*_cn, mn*_en 等)
"""

import argparse
import os
import shutil
import sys
import json
import struct
from datetime import datetime


def ensure_dir(directory):
    """确保目录存在，如果不存在则创建"""
    os.makedirs(directory, exist_ok=True)


def copy_file(src, dst):
    """复制文件"""
    if os.path.exists(src):
        shutil.copy2(src, dst)
        print(f"已复制: {src} -> {dst}")
        return True
    else:
        print(f"警告: 源文件不存在: {src}")
        return False


def copy_directory(src, dst):
    """复制目录"""
    if os.path.exists(src):
        shutil.copytree(src, dst, dirs_exist_ok=True)
        print(f"已复制目录: {src} -> {dst}")
        return True
    else:
        print(f"警告: 源目录不存在: {src}")
        return False


def struct_pack_string(string, max_len=None):
    """
    将字符串打包为二进制数据
    如果 max_len 为 None，则 max_len = len(string)
    否则如果 len(string) < max_len，剩余部分用 \x00 填充
    """
    if max_len is None:
        max_len = len(string)
    else:
        assert len(string) <= max_len

    left_num = max_len - len(string)
    out_bytes = None
    for char in string:
        if out_bytes is None:
            out_bytes = struct.pack('b', ord(char))
        else:
            out_bytes += struct.pack('b', ord(char))
    for i in range(left_num):
        out_bytes += struct.pack('x')  # 填充空字节
    return out_bytes


def read_data(filename):
    """读取二进制数据"""
    with open(filename, "rb") as f:
        return f.read()


def pack_models(model_path, out_file="srmodels.bin"):
    """
    将所有模型打包成一个二进制文件
    
    格式:
    {
        model_num: int (4 bytes)
        model1_info: model_info_t
        model2_info: model_info_t
        ...
        model_data (所有模型文件的二进制数据)
    }
    
    model_info_t:
    {
        model_name: char[32]
        file_number: int (4 bytes)
        file1_name: char[32]
        file1_start: int (4 bytes)
        file1_len: int (4 bytes)
        file2_name: char[32]
        file2_start: int (4 bytes)
        file2_len: int (4 bytes)
        ...
    }
    """
    models = {}
    file_num = 0
    model_num = 0
    
    # 遍历模型目录（只处理直接子目录）
    if not os.path.exists(model_path):
        print(f"错误: 模型路径不存在: {model_path}")
        return False
    
    for model_name in os.listdir(model_path):
        model_dir = os.path.join(model_path, model_name)
        if os.path.isdir(model_dir):
            models[model_name] = {}
            # 遍历模型目录中的所有文件（包括子目录）
            for root, dirs, files in os.walk(model_dir):
                for file_name in files:
                    file_num += 1
                    file_path = os.path.join(root, file_name)
                    # 保持相对路径结构
                    rel_path = os.path.relpath(file_path, model_dir)
                    models[model_name][rel_path] = read_data(file_path)
    
    if not models:
        print("警告: 没有找到任何模型")
        return False
    
    model_num = len(models)
    # 计算头部长度: model_num(4) + 每个模型的元数据
    header_len = 4 + model_num * (32 + 4) + file_num * (32 + 4 + 4)
    
    out_bin = struct.pack('I', model_num)  # 模型数量
    data_bin = None
    
    # 构建模型信息表和数据
    for key in models:
        model_bin = struct_pack_string(key, 32)  # 模型名称
        model_bin += struct.pack('I', len(models[key]))  # 文件数量
        
        for file_name in models[key]:
            model_bin += struct_pack_string(file_name, 32)  # 文件名
            if data_bin is None:
                model_bin += struct.pack('I', header_len)  # 第一个文件的偏移
                data_bin = models[key][file_name]
                model_bin += struct.pack('I', len(models[key][file_name]))  # 文件长度
            else:
                model_bin += struct.pack('I', header_len + len(data_bin))  # 后续文件的偏移
                data_bin += models[key][file_name]
                model_bin += struct.pack('I', len(models[key][file_name]))  # 文件长度
        
        out_bin += model_bin
    
    assert len(out_bin) == header_len, f"头部长度不匹配: {len(out_bin)} != {header_len}"
    
    if data_bin is not None:
        out_bin += data_bin
    
    out_file_path = os.path.join(model_path, out_file)
    with open(out_file_path, "wb") as f:
        f.write(out_bin)
    
    print(f"已生成: {out_file_path} (大小: {len(out_bin) / 1024:.2f} KB)")
    return True


def detect_model_type(model_name):
    """
    自动检测模型类型
    返回: ('wakenet'|'vadnet'|'nsnet'|'multinet'|'unknown', model_name)
    """
    model_name_lower = model_name.lower()
    
    if model_name_lower.startswith('wn') or 'wakenet' in model_name_lower:
        return ('wakenet', model_name)
    elif model_name_lower.startswith('vadnet') or 'vadn' in model_name_lower:
        return ('vadnet', model_name)
    elif model_name_lower.startswith('nsnet') or 'nsn' in model_name_lower:
        return ('nsnet', model_name)
    elif model_name_lower.startswith('mn') or 'multinet' in model_name_lower:
        return ('multinet', model_name)
    else:
        return ('unknown', model_name)


def get_model_path(model_name, model_type, esp_sr_model_path):
    """
    根据模型类型和名称获取完整路径
    返回模型路径，如果不存在则返回 None
    """
    if model_type == 'wakenet':
        model_path = os.path.join(esp_sr_model_path, 'wakenet_model', model_name)
    elif model_type == 'vadnet':
        model_path = os.path.join(esp_sr_model_path, 'vadnet_model', model_name)
    elif model_type == 'nsnet':
        model_path = os.path.join(esp_sr_model_path, 'nsnet_model', model_name)
    elif model_type == 'multinet':
        model_path = os.path.join(esp_sr_model_path, 'multinet_model', model_name)
    else:
        # 尝试作为完整路径
        if os.path.exists(model_name):
            return model_name
        return None
    
    if os.path.exists(model_path):
        return model_path
    return None


def get_model_paths(model_names, esp_sr_model_path):
    """
    获取所有模型的完整路径
    支持自动识别模型类型
    返回: [(model_type, model_name, model_path), ...]
    """
    if not model_names:
        return []
    
    valid_models = []
    for model_name in model_names:
        # 如果是完整路径，直接使用
        if os.path.isabs(model_name) and os.path.exists(model_name):
            model_type = 'custom'
            model_path = model_name
            print(f"找到自定义模型: {model_path}")
            valid_models.append((model_type, os.path.basename(model_name), model_path))
            continue
        
        # 自动检测模型类型
        model_type, clean_name = detect_model_type(model_name)
        
        # 获取模型路径
        model_path = get_model_path(clean_name, model_type, esp_sr_model_path)
        
        if model_path:
            print(f"找到 {model_type} 模型: {clean_name}")
            valid_models.append((model_type, clean_name, model_path))
        else:
            print(f"警告: 模型不存在: {model_name} (类型: {model_type})")
    
    return valid_models


def get_languages_from_models(model_list):
    """
    从模型列表中检测语言（仅从 multinet 模型）
    返回语言列表，支持多语言（如 ['cn', 'en']）
    """
    if not model_list:
        return []
    
    languages = set()
    cn_indicators = ['_cn', 'cn_']
    en_indicators = ['_en', 'en_']
    
    for model_type, model_name, _ in model_list:
        if model_type == 'multinet':
            if any(indicator in model_name for indicator in cn_indicators):
                languages.add('cn')
            if any(indicator in model_name for indicator in en_indicators):
                languages.add('en')
    
    return sorted(list(languages)) if languages else []


def process_sr_models(model_list, build_dir, assets_dir, esp_sr_model_path):
    """
    处理所有 SR 模型并生成 srmodels.bin
    支持 WakeNet、VADNet、NSNet、MultiNet 等多种模型类型
    
    参数:
        model_list: [(model_type, model_name, model_path), ...] 模型列表
        build_dir: 构建目录
        assets_dir: assets 目录
        esp_sr_model_path: ESP-SR 模型根目录
    
    返回生成的 srmodels.bin 文件名，如果失败则返回 None
    """
    if not model_list:
        return None
    
    # 创建 SR 模型构建目录
    sr_models_build_dir = os.path.join(build_dir, "srmodels")
    if os.path.exists(sr_models_build_dir):
        shutil.rmtree(sr_models_build_dir)
    os.makedirs(sr_models_build_dir)
    
    models_processed = 0
    needs_fst = False
    multinet_models = []
    
    # 复制所有模型
    for model_type, model_name, model_path in model_list:
        model_dst = os.path.join(sr_models_build_dir, model_name)
        if copy_directory(model_path, model_dst):
            models_processed += 1
            print(f"已添加 {model_type} 模型: {model_name}")
            
            # 记录 multinet 模型，检查是否需要 fst
            if model_type == 'multinet':
                multinet_models.append(model_name)
                if 'mn6' in model_name or 'mn7' in model_name:
                    needs_fst = True
    
    # 如果使用 Multinet6/7，添加 fst 模型
    if needs_fst:
        fst_model_path = os.path.join(esp_sr_model_path, 'multinet_model', 'fst')
        if os.path.exists(fst_model_path):
            fst_dst = os.path.join(sr_models_build_dir, 'fst')
            if copy_directory(fst_model_path, fst_dst):
                models_processed += 1
                print(f"已添加 fst 模型 (Multinet6/7 必需)")
            else:
                print("警告: 无法复制 fst 模型，Multinet6/7 可能无法正常工作")
        else:
            print(f"警告: fst 模型目录不存在: {fst_model_path}")
    
    if models_processed == 0:
        print("警告: 没有成功处理任何 SR 模型")
        return None
    
    # 使用 pack_models 函数生成 srmodels.bin
    srmodels_output = os.path.join(sr_models_build_dir, "srmodels.bin")
    try:
        if pack_models(sr_models_build_dir, "srmodels.bin"):
            # 复制 srmodels.bin 到 assets 目录
            copy_file(srmodels_output, os.path.join(assets_dir, "srmodels.bin"))
            print(f"成功打包 {models_processed} 个模型到 srmodels.bin")
            return "srmodels.bin"
        else:
            return None
    except Exception as e:
        print(f"错误: 生成 srmodels.bin 失败: {e}")
        import traceback
        traceback.print_exc()
        return None


def generate_index_json(assets_dir, srmodels, multinet_model_info):
    """
    生成 index.json 文件
    支持多语言配置
    """
    index_data = {
        "version": 1
    }
    
    if srmodels:
        index_data["srmodels"] = srmodels
    
    if multinet_model_info:
        index_data["multinet_model"] = multinet_model_info
    
    # 写入 index.json
    index_path = os.path.join(assets_dir, "index.json")
    with open(index_path, 'w', encoding='utf-8') as f:
        json.dump(index_data, f, indent=4, ensure_ascii=False)
    
    print(f"已生成: {index_path}")
    return True


def compute_checksum(data):
    """计算校验和"""
    return sum(data) & 0xFFFF


def sort_key(filename):
    """文件排序键"""
    basename, extension = os.path.splitext(filename)
    return extension, basename


def pack_assets_simple(target_path, include_path, out_file, assets_path, max_name_len=32):
    """
    简化版的 assets 打包函数
    将 assets 目录中的所有文件打包成 assets.bin
    """
    merged_data = bytearray()
    file_info_list = []
    skip_files = ['config.json']
    
    # 确保输出目录存在
    os.makedirs(os.path.dirname(out_file), exist_ok=True)
    os.makedirs(include_path, exist_ok=True)
    
    # 获取文件列表并排序
    file_list = sorted([f for f in os.listdir(target_path) 
                       if os.path.isfile(os.path.join(target_path, f)) and f not in skip_files], 
                      key=sort_key)
    
    for filename in file_list:
        file_path = os.path.join(target_path, filename)
        file_name = os.path.basename(file_path)
        file_size = os.path.getsize(file_path)
        
        file_info_list.append((file_name, len(merged_data), file_size, 0, 0))
        # 添加 0x5A5A 前缀
        merged_data.extend(b'\x5A\x5A')
        
        with open(file_path, 'rb') as bin_file:
            bin_data = bin_file.read()
        merged_data.extend(bin_data)
    
    total_files = len(file_info_list)
    
    # 构建文件索引表
    mmap_table = bytearray()
    for file_name, offset, file_size, width, height in file_info_list:
        if len(file_name) > max_name_len:
            print(f'警告: "{file_name}" 超过 {max_name_len} 字节，将被截断')
        fixed_name = file_name.ljust(max_name_len, '\0')[:max_name_len]
        mmap_table.extend(fixed_name.encode('utf-8'))
        mmap_table.extend(file_size.to_bytes(4, byteorder='little'))
        mmap_table.extend(offset.to_bytes(4, byteorder='little'))
        mmap_table.extend(width.to_bytes(2, byteorder='little'))
        mmap_table.extend(height.to_bytes(2, byteorder='little'))
    
    # 合并数据
    combined_data = mmap_table + merged_data
    combined_checksum = compute_checksum(combined_data)
    combined_data_length = len(combined_data).to_bytes(4, byteorder='little')
    
    # 构建头部: total_files(4) + checksum(4) + data_length(4)
    header_data = (total_files.to_bytes(4, byteorder='little') + 
                   combined_checksum.to_bytes(4, byteorder='little') + 
                   combined_data_length)
    
    final_data = header_data + combined_data
    
    # 写入输出文件
    with open(out_file, 'wb') as output_bin:
        output_bin.write(final_data)
    
    print(f"已生成 assets.bin: {out_file} (大小: {len(final_data) / 1024:.2f} KB)")
    return True


def build_assets(model_names, esp_sr_model_path, output_path, 
                 cn_wake_word=None, en_wake_word=None, threshold=0.2):
    """
    构建 assets.bin，支持多种模型类型
    
    参数:
        model_names: 模型名称列表，支持自动识别类型
                    例如: ['wn9_nihaoxiaozhi', 'vadnet1_medium', 'nsnet1', 'mn6_cn']
                    或完整路径: ['/path/to/model']
        esp_sr_model_path: ESP-SR 模型目录路径
        output_path: 输出 assets.bin 的路径
        cn_wake_word: 中文唤醒词（可选，仅用于 multinet）
        en_wake_word: 英文唤醒词（可选，仅用于 multinet）
        threshold: 检测阈值（0.0-1.0，仅用于 multinet）
    """
    # 创建临时构建目录
    temp_build_dir = os.path.join(os.path.dirname(output_path), "temp_build")
    assets_dir = os.path.join(temp_build_dir, "assets")
    
    try:
        # 清理并创建目录
        if os.path.exists(temp_build_dir):
            shutil.rmtree(temp_build_dir)
        ensure_dir(temp_build_dir)
        ensure_dir(assets_dir)
        
        print("=" * 60)
        print("开始构建 assets...")
        print("=" * 60)
        
        # 获取所有模型路径（自动识别类型）
        model_list = get_model_paths(model_names, esp_sr_model_path)
        if not model_list:
            print("错误: 没有找到任何有效的模型")
            return False
        
        # 显示找到的模型
        print(f"\n找到 {len(model_list)} 个模型:")
        for model_type, model_name, _ in model_list:
            print(f"  - {model_type}: {model_name}")
        
        # 处理 SR 模型，生成 srmodels.bin
        srmodels = process_sr_models(model_list, temp_build_dir, assets_dir, esp_sr_model_path)
        if not srmodels:
            print("错误: 生成 srmodels.bin 失败")
            return False
        
        # 检测语言（仅从 multinet 模型）
        multinet_models = [name for model_type, name, _ in model_list if model_type == 'multinet']
        languages = get_languages_from_models(model_list)
        
        # 构建 multinet_model 配置（仅当有 multinet 模型时）
        multinet_model_info = None
        if multinet_models:
            print(f"\n检测到 Multinet 模型，语言: {', '.join(languages) if languages else '未知'}")
            multinet_model_info = {
                "languages": languages if languages else ['cn'],
                "duration": 3000,  # 默认持续时间（毫秒）
                "threshold": threshold,
                "commands": {}
            }
            
            # 添加中文命令词（仅在明确指定时添加）
            if 'cn' in languages and cn_wake_word and cn_wake_word.strip():
                multinet_model_info["commands"]["cn"] = [
                    {
                        "command": cn_wake_word,
                        "text": cn_wake_word,
                        "action": "wake"
                    }
                ]
                print(f"中文唤醒词: {cn_wake_word}")
            elif 'cn' in languages:
                print("提示: 未配置中文唤醒词，可在运行时动态添加")
            
            # 添加英文命令词（仅在明确指定时添加）
            if 'en' in languages and en_wake_word and en_wake_word.strip():
                multinet_model_info["commands"]["en"] = [
                    {
                        "command": en_wake_word,
                        "text": en_wake_word,
                        "action": "wake"
                    }
                ]
                print(f"英文唤醒词: {en_wake_word}")
            elif 'en' in languages:
                print("提示: 未配置英文唤醒词，可在运行时动态添加")
            
            # 如果没有指定唤醒词，给出提示
            if not multinet_model_info["commands"]:
                print("提示: 未指定唤醒词，可在运行时通过 esp_mn_commands_add() 动态添加")
        
        # 生成 index.json
        generate_index_json(assets_dir, srmodels, multinet_model_info)
        
        # 打包成 assets.bin
        include_path = os.path.join(temp_build_dir, "include")
        image_file = os.path.join(temp_build_dir, "output", "assets.bin")
        if not pack_assets_simple(assets_dir, include_path, image_file, "assets", 32):
            return False
        
        # 复制最终文件到输出位置
        if os.path.exists(image_file):
            shutil.copy2(image_file, output_path)
            print(f"成功生成 assets.bin: {output_path}")
            
            # 显示大小信息
            total_size = os.path.getsize(output_path)
            print(f"Assets 文件大小: {total_size / 1024:.2f} KB ({total_size} 字节)")
            
            return True
        else:
            print(f"错误: 生成的 assets.bin 不存在: {image_file}")
            return False
            
    except Exception as e:
        print(f"错误: 构建 assets 失败: {e}")
        import traceback
        traceback.print_exc()
        return False
    finally:
        # 清理临时目录
        if os.path.exists(temp_build_dir):
            shutil.rmtree(temp_build_dir)


def main():
    parser = argparse.ArgumentParser(
        description='构建 assets.bin，支持多种 ESP-SR 模型打包',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
使用示例:
  # 打包 VADNet 和 NSNet 模型（用于 AFE）
  python build_assets.py --esp_sr_model_path ./managed_components/espressif__esp-sr/model \\
                         --models vadnet1_medium nsnet1 \\
                         --output assets.bin

  # 打包 Multinet 模型（用于语音命令识别）
  python build_assets.py --esp_sr_model_path ./managed_components/espressif__esp-sr/model \\
                         --models mn6_cn mn6_en \\
                         --cn_wake_word "你好小智" \\
                         --en_wake_word "hello" \\
                         --output assets.bin

  # 打包多种模型（VAD + NS + Multinet）
  python build_assets.py --esp_sr_model_path ./managed_components/espressif__esp-sr/model \\
                         --models vadnet1_medium nsnet1 mn6_cn \\
                         --cn_wake_word "你好小智" \\
                         --output assets.bin

  # 使用完整路径指定自定义模型
  python build_assets.py --esp_sr_model_path ./managed_components/espressif__esp-sr/model \\
                         --models /path/to/custom/model \\
                         --output assets.bin

支持的模型类型（自动识别）:
  - WakeNet: wn9_*, wn8_* (唤醒词检测)
  - VADNet: vadnet* (语音活动检测)
  - NSNet: nsnet* (噪声抑制)
  - MultiNet: mn*_cn, mn*_en (语音命令识别)
        """
    )
    parser.add_argument('--esp_sr_model_path', required=True, 
                       help='ESP-SR 模型目录路径（例如: ./managed_components/espressif__esp-sr/model）')
    parser.add_argument('--output', required=True, 
                       help='输出 assets.bin 的路径')
    parser.add_argument('--models', required=True, nargs='+',
                       help='模型名称列表，支持自动识别类型。例如: vadnet1_medium nsnet1 mn6_cn')
    parser.add_argument('--cn_wake_word', default=None,
                       help='中文唤醒词（可选，仅用于 multinet 模型）')
    parser.add_argument('--en_wake_word', default=None,
                       help='英文唤醒词（可选，仅用于 multinet 模型）')
    parser.add_argument('--threshold', type=float, default=0.2,
                       help='Multinet 检测阈值 (0.0-1.0，默认 0.2)')
    
    args = parser.parse_args()
    
    print("=" * 60)
    print("构建 assets.bin")
    print("=" * 60)
    print(f"ESP-SR 模型路径: {args.esp_sr_model_path}")
    print(f"模型列表: {', '.join(args.models)}")
    print(f"输出路径: {args.output}")
    if args.cn_wake_word or args.en_wake_word:
        print(f"唤醒词: CN={args.cn_wake_word}, EN={args.en_wake_word}")
    if any('mn' in m.lower() or 'multinet' in m.lower() for m in args.models):
        print(f"Multinet 阈值: {args.threshold}")
    print("=" * 60)
    print()
    
    success = build_assets(
        model_names=args.models,
        esp_sr_model_path=args.esp_sr_model_path,
        output_path=args.output,
        cn_wake_word=args.cn_wake_word,
        en_wake_word=args.en_wake_word,
        threshold=args.threshold
    )
    
    if not success:
        sys.exit(1)
    
    print()
    print("=" * 60)
    print("构建完成！")
    print("=" * 60)


if __name__ == "__main__":
    main()

