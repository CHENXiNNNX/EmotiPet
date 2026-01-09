#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
代码格式化脚本 (Windows Python版本)
使用 clang-format 自动格式化代码
"""

import os
import sys
import subprocess
import shutil
from pathlib import Path

# ANSI 颜色代码 (Windows 10+ 支持)
GREEN = '\033[0;32m'
YELLOW = '\033[1;33m'
NC = '\033[0m'  # No Color

# 在 Windows 上启用 ANSI 转义码支持
if sys.platform == 'win32':
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        kernel32.SetConsoleMode(kernel32.GetStdHandle(-11), 7)
    except Exception:
        pass  # 如果失败，继续执行（可能不支持颜色）


def find_clang_format():
    """查找 clang-format 可执行文件"""
    # 1. 首先检查 PATH 环境变量
    clang_format = shutil.which('clang-format')
    if clang_format:
        return clang_format
    
    # 2. 检查环境变量
    if 'CLANG_FORMAT_PATH' in os.environ:
        custom_path = Path(os.environ['CLANG_FORMAT_PATH'])
        if custom_path.exists() and custom_path.is_file():
            return str(custom_path)
    
    # 3. 检查常见的 Windows 安装路径
    common_paths = [
        Path('E:/Software/LLVM/bin/clang-format.exe'),  # 用户安装路径
        Path('C:/Program Files/LLVM/bin/clang-format.exe'),
        Path('C:/Program Files (x86)/LLVM/bin/clang-format.exe'),
        Path('C:/msys64/mingw64/bin/clang-format.exe'),
        Path('C:/msys64/usr/bin/clang-format.exe'),
    ]
    
    for path in common_paths:
        if path.exists() and path.is_file():
            return str(path)
    
    # 4. 尝试检查所有驱动器
    for drive in ['C:', 'D:', 'E:', 'F:']:
        llvm_path = Path(drive) / 'Software' / 'LLVM' / 'bin' / 'clang-format.exe'
        if llvm_path.exists():
            return str(llvm_path)
    
    return None


def find_cpp_files(project_root):
    """查找所有 C++ 源文件"""
    cpp_extensions = {'.cc', '.cpp', '.hpp', '.h'}
    files = []
    
    main_dir = project_root / 'main'
    if not main_dir.exists():
        return files
    
    for ext in cpp_extensions:
        for file_path in main_dir.rglob(f'*{ext}'):
            # 排除 build 和 managed_components 目录
            parts = file_path.parts
            if 'build' not in parts and 'managed_components' not in parts:
                files.append(file_path)
    
    return sorted(files)


def main():
    """主函数"""
    # 获取项目根目录
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    os.chdir(project_root)
    
    print(f"{GREEN}=== 代码格式化工具 ==={NC}\n")
    
    # 查找 clang-format
    clang_format_path = find_clang_format()
    if not clang_format_path:
        print(f"{YELLOW}错误: clang-format 未找到{NC}")
        print("请执行以下操作之一:")
        print("  1. 将 LLVM bin 目录添加到系统 PATH 环境变量")
        print("  2. 设置环境变量 CLANG_FORMAT_PATH 指向 clang-format.exe")
        print("  3. 从 LLVM 官网下载并安装到默认位置")
        print(f"\n常见路径: C:\\Program Files\\LLVM\\bin\\clang-format.exe")
        sys.exit(1)
    
    # 验证 clang-format 可用
    try:
        result = subprocess.run(
            [clang_format_path, '--version'],
            capture_output=True,
            text=True,
            timeout=5
        )
        if result.returncode == 0:
            version_line = result.stdout.split('\n')[0] if result.stdout else 'unknown'
            print(f"找到 clang-format: {version_line}")
            print(f"路径: {clang_format_path}\n")
        else:
            print(f"{YELLOW}警告: clang-format 无法执行{NC}\n")
    except Exception as e:
        print(f"{YELLOW}警告: 无法验证 clang-format - {e}{NC}\n")
    
    # 查找所有 C++ 源文件
    files = find_cpp_files(project_root)
    file_count = len(files)
    
    if file_count == 0:
        print(f"{YELLOW}警告: 未找到 C++ 源文件{NC}")
        sys.exit(1)
    
    print(f"找到 {file_count} 个文件")
    print("")
    
    # 询问是否继续
    reply = input("是否格式化所有文件? (y/n) ")
    if reply.lower() != 'y':
        print("已取消")
        sys.exit(0)
    
    # 格式化文件
    formatted = 0
    for file_path in files:
        if file_path.exists():
            print(f"格式化: {file_path}")
            try:
                result = subprocess.run(
                    [clang_format_path, '-i', str(file_path)],
                    cwd=project_root,
                    check=True
                )
                formatted += 1
            except subprocess.CalledProcessError as e:
                print(f"{YELLOW}警告: 格式化失败 {file_path} - {e}{NC}")
            except Exception as e:
                print(f"{YELLOW}警告: 格式化失败 {file_path} - {e}{NC}")
    
    print("")
    print(f"{GREEN}已格式化 {formatted} 个文件{NC}")


if __name__ == '__main__':
    main()

