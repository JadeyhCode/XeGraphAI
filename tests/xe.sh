#!/bin/sh
# tests/xe.sh — XeGraphAI 自测：空符号层 / 切碎 / 会话记忆
set -e
cd "$(dirname "$0")/.."
gcc -std=c11 -O2 -Wall -Wextra -I. -o build_xe_selftest tools/xe_selftest.c -lm
./build_xe_selftest
