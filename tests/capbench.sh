#!/bin/sh
# tests/capbench.sh — **能力门**：一次跑完隐藏组 + 全部盲测组，并与基线对比（§6.817）。
#
# 为什么要有：2026-11-13 那次“一次补 14 条算子”把 blind12/13/14 打崩（泛关键词抢戏），
# 但当时只盯着新盲测看，隐藏组先烂了。以后**加知识前先跑这一条**：
#   · 任何一组的“通过数”低于基线 ⇒ FAIL（回归）；
#   · 任何一组“答错数”高于基线 ⇒ FAIL（更糟：硬答）；
#   · 拒绝数变多不算失败（拒绝是诚实），但会在表里显示。
#
# 用法：sh tests/capbench.sh            # 跑全部并与基线比
#       sh tests/capbench.sh --write    # 用当前结果重写基线（只在确认改进后手动跑）
set -u
# ⚠ 这条**必须独占** `_t/work`（和 codebench 等套件并发会互相删 synth 产物，实测出过假回归）。
# 整包（tests/all.sh）是并行的 ⇒ 默认跳过；手动跑或 AIP_SERIAL=1 时才真跑。
if [ "${AIP_SERIAL:-0}" != "1" ] && [ "${AIP_CAPBENCH:-0}" != "1" ]; then
    printf "SKIP capbench（要独占 _t/work：手动 sh tests/capbench.sh，或 AIP_SERIAL=1 sh tests/all.sh）\n"
    exit 0
fi
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BASE="$ROOT/tests/capbench.baseline"
WRITE=0
[ "${1:-}" = "--write" ] && WRITE=1   # ⚠ 必须**先**存下来：下面 `set --` 会把位置参数冲掉（实测踩过）

run_py() {  # $1=脚本；输出 "pass refuse wrong"
    python3 "$1" 2>/dev/null | sed -n "s/.*=> pass \([0-9]*\) \/ refuse \([0-9]*\) \/ wrong \([0-9]*\).*/\1 \2 \3/p" | tail -1
}

OUT=$(mktemp)
fail=0
printf "%-12s %6s %6s %6s\n" "组" "通过" "拒绝" "答错" | tee "$OUT"

# 隐藏组（41 组 × 8）：把每组的通过数加起来
H=$(python3 tools/code_hidden.py 2>/dev/null | grep -o "=> [0-9]* / 8" | sed "s/=> \([0-9]*\) \/ 8/\1/")
HP=$(printf "%s\n" "$H" | awk "{s+=\$1} END{print s+0}")
HT=$(printf "%s\n" "$H" | wc -l | tr -d " ")
HT=$((HT * 8))
printf "%-12s %6s %6s %6s\n" "hidden" "$HP" "0" "$((HT - HP))" | tee -a "$OUT"

for g in 11 12 13 14 15 16 17 18 19 20 21 22 23; do
    R=$(run_py "tools/code_blind$g.py")
    set -- $R
    printf "%-12s %6s %6s %6s\n" "blind$g" "${1:-0}" "${2:-0}" "${3:-0}" | tee -a "$OUT"
done

if [ "$WRITE" -eq 1 ]; then
    cp "$OUT" "$BASE"
    printf "\n已写基线：%s\n" "$BASE"
    rm -f "$OUT"
    exit 0
fi

[ -f "$BASE" ] || { printf "\n没有基线（先跑 serge --write 一次，确认数字是当前真实水平）\n" | sed "s/serge/sh tests\/capbench.sh/"; rm -f "$OUT"; exit 2; }

printf "\n== 与基线比 ==\n"
while read -r name p r w; do
    [ "$name" = "组" ] && continue
    bp=$(awk -v n="$name" '$1==n{print $2}' "$BASE")
    bwn=$(awk -v n="$name" '$1==n{print $4}' "$BASE")
    [ -z "${bp:-}" ] && continue
    if [ "$p" -lt "$bp" ]; then printf "  [FAIL] %s 通过 %s < 基线 %s\n" "$name" "$p" "$bp"; fail=1; fi
    if [ "$w" -gt "$bwn" ]; then printf "  [FAIL] %s 答错 %s > 基线 %s\n" "$name" "$w" "$bwn"; fail=1; fi
done < "$OUT"
rm -f "$OUT"
[ "$fail" -eq 0 ] && printf "PASS capbench（没有组低于基线）\n" || printf "FAIL capbench（有组回归）\n"
[ "$fail" -eq 0 ]
