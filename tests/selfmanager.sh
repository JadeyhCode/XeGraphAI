#!/bin/sh
# tests/selfmanager.sh — **自管理编排器**（§6.828）：串行锁 / 步骤 / 门禁 / 日报。
#
# 判据：
#   ① 正常一轮：跑 generalize（合并写概念 + 重建模型）→ 写 report.jsonl + 日报；
#   ② 锁：另一份编排在跑时，第二份必须**拒绝**（并发是假读数的根因）；
#   ③ 跑完锁要释放。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p _t/manager
[ -f code/code_model.bin ] || sh code/build_code_model.sh > _t/manager/build.log 2>&1
[ -f code/code_model.bin ] || { echo "FAIL: 缺 code/code_model.bin"; exit 1; }
fails=0

# ① 正常一轮（不跑门禁，保持快）
OUT=$ROOT/_t/manager/run.log
python3 tools/selfmanager.py --steps generalize > "$OUT" 2>&1
RC=$?
[ "$RC" -eq 0 ] || { printf "  [FAIL] 编排 rc=%s\n" "$RC"; fails=1; }
grep -qa "编排完成" "$OUT" || { printf "  [FAIL] 没有完成行\n"; fails=1; }
[ -f _t/manager/report.jsonl ] || { printf "  [FAIL] 没写 report.jsonl\n"; fails=1; }
ls _t/manager/daily-*.md > /dev/null 2>&1 || { printf "  [FAIL] 没写日报\n"; fails=1; }
[ -f _t/manager.lock ] && { printf "  [FAIL] 跑完锁没释放\n"; fails=1; }

# ② 锁：造一个“活着的”持有者（sleep），第二份必须拒绝
sleep 30 &
SPID=$!
printf "%s" "$SPID" > _t/manager.lock
python3 tools/selfmanager.py --steps generalize > _t/manager/run2.log 2>&1
RC2=$?
[ "$RC2" -eq 2 ] || { printf "  [FAIL] 并发时没有拒绝（rc=%s）\n" "$RC2"; fails=1; }
grep -qa "REFUSE" _t/manager/run2.log || { printf "  [FAIL] 拒绝时没说明原因\n"; fails=1; }
kill "$SPID" 2>/dev/null || true
rm -f _t/manager.lock

[ "$fails" -eq 0 ] && printf "PASS selfmanager（串行锁/拒绝并发/日报/报告）\n" || printf "FAIL selfmanager\n"
[ "$fails" -eq 0 ]
