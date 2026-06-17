# test_python_batch.py
import ctypes
import json

lib = ctypes.CDLL("./lib/libsatellite.dylib")

CALLBACK_TYPE = ctypes.CFUNCTYPE(
    None, ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
    ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_int, ctypes.c_int
)

def progress_callback(step, msg, progress, error_code, error_msg, data, current, total):
    if error_code != 0:
        print(f"❌ 错误: {error_msg}")
    else:
        print(f"📊 [{current+1}/{total}] {msg.decode()} ({progress}%)")

# 设置批量处理函数
lib.extract_trajectories_from_folder.argtypes = [
    ctypes.c_char_p,
    CALLBACK_TYPE
]
lib.extract_trajectories_from_folder.restype = ctypes.c_char_p

# 调用
callback = CALLBACK_TYPE(progress_callback)
result_ptr = lib.extract_trajectories_from_folder(
    "./test_images/".encode('utf-8'),
    callback
)

result = json.loads(result_ptr.decode('utf-8'))
print(json.dumps(result, indent=2, ensure_ascii=False))