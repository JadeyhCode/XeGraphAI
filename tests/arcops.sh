#!/bin/sh
# tests/arcops.sh — **每个 ARC 原语都要有一道只有它能解的题**（§6.790）
#
# `tests/arc.sh` 看护"机制在图上"，`arcfuzz` 看护"不崩"，这一套看护**原语本身**：
# 12 个原语各造一道小题（题由 tools/arc_opsgen.py 现造，不依赖数据集），要求
#   ① `::arc solve` 报 `解出：<期望原语>`（说明没有别的原语抢先/没有退化）
#   ② 预测网格与任务里的期望输出**逐格相同**
#
# 为什么值得：原语是最容易"悄悄改坏"的地方（方向写反、变成 no-op、被误停用），
# 而 `::arc solve` 只要还能解出*别的*路径就不会红 —— 这道门专盯"这个原语还对不对"。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/arcops"; rm -rf "$W"; mkdir -p "$W"
python3 "$ROOT/tools/arc_opsgen.py" "$W" > "$W/gen.log" 2>&1 || { echo "FAIL: 生成原语题失败"; cat "$W/gen.log"; exit 1; }

pass=0; fail=0
ok()  { printf '  [OK]   %-10s %s\n' "$1" "$2"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %-10s %s\n         证据: %s\n' "$1" "$2" "${3:-}"; fail=$((fail + 1)); }

while IFS=$(printf '\t') read -r name path want; do
    [ -n "${name:-}" ] || continue
    printf '::arc solve %s\n::quit\n' "$path" > "$W/in.txt"
    ./build_aip --model "$MODEL" < "$W/in.txt" > "$W/out.txt" 2>&1
    rc=$?
    got=$(sed -n 's/.*预测1: \(\[[0-9/]*\]\).*/\1/p' "$W/out.txt" | head -1)
    exp=$(python3 - "$path" <<'PY'
import json, sys
t = json.load(open(sys.argv[1]))
g = t["test"][0]["output"]
print("[" + "/".join("".join(str(v) for v in r) for r in g) + "]")
PY
)
    how=$(sed -n 's/.*解出：\([^（]*\)（.*/\1/p' "$W/out.txt" | head -1)
    if [ "$rc" -ne 0 ]; then
        bad "$name" "退出码 $rc" "$(tail -2 "$W/out.txt" | tr '\n' ' ')"
    elif ! printf '%s' "$how" | grep -qE -- "$want"; then
        bad "$name" "解出的原语不是 $want（实际 ${how:-无}）" "$(grep -a '解出\|解不出' "$W/out.txt" | head -1)"
    elif [ "$got" != "$exp" ]; then
        bad "$name" "预测不是期望网格" "got=$got exp=$exp"
    else
        ok "$name" "解出：$want，预测逐格正确"
    fi
done < "$W/index.tsv"

printf '\nARC 原语：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
