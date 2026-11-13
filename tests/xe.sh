#!/bin/sh
# tests/xe.sh — XeGraphAI 自测：空符号层 / 切碎 / 会话记忆 / 图补丁 / 插件
set -e
cd "$(dirname "$0")/.."
gcc -std=c11 -O2 -I. -o build_xe_selftest tools/xe_selftest.c -lm
./build_xe_selftest
gcc -std=c11 -O2 -I. -o build_xe_patchtest tools/xe_patchtest.c -lm
./build_xe_patchtest
gcc -std=c11 -O2 -I. -o build_xe_sesstest tools/xe_sesstest.c -lm
./build_xe_sesstest
gcc -std=c11 -O2 -I. -o build_xe_plugintest tools/xe_plugintest.c -lm
./build_xe_plugintest
