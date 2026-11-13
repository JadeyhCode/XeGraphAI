#!/bin/sh
# tests/arcfuzz.sh — **ARC 求解器不许崩、不许卡死、不许没结论**（§6.789）
#
# `arc.h` 里到处是定长数组（30×30、8 训练对、4 测试）和"按面积/颜色/连通块"的直觉逻辑；
# 真实任务没覆盖 1×1、全同色、30×30、非方形、坏文件这些边界。这套拿随机任务撞：
#   ① 随机任务（10 种形状 × 8 种变换意图，固定种子）逐个 `::arc solve`：
#      退出码 0、输出里必须有「解出」或「解不出」；
#   ② 病态输入：空文件 / 截断 JSON / 只有 test 没有 train / 网格超过 30 / 9 个训练对 ——
#      同样只要求"不崩、有话说"（解出 / 解不出 / 读不了 都算）。
#
# 用法：sh tests/arcfuzz.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/arcfuzz"; rm -rf "$W"; mkdir -p "$W"
N=${ARCFUZZ_N:-24}
python3 "$ROOT/tools/arc_fuzz.py" --n "$N" --out "$W/tasks" --seed 20261107 > "$W/gen.log" 2>&1 || {
    echo "FAIL: 生成随机任务失败"; cat "$W/gen.log"; exit 1; }

pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }

run_one() {   # $1=任务文件 $2=说明
    printf '::arc solve %s\n::quit\n' "$1" > "$W/in.txt"
    if command -v timeout >/dev/null 2>&1; then
        timeout 30 ./build_aip --model "$MODEL" < "$W/in.txt" > "$W/out.txt" 2>&1
    else
        ./build_aip --model "$MODEL" < "$W/in.txt" > "$W/out.txt" 2>&1
    fi
    rc=$?
    if [ "$rc" -ne 0 ]; then
        bad "$2：退出码 $rc（崩了/超时）" "$(tail -2 "$W/out.txt" | tr '\n' ' ')"
        return
    fi
    # "有话说"= 解出 / 解不出 / 读不了（坏文件）/ 用法 —— 四种都算给了结论
    if grep -qaE '解出|解不出|读不了|\[用法\]' "$W/out.txt"; then
        ok "$2（rc=0，有结论）"
    else
        bad "$2：没有结论行" "$(tail -2 "$W/out.txt" | tr '\n' ' ')"
    fi
}

echo "== ① 随机任务（$N 个，固定种子）=="
i=0
for f in "$W"/tasks/f*.json; do
    i=$((i + 1))
    run_one "$f" "随机任务 $i"
done

echo "== ② 病态输入（只要求不崩、有话说）=="
: > "$W/empty.json"
printf '{"train": [' > "$W/trunc.json"
printf '{"test": [{"input": [[1]], "output": [[1]]}]}' > "$W/notrain.json"
python3 - "$W/big.json" <<'PY'
import json, sys
g = [[1] * 31 for _ in range(31)]          # 超过 30×30：必须被拒，不许越界
json.dump({"train": [{"input": g, "output": g}], "test": [{"input": g, "output": g}]},
          open(sys.argv[1], "w"))
PY
python3 - "$W/many.json" <<'PY'
import json, sys
tr = [{"input": [[1, 2], [3, 4]], "output": [[2, 1], [4, 3]]} for _ in range(9)]   # 9 > 8 个训练对
json.dump({"train": tr, "test": [{"input": [[1, 2], [3, 4]], "output": [[2, 1], [4, 3]]}]},
          open(sys.argv[1], "w"))
PY
for f in empty trunc notrain big many; do
    run_one "$W/$f.json" "病态输入 $f.json"
done

printf '\nARC 随机/边界：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
