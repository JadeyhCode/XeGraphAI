#!/bin/sh
# tools/build_frames_model.sh — 生成**带帧的模型**（不动冻结的出厂 model.bin）（§6.771）
#
# 为什么要单独一份：`model.bin` 是**标定产物**（`tests/factorymodel.sh` 钉住节点/边数）。
# 实测往它里面加事实（哪怕 2 条）都会改变对话行为、让行为套件变红。
# 所以帧要进模型，就生成**另一份** `model_frames.bin`：
#
#   sh tools/build_frames_model.sh                 # 生成 model_frames.bin
#   AIP_FRAMES_FROM_GRAPH=1 ./build_aip --model model_frames.bin   # 从图装载帧
#
# 生成完会自动跑 `::framesync` 校验（图里 22 帧 / 67 问法 / 132 答法 必须与内存一致）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
OUT=${1:-model_frames.bin}
[ -s model.bin ] || { echo "FAIL: 缺 model.bin"; exit 1; }
[ -x build_aip ] || { echo "FAIL: 先编译（gcc -std=c11 -O2 -o build_aip main.c -lm）"; exit 1; }
TRIP=/tmp/_frames_model.triples
sh tools/frames2triples.sh > "$TRIP" 2>/dev/null || { echo "FAIL: 生成帧三元组失败"; exit 1; }
N=$(wc -l < "$TRIP")
cp model.bin "$OUT.work"
printf '::ingest %s\n::save %s\n::quit\n' "$TRIP" "$OUT" | ./build_aip --model "$OUT.work" 2>&1 | grep -a "导入" | head -1
rm -f "$OUT.work"
[ -s "$OUT" ] || { echo "FAIL: 没生成 $OUT"; exit 1; }
echo "== 校验（::framesync）=="
printf '::framesync\n::quit\n' | ./build_aip --model "$OUT" 2>&1 | grep -aE "帧镜像|图里|结论"
echo
echo "生成：$OUT（帧三元组 $N 条）"
echo "用法：AIP_FRAMES_FROM_GRAPH=1 ./build_aip --model $OUT"
echo "⚠ 出厂 model.bin 未改动（它是标定产物，见 tests/factorymodel.sh）"
