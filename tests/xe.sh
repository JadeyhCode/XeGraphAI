#!/bin/sh
# tests/xe.sh — XeGraphAI 自测：空符号层 / 切碎 / 会话记忆 / 图补丁 / 插件 / 现场学习 / 元认知
set -e
cd "$(dirname "$0")/.."
for t in selftest patchtest sesstest plugintest learntest metatest; do
    gcc -std=c11 -O2 -I. -o build_xe_$t tools/xe_$t.c -lm
    ./build_xe_$t
done
