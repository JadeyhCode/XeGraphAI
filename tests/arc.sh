#!/bin/sh
# tests/arc.sh — **ARC 归纳：变换表真的在图上**（§6.779）
#
# 这一套验的不是"能解几道 ARC"（那是基准，见 docs/benchmark-arc.md），而是**机制**：
#   ① `::arc sync` 把变换表写进图（26 个变换，三类事实：属于 变换类:X / 有 序号 N / 有 启用 是）
#   ② 表**随模型存盘**：sync → ::save → 新进程加载 ⇒ 表还在（图里查得到，不是内存里的常量）
#   ③ 停用/启用改的是**图上的事实**：`::arc off symh` 之后同一道题**解不出**，`on` 回来又解得出
#      —— 如果表是 C 里的静态数组，第 ③ 条不可能成立
#   ④ 解不出就明说（不猜），坏文件不崩
#
# 用法：sh tests/arc.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }

W="$ROOT/_t/arc"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
cp "$ROOT/tests/fixtures/arc/symh.json" "$W/symh.json"
cp "$ROOT/tests/fixtures/arc/symh_up2.json" "$W/symh_up2.json"
cp "$ROOT/tests/fixtures/arc/connect_h.json" "$W/connect_h.json"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
has() { printf '%s' "$2" | grep -q -- "$1"; }

run() { printf '%b' "$1" | ./build_aip --model "$2" 2>&1; }

echo "== ① 没 sync 之前：图里没有变换表 =="
o=$(run '::arc list\n::quit\n' "$W/m.bin")
has '还没有变换表' "$o" && ok "空图 → 明说没有表（不假装有）" || bad "空图行为" "$(printf '%s' "$o" | grep ARC | head -2)"

echo "== ② sync：26 个变换进图 =="
o=$(run '::arc sync\n::arc list\n::quit\n' "$W/m.bin")
has '变换表进图：' "$o" && ok "sync 把变换表写进图" || bad "sync" "$(printf '%s' "$o" | grep ARC | head -2)"
has '变换:rot90' "$o" && ok "list 里有 rot90" || bad "list 内容" "$(printf '%s' "$o" | grep '变换:' | head -3)"
has '启用 [0-9][0-9]*' "$o" && ok "list 报出启用数" || bad "启用计数" "$(printf '%s' "$o" | grep '登记' | head -1)"

echo "== ③ 表随模型存盘（图里的事实，不是内存常量）=="
printf '::arc sync\n::save %s/m2.bin\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" > "$W/save.log" 2>&1
o=$(run '::arc list\n::quit\n' "$W/m2.bin")
has '图上登记 [0-9][0-9]* 个变换' "$o" && ok "新进程加载后表还在（图里查得到）" || bad "存读后丢表" "$(printf '%s' "$o" | grep ARC | head -2)"

echo "== ④ 解题：解出要给出正确答案 =="
o=$(run '::arc sync\n::arc solve symh.json\n::quit\n' "$W/m.bin")
# 注意：cwd 是仓库根，任务路径用相对仓库的路径
o=$(cd "$ROOT" && printf '::arc sync\n::arc solve %s/symh.json\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" 2>&1)
has '解出：symh' "$o" && ok "对称补全题解出 symh" || bad "解题" "$(printf '%s' "$o" | grep ARC | tail -2)"
has '\[606/070/808\]' "$o" && ok "预测网格逐格正确" || bad "预测内容" "$(printf '%s' "$o" | grep 预测 | head -1)"

echo "== ⑤ 停用/启用改的是图上的事实（table-in-graph 的反证）=="
# ⑤a 启用状态：单步就够 ⇒ 报的是 symh 本身
o=$(cd "$ROOT" && printf '::arc sync\n::arc solve %s/symh.json\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" 2>&1)
has '解出：symh（深度 1' "$o" && ok "全启用：一步 symh 解出" || bad "全启用" "$(printf '%s' "$o" | grep -E '解出|解不出' | head -1)"
# ⑤b 停用 symh：图上少了它 ⇒ 解出路径**不再是 symh**（深度 3 会拿等价组合顶上，这正是"策略从图里来"）
o=$(cd "$ROOT" && printf '::arc sync\n::arc off symh\n::arc solve %s/symh.json\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" 2>&1)
has '启用 [0-9][0-9]* 个变换' "$o" && ok "停用后仍按图里的启用数走" || bad "停用计数" "$(printf '%s' "$o" | grep 启用 | head -1)"
printf '%s' "$o" | grep -q '解出：symh（深度 1' && bad "停用后仍报 symh" "$(printf '%s' "$o" | grep 解出 | head -1)" || ok "停用 symh ⇒ 解出路径不再是 symh（图上没了就用等价组合）"
# ⑤c 全部停用 ⇒ 解不出（这才是不可能靠组合绕过去的反证）
NAMES=$(cd "$ROOT" && printf '::arc sync\n::arc list\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1 | grep -o '变换:[a-z0-9_]*' | sed 's/变换://' | sort -u)
N_NAMES=$(printf '%s\n' "$NAMES" | grep -c . || true)
{
    printf '::arc sync\n'
    printf '%s\n' "$NAMES" | while read -r n; do [ -n "$n" ] && printf '::arc off %s\n' "$n"; done
    printf '::arc list\n::arc solve %s/symh.json\n::quit\n' "$W"
} > "$W/all_off.in"
o=$(cd "$ROOT" && ./build_aip --model "$W/m.bin" < "$W/all_off.in" 2>&1)
printf '%s' "$o" | grep -q '启用 0' && ok "全部停用：图上启用 0（共 $N_NAMES 个）" || bad "全部停用计数" "$(printf '%s' "$o" | grep '启用' | head -1)"
has '解不出' "$o" && ok "全部停用 ⇒ 同一道题解不出" || bad "全部停用仍解出" "$(printf '%s' "$o" | grep -E '解出' | head -1)"
# ⑤d 全部启用回来 ⇒ 又解得出
{
    printf '::arc sync\n'
    printf '%s\n' "$NAMES" | while read -r n; do [ -n "$n" ] && printf '::arc on %s\n' "$n"; done
    printf '::arc solve %s/symh.json\n::quit\n' "$W"
} > "$W/all_on.in"
o=$(cd "$ROOT" && ./build_aip --model "$W/m.bin" < "$W/all_on.in" 2>&1)
has '解出：symh' "$o" && ok "全部启用回来 ⇒ 又解得出" || bad "启用回来仍解不出" "$(printf '%s' "$o" | grep -E '解出|解不出' | head -2)"

echo "== ⑥ 坏输入：不崩、不猜 =="
o=$(cd "$ROOT" && printf '::arc solve /nonexistent.json\n::arc off 不存在的变换\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1)
rc=$?
[ "$rc" -eq 0 ] && ok "坏文件/坏名字不崩（rc=0）" || bad "坏输入崩了" "rc=$rc"
has '读不了任务' "$o" && ok "坏文件明说读不了" || bad "坏文件提示" "$(printf '%s' "$o" | grep ARC | head -2)"
has '图上没有' "$o" && ok "未知变换名报错" || bad "未知变换名" "$(printf '%s' "$o" | grep ARC | tail -2)"

echo "== ⑦ 策略也在图里：深度 1 解不出的两步题，深度 2 解得出 =="
o=$(cd "$ROOT" && printf '::arc sync\n::arc depth 1\n::arc solve %s/symh_up2.json\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" 2>&1)
has '深度 1' "$o" && ok "策略写进图（深度 1）" || bad "策略写图" "$(printf '%s' "$o" | grep 策略 | head -2)"
has '解不出' "$o" && ok "深度 1 → 两步题解不出（策略真的生效）" || bad "深度 1 仍解出" "$(printf '%s' "$o" | grep -E '解出|解不出' | head -1)"
o=$(cd "$ROOT" && printf '::arc sync\n::arc depth 2\n::arc solve %s/symh_up2.json\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" 2>&1)
has '解出：' "$o" && ok "深度 2 → 同一道题解出" || bad "深度 2 解不出" "$(printf '%s' "$o" | grep -E '解出|解不出' | head -2)"
has '\[660066/660066/007700/007700/880088/880088\]' "$o" && ok "两步结果逐格正确" || bad "两步结果" "$(printf '%s' "$o" | grep 预测 | head -1)"

echo "== ⑧ 背景加法归纳：连点成线（纯加法题）=="
o=$(cd "$ROOT" && printf '::arc sync\n::arc solve %s/connect_h.json\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" 2>&1)
has '解出：connecth' "$o" && ok "加法原语解出纯加法题（connecth）" || bad "加法题没解出" "$(printf '%s' "$o" | grep -E '解出|解不出' | head -2)"
has '\[55555555\]' "$o" && ok "预测逐格正确" || bad "预测内容" "$(printf '%s' "$o" | grep 预测 | head -1)"

printf '\nARC：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
