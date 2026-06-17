#!/usr/bin/env python3
import ctypes
import json
import os

# 加载动态库
lib_path = "./lib/libsatellite.dylib"

if not os.path.exists(lib_path):
    print(f"❌ 错误: 找不到 {lib_path}")
    print("请先运行: cd c_src && ./build.sh")
    exit(1)

lib = ctypes.CDLL(lib_path)

# 定义回调函数类型
CALLBACK_TYPE = ctypes.CFUNCTYPE(
    None,
    ctypes.c_int,
    ctypes.c_char_p,
    ctypes.c_int,
    ctypes.c_int,
    ctypes.c_char_p,
    ctypes.c_char_p
)

def test_callback(step, msg, progress, error_code, error_msg, data):
    msg_str = msg.decode() if msg else ""
    error_msg_str = error_msg.decode() if error_msg else ""
    
    if error_code != 0:
        print(f"❌ 步骤 {step}: {msg_str} ({progress}%)")
        print(f"   错误码: {error_code}, 错误信息: {error_msg_str}")
    else:
        print(f"✅ 步骤 {step}: {msg_str} ({progress}%)")
    
    if data:
        data_str = data.decode()
        if step == 5 and error_code == 0:
            # 完整数据
            print(f"📊 结果数据: {data_str[:500]}...")
        else:
            print(f"📊 数据片段: {data_str[:200]}...")

# 定义函数
lib.extract_satellite_trajectory.argtypes = [ctypes.c_char_p, CALLBACK_TYPE]
lib.extract_satellite_trajectory.restype = ctypes.c_char_p


# 创建测试图片（如果不存在）
test_image = "test.jpg"
if not os.path.exists(test_image):
    print(f"⚠️ 测试图片 {test_image} 不存在，创建一个空的...")
    # 创建一个最小的 JPEG 文件（实际项目中请用真实图片）
    with open(test_image, 'wb') as f:
        f.write(b'\xff\xd8\xff\xe0\x00\x10JFIF\x00\x01\x01\x00\x00\x01\x00\x01\x00\x00')

# 调用 C 函数
print("🚀 调用 C 算法...")
callback = CALLBACK_TYPE(test_callback)
result_ptr = lib.extract_satellite_trajectory(test_image.encode('utf-8'), callback)

result_str = result_ptr.decode('utf-8')
print("\n📋 最终 JSON 结果:")
print(json.dumps(json.loads(result_str), indent=2, ensure_ascii=False))