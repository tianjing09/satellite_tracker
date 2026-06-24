import ctypes

lib = ctypes.CDLL("./lib/libsatellite.dylib")

CALLBACK_TYPE = ctypes.CFUNCTYPE(
    None, ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
    ctypes.c_int, ctypes.c_char_p, ctypes.c_char_p,
    ctypes.c_int, ctypes.c_int
)

def progress_callback(step, msg, progress, error_code, error_msg, data, current, total):
    pass

lib.process_images.argtypes = [
    ctypes.POINTER(ctypes.c_char_p),
    ctypes.c_int,
    ctypes.c_char_p,
    CALLBACK_TYPE
]
lib.process_images.restype = ctypes.c_char_p

image_paths = ["./test/test.jpg"]
c_strings = [p.encode('utf-8') for p in image_paths]
c_array = (ctypes.c_char_p * len(c_strings))(*c_strings)
output_dir = "./test_output".encode('utf-8')
callback = CALLBACK_TYPE(progress_callback)

result_ptr = lib.process_images(c_array, len(c_strings), output_dir, callback)
result_str = result_ptr.decode('utf-8')

print("=== 原始返回值 ===")
print(repr(result_str))
print("\n=== 长度 ===")
print(len(result_str))

# 显示前200个字符
print("\n=== 前200个字符 ===")
print(result_str[:200])
