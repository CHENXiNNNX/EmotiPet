#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32 固件打包脚本
从 build 目录打包 bin 文件，生成发布包
"""

import os
import sys
import json
import shutil
import hashlib
import argparse
from datetime import datetime
from pathlib import Path
from typing import Dict, List, Optional


class BinPackager:
    """Bin文件打包器"""
    
    def __init__(self, build_dir: str, output_dir: str = "release", version: Optional[str] = None):
        """
        初始化打包器
        
        Args:
            build_dir: build目录路径
            output_dir: 输出目录路径
            version: 版本号（如果为None，则从时间戳生成）
        """
        self.build_dir = Path(build_dir)
        self.output_dir = Path(output_dir)
        self.version = version or self._generate_version()
        self.package_name = f"EmotiPet_v{self.version}"
        self.package_dir = self.output_dir / self.package_name
        
        # 从 flasher_args.json 读取文件信息
        self.flasher_args = self._load_flasher_args()
        
    def _load_flasher_args(self) -> Dict:
        """加载 flasher_args.json"""
        flasher_args_path = self.build_dir / "flasher_args.json"
        if not flasher_args_path.exists():
            print(f"警告: 未找到 {flasher_args_path}")
            return {}
        
        try:
            with open(flasher_args_path, 'r', encoding='utf-8') as f:
                return json.load(f)
        except Exception as e:
            print(f"错误: 无法读取 flasher_args.json: {e}")
            return {}
    
    def _generate_version(self) -> str:
        """生成版本号（基于时间戳）"""
        now = datetime.now()
        return now.strftime("%Y%m%d_%H%M%S")
    
    def calculate_md5(self, filepath: Path) -> str:
        """计算文件的MD5值"""
        hash_md5 = hashlib.md5()
        try:
            with open(filepath, "rb") as f:
                for chunk in iter(lambda: f.read(4096), b""):
                    hash_md5.update(chunk)
            return hash_md5.hexdigest()
        except Exception as e:
            print(f"错误: 无法计算 {filepath} 的MD5: {e}")
            return ""
    
    def get_file_info(self, filepath: Path) -> Dict:
        """获取文件信息"""
        if not filepath.exists():
            return {}
        
        stat = filepath.stat()
        return {
            'name': filepath.name,
            'path': str(filepath.relative_to(self.build_dir)),
            'size': stat.st_size,
            'md5': self.calculate_md5(filepath),
            'modified': datetime.fromtimestamp(stat.st_mtime).strftime('%Y-%m-%d %H:%M:%S')
        }
    
    def collect_bin_files(self) -> List[Dict]:
        """收集所有需要打包的bin文件"""
        bin_files = []
        
        # 从 flasher_args.json 获取文件列表
        flash_files = self.flasher_args.get('flash_files', {})
        
        # 需要打包的文件列表
        files_to_package = {
            'bootloader': 'bootloader/bootloader.bin',
            'app': 'EmotiPet.bin',
            'partition-table': 'partition_table/partition-table.bin',
            'otadata': 'ota_data_initial.bin'
        }
        
        for file_type, file_path in files_to_package.items():
            file_full_path = self.build_dir / file_path
            
            if not file_full_path.exists():
                print(f"警告: 文件不存在: {file_full_path}")
                continue
            
            file_info = self.get_file_info(file_full_path)
            if file_info:
                # 添加烧录地址信息
                offset = None
                for addr, path in flash_files.items():
                    if path == file_path:
                        offset = addr
                        break
                
                file_info['type'] = file_type
                file_info['offset'] = offset
                bin_files.append(file_info)
                print(f"找到文件: {file_path} ({file_info['size']} 字节)")
        
        return bin_files
    
    def create_package(self, compress: bool = False) -> bool:
        """创建打包文件"""
        print(f"\n{'='*60}")
        print(f"开始打包固件")
        print(f"{'='*60}")
        print(f"版本号: {self.version}")
        print(f"Build目录: {self.build_dir}")
        print(f"输出目录: {self.package_dir}")
        
        # 创建输出目录
        self.package_dir.mkdir(parents=True, exist_ok=True)
        
        # 收集bin文件
        bin_files = self.collect_bin_files()
        if not bin_files:
            print("错误: 未找到任何bin文件")
            return False
        
        # 复制文件
        print(f"\n复制文件到打包目录...")
        for file_info in bin_files:
            src_path = self.build_dir / file_info['path']
            dst_path = self.package_dir / file_info['name']
            
            try:
                shutil.copy2(src_path, dst_path)
                print(f"  ✓ {file_info['name']}")
            except Exception as e:
                print(f"  ✗ 复制失败 {file_info['name']}: {e}")
                return False
        
        # 生成清单文件
        manifest = self._generate_manifest(bin_files)
        manifest_path = self.package_dir / "manifest.json"
        try:
            with open(manifest_path, 'w', encoding='utf-8') as f:
                json.dump(manifest, f, indent=2, ensure_ascii=False)
            print(f"\n✓ 生成清单文件: manifest.json")
        except Exception as e:
            print(f"✗ 生成清单文件失败: {e}")
            return False
        
        # 复制 flasher_args.json（如果存在）
        flasher_args_path = self.build_dir / "flasher_args.json"
        if flasher_args_path.exists():
            try:
                shutil.copy2(flasher_args_path, self.package_dir / "flasher_args.json")
                print(f"✓ 复制 flasher_args.json")
            except Exception as e:
                print(f"✗ 复制 flasher_args.json 失败: {e}")
        
        # 生成README
        readme_path = self.package_dir / "README.txt"
        self._generate_readme(readme_path, manifest)
        print(f"✓ 生成 README.txt")
        
        # 压缩（如果需要）
        if compress:
            archive_path = self._create_archive()
            if archive_path:
                print(f"\n✓ 创建压缩包: {archive_path}")
                # 可选：删除原始目录
                # shutil.rmtree(self.package_dir)
        
        print(f"\n{'='*60}")
        print(f"打包完成!")
        print(f"输出目录: {self.package_dir}")
        if compress:
            print(f"压缩包: {archive_path}")
        print(f"{'='*60}\n")
        
        return True
    
    def _generate_manifest(self, bin_files: List[Dict]) -> Dict:
        """生成清单文件"""
        manifest = {
            'package': {
                'name': self.package_name,
                'version': self.version,
                'created': datetime.now().strftime('%Y-%m-%d %H:%M:%S'),
                'target': 'ESP32-S3',
                'description': 'EmotiPet 固件发布包'
            },
            'files': bin_files,
            'flash_info': {
                'flash_mode': self.flasher_args.get('flash_settings', {}).get('flash_mode', 'dio'),
                'flash_size': self.flasher_args.get('flash_settings', {}).get('flash_size', '16MB'),
                'flash_freq': self.flasher_args.get('flash_settings', {}).get('flash_freq', '80m')
            }
        }
        return manifest
    
    def _generate_readme(self, readme_path: Path, manifest: Dict):
        """生成README文件"""
        readme_content = f"""EmotiPet 固件发布包
版本: {manifest['package']['version']}
创建时间: {manifest['package']['created']}
目标芯片: {manifest['package']['target']}

文件列表:
"""
        for file_info in manifest['files']:
            readme_content += f"""
- {file_info['name']}
  类型: {file_info['type']}
  大小: {file_info['size']} 字节
  MD5: {file_info['md5']}
  烧录地址: {file_info.get('offset', 'N/A')}
  修改时间: {file_info['modified']}
"""
        
        readme_content += f"""
烧录配置:
  模式: {manifest['flash_info']['flash_mode']}
  大小: {manifest['flash_info']['flash_size']}
  频率: {manifest['flash_info']['flash_freq']}

使用方法:
1. 使用 esptool.py 烧录固件
2. 参考 flasher_args.json 中的地址信息
3. 或使用 ESP-IDF 的 flash 命令

示例命令:
  esptool.py --chip esp32s3 --port /dev/ttyUSB0 \\
    --baud 921600 write_flash \\
    0x0 bootloader.bin \\
    0x8000 partition-table.bin \\
    0xd000 ota_data_initial.bin \\
    0x20000 EmotiPet.bin
"""
        
        try:
            with open(readme_path, 'w', encoding='utf-8') as f:
                f.write(readme_content)
        except Exception as e:
            print(f"警告: 无法生成README文件: {e}")
    
    def _create_archive(self) -> Optional[Path]:
        """创建压缩包"""
        try:
            archive_format = 'zip'
            archive_name = f"{self.package_name}.{archive_format}"
            archive_path = self.output_dir / archive_name
            
            # 如果压缩包已存在，先删除
            if archive_path.exists():
                archive_path.unlink()
            
            shutil.make_archive(
                str(self.output_dir / self.package_name),
                archive_format,
                self.output_dir,
                self.package_name
            )
            
            return archive_path
        except Exception as e:
            print(f"错误: 创建压缩包失败: {e}")
            return None


def main():
    """主函数"""
    parser = argparse.ArgumentParser(
        description='ESP32 固件打包工具',
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
示例:
  # 使用默认版本号（时间戳）
  python package_bin.py
  
  # 指定版本号
  python package_bin.py --version 1.0.0
  
  # 指定build目录和输出目录
  python package_bin.py --build-dir ../build --output-dir ./release
  
  # 打包并压缩
  python package_bin.py --compress
        """
    )
    
    parser.add_argument(
        '--build-dir',
        type=str,
        default='build',
        help='build目录路径（默认: build）'
    )
    
    parser.add_argument(
        '--output-dir',
        type=str,
        default='release',
        help='输出目录路径（默认: release）'
    )
    
    parser.add_argument(
        '--version',
        type=str,
        default=None,
        help='版本号（默认: 自动生成时间戳版本）'
    )
    
    parser.add_argument(
        '--compress',
        action='store_true',
        help='创建压缩包'
    )
    
    args = parser.parse_args()
    
    # 检查build目录是否存在
    build_dir = Path(args.build_dir)
    if not build_dir.exists():
        print(f"错误: build目录不存在: {build_dir}")
        print(f"提示: 请先编译项目 (idf.py build)")
        return 1
    
    # 创建打包器并执行打包
    packager = BinPackager(
        build_dir=str(build_dir.absolute()),
        output_dir=args.output_dir,
        version=args.version
    )
    
    if packager.create_package(compress=args.compress):
        return 0
    else:
        return 1


if __name__ == '__main__':
    sys.exit(main())

