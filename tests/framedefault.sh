#!/bin/sh
# tests/framedefault.sh — **帧的默认来源改成图**（§6.782）
#
# 以前 `AIP_FRAMES_FROM_GRAPH=1` 才从图装载（保守）。现在反过来：**图里有 `帧库` 就用图**，
# 没有才退回 frames.txt；要退回文件可以 `AIP_FRAMES_FROM_GRAPH=0`。
#
# 判据：
#   ① 帧模型（model_frames.bin，图里有帧库）：默认路径与 AIP_FRAMES_FROM_GRAPH=0（文件路径）**逐条答案相同**；
#   ② `::framesync` 报内存与图里一致（22 帧 / 67 问法 / 132 答法 / 22 有填充）；
#   ③ 出厂模型（没有帧库）：仍从 frames.txt 装载 —— 不能因为改了默认就把帧弄没了。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
FM=${1:-$ROOT/model_frames.bin}
FMODEL=${2:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/framedefault"; rm -rf "$W"; mkdir -p "$W"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
has() { printf '%s' "$2" | grep -q -- "$1"; }

Q='你好\n今天天气怎么样\n猫是什么\n你会什么\n吃了吗\n再见\n睡吧\n::quit\n'

echo "== ① 帧模型：默认（图）与 AIP_FRAMES_FROM_GRAPH=0（文件）逐条相同 =="
if [ ! -s "$FM" ]; then
    # 沙箱里没有生成物：现造（不动出厂 model.bin）
    sh "$ROOT/tools/build_frames_model.sh" "$W/model_frames.bin" > "$W/build.log" 2>&1 || true
    FM="$W/model_frames.bin"
fi
if [ -s "$FM" ]; then
    printf '%b' "$Q" | ./build_aip --model "$FM" > "$W/graph.txt" 2>&1
    printf '%b' "$Q" | AIP_FRAMES_FROM_GRAPH=0 ./build_aip --model "$FM" > "$W/file.txt" 2>&1
    sed -n 's/.*AI > //p' "$W/graph.txt" > "$W/g.txt" || true
    sed -n 's/.*AI > //p' "$W/file.txt" > "$W/f.txt" || true
    if [ -s "$W/g.txt" ] && diff -q "$W/g.txt" "$W/f.txt" > /dev/null; then
        ok "两条来源的答复逐条相同（$(wc -l < "$W/g.txt") 条）"
    else
        bad "图/文件两条来源答复不同" "$(diff "$W/g.txt" "$W/f.txt" | head -4 | tr '\n' ' ')"
    fi
    o=$(printf '::framesync\n::quit\n' | ./build_aip --model "$FM" 2>&1)
    has '22 帧 / 问法 67 / 答法 132 / 有填充的帧 22' "$o" && ok "framesync 报内存与图一致（22/67/132/22）" \
        || bad "framesync 不一致" "$(printf '%s' "$o" | grep 帧 | head -3 | tr '\n' ' ')"
else
    bad "缺 model_frames.bin（先 sh tools/build_frames_model.sh）" "$FM"
fi

echo "== ② 出厂模型没有帧库：仍从 frames.txt 装载 =="
printf '你好\n::quit\n' | ./build_aip --model "$FMODEL" > "$W/factory.txt" 2>&1
has 'AI > ' "$(cat "$W/factory.txt")" && ok "出厂模型照常答（帧没被改坏）" || bad "出厂模型不答了" "$(tail -2 "$W/factory.txt" | tr '\n' ' ')"

printf '\n帧默认来源：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
