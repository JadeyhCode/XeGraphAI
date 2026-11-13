#!/bin/sh
# tests/selfserve.sh — **需求驱动自服务训练**（§6.822）：需求 → 图内推 → 断点 → 取源 → 验证 → 扩图 → 复测。
#
# 判据（任一不满足即 FAIL）：
#   ① 训练前 < 4（否则测的是空）；
#   ② 训练后 = 4/4（每条需求都能被自己补会）；
#   ③ 语料里那条**错误实现**必须被验证层拒掉（grep REJECT ... divisors_bad）——
#      这是“没有 oracle 也能判语义”的关键证据；
#   ④ 仓库模型不被改动（只改 _t/serve/m.bin 副本）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p _t/serve
if [ ! -f code/code_model.bin ]; then
    sh code/build_code_model.sh > _t/serve/build_model.log 2>&1
fi
[ -f code/code_model.bin ] || { echo "FAIL: 缺 code/code_model.bin"; exit 1; }
BEFORE=$(sha256sum code/code_model.bin | cut -d" " -f1)
OUT=$ROOT/_t/serve/run.log
python3 tools/selfserve.py > "$OUT" 2>&1
RC=$?
B=$(sed -n "s/.*训练前 \([0-9]*\)\/\([0-9]*\).*/\1/p" "$OUT" | tail -1)
A=$(sed -n "s/.*训练后 \([0-9]*\)\/\([0-9]*\).*/\1/p" "$OUT" | tail -1)
T=$(sed -n "s/.*训练后 [0-9]*\/\([0-9]*\).*/\1/p" "$OUT" | tail -1)
B=${B:-0}; A=${A:-0}; T=${T:-4}
printf "  训练前 %s/%s ⇒ 训练后 %s/%s（rc=%s）\n" "$B" "$T" "$A" "$T" "$RC"

fails=0
[ "$RC" -eq 0 ] || { printf "  [FAIL] selfserve 非零退出（rc=%s）\n" "$RC"; fails=1; }
[ "$B" -lt "$T" ] || { printf "  [FAIL] 训练前就全过 —— 没测到缺口\n"; fails=1; }
[ "$A" -eq "$T" ] || { printf "  [FAIL] 训练后只有 %s/%s\n" "$A" "$T"; fails=1; }
grep -qa "REJECT.*divisors_bad" "$OUT" \
    && printf "  错误实现被验证层拒掉（divisors_bad）\n" \
    || { printf "  [FAIL] 错误实现没被拒（验证层失效）\n"; fails=1; }
AFTER=$(sha256sum code/code_model.bin | cut -d" " -f1)
[ "$BEFORE" = "$AFTER" ] || { printf "  [FAIL] 仓库模型被改了\n"; fails=1; }

[ "$fails" -eq 0 ] && printf "PASS selfserve（%s→%s/%s，错误候选被拒，仓库模型未改）\n" "$B" "$A" "$T" || printf "FAIL selfserve\n"
[ "$fails" -eq 0 ]
