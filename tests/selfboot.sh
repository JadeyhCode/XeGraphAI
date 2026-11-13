#!/bin/sh
# tests/selfboot.sh — **差分自举**这门机制的看护（§6.819）。
#
# 它跑 tools/selfboot.py：用 Python oracle 当判据，让系统自己发现缺口、起草算子/排除词、
# 再跑 5 组随机输入复核。判据（任一不满足即 FAIL）：
#   ① 自举前有缺口（before < 12）——否则说明“自举”测的是空；
#   ② 自举后 ≥ 10/12 —— 否则这套机制没学会它该学会的；
#   ③ 全程只改**模型副本**（_t/selfboot/m.bin），仓库里的 code/code_model.bin 不动。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"

[ -f build_aip ] || { echo "FAIL: 缺 build_aip"; exit 1; }
# 沙箱里 code_model.bin 不一定在（它不进仓库）⇒ 现场重建（和 codebench 一样）
mkdir -p _t/selfboot
if [ ! -f code/code_model.bin ]; then
    sh code/build_code_model.sh > _t/selfboot/build_model.log 2>&1
fi
[ -f code/code_model.bin ] || { echo "FAIL: 缺 code/code_model.bin（重建也失败，见 _t/selfboot/build_model.log）"; exit 1; }
BEFORE_HASH=$(sha256sum code/code_model.bin | cut -d" " -f1)

OUT=$ROOT/_t/selfboot/run.log
mkdir -p "$ROOT/_t/selfboot"
python3 tools/selfboot.py > "$OUT" 2>&1
RC=$?

B=$(sed -n "s/.*自举前 \([0-9]*\)\/\([0-9]*\).*/\1/p" "$OUT" | tail -1)
A=$(sed -n "s/.*自举后 \([0-9]*\)\/\([0-9]*\).*/\1/p" "$OUT" | tail -1)
B=${B:-0}; A=${A:-0}
T=$(sed -n "s/.*自举后 [0-9]*\/\([0-9]*\).*/\1/p" "$OUT" | tail -1)
T=${T:-12}
printf "  自举前 %s/%s ⇒ 自举后 %s/%s（rc=%s）\n" "$B" "$T" "$A" "$T" "$RC"

fails=0
[ "$RC" -eq 0 ] || { printf "  [FAIL] selfboot.py 非零退出（rc=%s）\n" "$RC"; fails=1; }
[ "$B" -lt "$T" ] || { printf "  [FAIL] 自举前就已经 12/12 —— 这轮没测到任何缺口\n"; fails=1; }
[ "$A" -ge $((T * 85 / 100)) ] || { printf "  [FAIL] 自举后只有 %s/%s（要求 ≥85%%）\n" "$A" "$T"; fails=1; }
AFTER_HASH=$(sha256sum code/code_model.bin | cut -d" " -f1)
[ "$BEFORE_HASH" = "$AFTER_HASH" ] || { printf "  [FAIL] 仓库模型被改了（selfboot 只该改副本）\n"; fails=1; }

[ "$fails" -eq 0 ] && printf "PASS selfboot（自举 %s→%s/%s，仓库模型未改）\n" "$B" "$A" "$T" || printf "FAIL selfboot\n"
[ "$fails" -eq 0 ]
