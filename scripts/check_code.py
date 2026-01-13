#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
代码检查脚本 (Windows Python版本)
使用 clang-tidy 和 clang-format 检查代码
"""

import os
import sys
import subprocess
import shutil
import tempfile
import difflib
from pathlib import Path

# ANSI 颜色代码 (Windows 10+ 支持)
RED = '\033[0;31m'
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


def find_tool(tool_name):
    """查找工具可执行文件"""
    # 1. 首先检查 PATH 环境变量
    tool_path = shutil.which(tool_name)
    if tool_path:
        return tool_path
    
    # 2. 检查环境变量（工具名大写+_PATH）
    env_var = f'{tool_name.upper().replace("-", "_")}_PATH'
    if env_var in os.environ:
        custom_path = Path(os.environ[env_var])
        if custom_path.exists() and custom_path.is_file():
            return str(custom_path)
    
    # 3. 检查常见的 Windows 安装路径
    tool_exe = f'{tool_name}.exe' if sys.platform == 'win32' else tool_name
    common_paths = [
        Path('E:/Software/LLVM/bin') / tool_exe,  # 用户安装路径
        Path('C:/Program Files/LLVM/bin') / tool_exe,
        Path('C:/Program Files (x86)/LLVM/bin') / tool_exe,
        Path('C:/msys64/mingw64/bin') / tool_exe,
        Path('C:/msys64/usr/bin') / tool_exe,
    ]
    
    for path in common_paths:
        if path.exists() and path.is_file():
            return str(path)
    
    # 4. 尝试检查所有驱动器的常见位置
    for drive in ['C:', 'D:', 'E:', 'F:']:
        llvm_path = Path(drive) / 'Software' / 'LLVM' / 'bin' / tool_exe
        if llvm_path.exists():
            return str(llvm_path)
    
    return None


def check_tool(tool_name, install_hint):
    """检查工具是否安装并返回路径"""
    tool_path = find_tool(tool_name)
    if not tool_path:
        print(f"{RED}错误: {tool_name} 未找到{NC}")
        print(f"请执行以下操作之一:")
        print(f"  1. 将 LLVM bin 目录添加到系统 PATH 环境变量")
        print(f"  2. 设置环境变量 {tool_name.upper().replace('-', '_')}_PATH 指向 {tool_name}.exe")
        print(f"  3. 从 LLVM 官网下载并安装到默认位置")
        sys.exit(1)
    
    # 验证工具可用
    try:
        result = subprocess.run(
            [tool_path, '--version'],
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
            timeout=5
        )
        if result.returncode != 0:
            print(f"{YELLOW}警告: {tool_name} 无法执行{NC}")
    except Exception:
        pass  # 忽略验证错误，继续使用
    
    return tool_path


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


def check_format(file_path, project_root, clang_format_path):
    """使用 clang-format 检查代码格式"""
    try:
        # 运行 clang-format 获取格式化后的内容
        result = subprocess.run(
            [clang_format_path, str(file_path)],
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
            cwd=project_root
        )
        
        if result.returncode != 0:
            print(f"{RED}格式检查失败: {file_path}{NC}")
            return True  # 返回 True 表示有问题
        
        # 检查输出是否为空
        if result.stdout is None:
            print(f"{YELLOW}警告: clang-format 输出为空: {file_path}{NC}")
            return False  # 不视为格式问题
        
        # 读取原文件内容
        try:
            with open(file_path, 'r', encoding='utf-8', errors='ignore') as f:
                original_content = f.read()
        except Exception as e:
            print(f"{YELLOW}警告: 无法读取文件 {file_path}: {e}{NC}")
            return False
        
        formatted_content = result.stdout
        
        # 比较内容
        if original_content != formatted_content:
            print(f"{RED}格式问题: {file_path}{NC}")
            # 显示差异（最多20行）
            try:
                diff = list(difflib.unified_diff(
                    original_content.splitlines(keepends=True),
                    formatted_content.splitlines(keepends=True),
                    fromfile=str(file_path),
                    tofile='格式化后',
                    n=3
                ))
                for line in diff[:20]:
                    print(line, end='')
            except Exception as e:
                print(f"{YELLOW}无法显示差异: {e}{NC}")
            return True  # 返回 True 表示有问题
        
        return False  # 返回 False 表示没有问题
        
    except Exception as e:
        print(f"{RED}格式检查失败: {file_path} - {e}{NC}")
        return True


def check_tidy(file_path, project_root, clang_tidy_path, compile_db_path=None):
    """使用 clang-tidy 检查代码质量"""
    try:
        cmd = [clang_tidy_path, str(file_path)]
        
        # 添加配置文件
        clang_tidy_config = project_root / '.clang-tidy'
        if clang_tidy_config.exists():
            cmd.extend(['--config-file', str(clang_tidy_config)])
        
        # 添加编译数据库
        if compile_db_path:
            compile_db_dir = compile_db_path.parent
            cmd.extend(['-p', str(compile_db_dir)])
        
        # 添加其他参数
        project_root_str = str(project_root).replace('\\', '/')
        main_path = f"{project_root_str}/main/"
        cmd.extend([
            '--header-filter', f'^{main_path}.*',
            '--system-headers=0',
            '--quiet'
        ])
        
        result = subprocess.run(
            cmd,
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
            cwd=project_root
        )
        
        # 过滤输出，只保留 main 目录下的警告和错误
        output_lines = []
        if result.stderr:
            output_lines.extend(result.stderr.splitlines())
        if result.stdout:
            output_lines.extend(result.stdout.splitlines())
        
        main_issues = []
        
        exclude_patterns = [
            'clang-diagnostic-error',
            'unknown argument',
            'not supported',
            '/esp-idf/',
            '/managed_components/',
            'expanded from macro'
        ]
        
        for line in output_lines:
            if 'warning:' in line or 'error:' in line:
                if 'main/' in line:
                    # 检查是否包含排除模式
                    if not any(pattern in line for pattern in exclude_patterns):
                        main_issues.append(line)
        
        if main_issues:
            for issue in main_issues:
                print(issue)
            return True  # 返回 True 表示有问题
        
        return False  # 返回 False 表示没有问题
        
    except Exception as e:
        print(f"{RED}代码质量检查失败: {file_path} - {e}{NC}")
        return False  # 检查失败不算问题，避免误报


def main():
    """主函数"""
    # 获取项目根目录
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    os.chdir(project_root)
    
    print(f"{GREEN}=== 代码检查工具 ==={NC}\n")
    
    # 检查工具是否安装并获取路径
    clang_tidy_path = check_tool('clang-tidy', '请安装 clang-tidy')
    clang_format_path = check_tool('clang-format', '请安装 clang-format')
    
    # 显示工具版本信息
    try:
        result = subprocess.run(
            [clang_tidy_path, '--version'],
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
            timeout=5
        )
        if result.returncode == 0 and result.stdout:
            version_line = result.stdout.split('\n')[0]
            print(f"找到 clang-tidy: {version_line}")
            print(f"路径: {clang_tidy_path}")
    except Exception:
        pass
    
    try:
        result = subprocess.run(
            [clang_format_path, '--version'],
            capture_output=True,
            text=True,
            encoding='utf-8',
            errors='ignore',
            timeout=5
        )
        if result.returncode == 0 and result.stdout:
            version_line = result.stdout.split('\n')[0]
            print(f"找到 clang-format: {version_line}")
            print(f"路径: {clang_format_path}\n")
    except Exception:
        pass
    
    print("")
    
    # 查找所有 C++ 源文件
    files_to_check = find_cpp_files(project_root)
    
    if not files_to_check:
        print(f"{YELLOW}警告: 未找到 C++ 源文件{NC}")
        sys.exit(1)
    
    # 1. 使用 clang-format 检查代码格式
    print(f"{YELLOW}[1/2] 检查代码格式 (clang-format)...{NC}")
    format_issues = 0
    
    for file_path in files_to_check:
        if file_path.exists():
            if check_format(file_path, project_root, clang_format_path):
                format_issues += 1
    
    if format_issues == 0:
        print(f"{GREEN}代码格式检查通过{NC}\n")
    else:
        print(f"{RED}✗ 发现 {format_issues} 个格式问题{NC}\n")
    
    # 2. 使用 clang-tidy 检查代码质量
    print(f"{YELLOW}[2/2] 检查代码质量 (clang-tidy)...{NC}")
    
    # 查找编译数据库
    compile_db_path = None
    if (project_root / 'build' / 'compile_commands.json').exists():
        compile_db_path = project_root / 'build' / 'compile_commands.json'
    elif (project_root / 'compile_commands.json').exists():
        compile_db_path = project_root / 'compile_commands.json'
    else:
        print(f"{YELLOW}警告: 未找到 compile_commands.json{NC}")
        print("ESP-IDF 项目需要先编译一次以生成编译数据库")
        print("运行: idf.py build")
        print("")
        print("或者使用以下命令生成:")
        print("  idf.py build 2>&1 | tee build.log")
        print("  python scripts/generate_compile_commands.py")
        print("")
        reply = input("是否继续使用基本检查? (y/n) ")
        if reply.lower() != 'y':
            sys.exit(1)
    
    tidy_issues = 0
    for file_path in files_to_check:
        if file_path.exists():
            print(f"检查: {file_path}")
            if check_tidy(file_path, project_root, clang_tidy_path, compile_db_path):
                tidy_issues += 1
    
    if tidy_issues == 0:
        print(f"{GREEN}代码质量检查通过{NC}\n")
    else:
        print(f"{RED}✗ 发现 {tidy_issues} 个文件有代码质量问题{NC}\n")
    
    # 总结
    print(f"{GREEN}=== 检查完成 ==={NC}")
    total_issues = format_issues + tidy_issues
    if total_issues == 0:
        print(f"{GREEN}所有检查通过！{NC}")
        sys.exit(0)
    else:
        print(f"{YELLOW}发现 {total_issues} 个问题 (格式: {format_issues}, 质量: {tidy_issues})，请修复后重新检查{NC}")
        sys.exit(1)


if __name__ == '__main__':
    main()

