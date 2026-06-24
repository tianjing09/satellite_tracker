# test_python_batch.py
# python3 test_python.py

#!/usr/bin/env python
import ctypes
import json
import os
from pathlib import Path

# ========== 加载动态库 ==========
lib_path = "./lib/libsatellite.dylib"

if not os.path.exists(lib_path):
    print(f"❌ 动态库不存在: {lib_path}")
    print("请先编译: cd c_src && ./build.sh")
    exit(1)

lib = ctypes.CDLL(lib_path)

# ========== 定义回调函数类型 ==========
# void callback(int step, char* message, int progress, int error_code, 
#               char* error_msg, char* data, int current, int total)
CALLBACK_TYPE = ctypes.CFUNCTYPE(
    None,
    ctypes.c_int,        # step
    ctypes.c_char_p,     # message
    ctypes.c_int,        # progress
    ctypes.c_int,        # error_code
    ctypes.c_char_p,     # error_msg
    ctypes.c_char_p,     # data
    ctypes.c_int,        # current
    ctypes.c_int         # total
)

# ========== 进度回调 ==========
def progress_callback(step, msg, progress, error_code, error_msg, data, current, total):
    msg_str = msg.decode('utf-8') if msg else ""
    error_msg_str = error_msg.decode('utf-8') if error_msg else ""
    data_str = data.decode('utf-8') if data else ""
    
    if error_code != 0:
        print(f"❌ 错误 (Step {step}): {error_msg_str}")
    else:
        if total > 1:
            print(f"📊 [{current+1}/{total}] {msg_str} ({progress}%)")
        else:
            print(f"📊 Step {step}: {msg_str} ({progress}%)")
        
        # 如果 data 包含路径，打印出来
        if data_str and step == 5:
            print(f"   📁 输出: {data_str}")

# ========== 设置 process_images 函数 ==========
lib.process_images.argtypes = [
    ctypes.POINTER(ctypes.c_char_p),  # 字符串数组
    ctypes.c_int,                      # 数量
    ctypes.c_char_p,                   # 输出目录
    CALLBACK_TYPE                      # 回调
]
lib.process_images.restype = ctypes.c_char_p

# ========== 准备测试图片 ==========
def get_test_images():
    """获取测试图片列表"""
    # 方式1: 使用 test/ 目录下的图片
    test_dir = Path("./test")
    if test_dir.exists():
        extensions = ('.jpg', '.jpeg', '.png', '.tiff', '.tif', '.fits', '.fit')
        images = [str(p) for p in test_dir.glob("*") if p.suffix.lower() in extensions]
        # images = [str(p) for p in test_dir.glob("*.jpg")]
        if images:
            return images
    
    # 方式2: 如果没有测试图片，创建模拟图片
    print("⚠️ 没有找到测试图片，创建模拟图片...")
    test_dir.mkdir(exist_ok=True)
    
    # 创建模拟图片文件（空文件）
    for i in range(1, 4):
        img_path = test_dir / f"test_{i}.jpg"
        if not img_path.exists():
            img_path.touch()
        images.append(str(img_path))
    
    return images

# ========== 主测试 ==========
def main():
    print("=" * 60)
    print("🚀 C 算法测试 - process_images")
    print("=" * 60)
    
    # 1. 准备测试图片
    image_paths = get_test_images()
    print(f"📁 找到 {len(image_paths)} 张图片:")
    for p in image_paths:
        print(f"   - {p}")
    
    # 2. 准备输出目录
    output_dir = "./test_output"
    os.makedirs(output_dir, exist_ok=True)
    print(f"📤 输出目录: {output_dir}")
    
    # 3. 准备参数
    # Python 字符串列表 → C 字符串数组
    c_strings = [path.encode('utf-8') for path in image_paths]
    c_array = (ctypes.c_char_p * len(c_strings))(*c_strings)
    
    # 4. 创建回调
    callback = CALLBACK_TYPE(progress_callback)
    
    # 5. 调用 C 函数
    print("\n⏳ 开始处理...\n")
    
    try:
        result_ptr = lib.process_images(
            c_array,
            len(c_strings),
            output_dir.encode('utf-8'),
            callback
        )
        
        # 6. 解析结果
        result_str = result_ptr.decode('utf-8')
        result = json.loads(result_str)
        
        # 7. 打印结果
        print("\n" + "=" * 60)
        print("📋 最终结果")
        print("=" * 60)
        print(json.dumps(result, indent=2, ensure_ascii=False))
        
        # 8. 显示生成的文件
        print("\n" + "=" * 60)
        print("📁 生成的文件")
        print("=" * 60)
        output_dir_path = Path(output_dir)
        if output_dir_path.exists():
            files = list(output_dir_path.glob("*.jpg"))
            if files:
                for f in files:
                    print(f"   ✅ {f.name}")
            else:
                print("   ⚠️ 没有生成任何文件（模拟模式）")
        else:
            print("   ❌ 输出目录不存在")
        
        print("\n✅ 测试完成！")
        
    except Exception as e:
        print(f"❌ 调用失败: {e}")
        import traceback
        traceback.print_exc()

if __name__ == "__main__":
    main()