#!/bin/sh
# 在仓库根目录使用本次 Release 核心库生成补测程序。
set -eu
/usr/bin/c++ -std=c++20 -O3 -DNDEBUG -Isrc -Isrc/Algorithms -Isrc/Hashing -Isrc/Storage -Isrc/Runtime \
    result/instrumentation-20261124/raw/supplement/measure.cpp build/release/libfinesse_core.a \
    /opt/homebrew/lib/libcrypto.dylib build/release/third_party/xdelta-build/libxdelta3.a -o "$1"
