#!/bin/bash
# build.sh - 编译脚本

set -e  # 遇到错误停止

echo "🔨 开始编译 C 代码..."

# 创建输出目录
mkdir -p ../lib

# 编译动态库 (供 Python 调用)
echo "   编译动态库 libsatellite.dylib ..."
clang -shared -fPIC -o ../lib/libsatellite.dylib satellite_tracker.c

# 编译独立测试程序
echo "   编译测试程序 test_c ..."
clang -o ../test_c satellite_tracker.c

# 检查编译结果
if [ -f "../lib/libsatellite.dylib" ]; then
    echo "✅ 动态库编译成功: ../lib/libsatellite.dylib"
else
    echo "❌ 动态库编译失败"
    exit 1
fi

if [ -f "../test_c" ]; then
    echo "✅ 测试程序编译成功: ../test_c"
else
    echo "❌ 测试程序编译失败"
    exit 1
fi

echo "🎉 编译完成！"