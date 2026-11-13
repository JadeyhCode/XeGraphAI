#!/bin/sh
# tests/parasink.sh — **说法归一表沉进图**（§6.781）
#
# 归一原来是 main/paper 里 20 多个 `if (!strcmp(w,"可以")) return "会";` —— 那是代码，不是知识。
# 这一版把它变成**图上的事实**：`可以 --[同义]--> 会`。
#
# 判据（全是行为）：
#   ① 图上没有那条同义边时：`企鹅拿手游泳` **一条都抽不出来**（不猜）；
#   ② `::teach 拿手 同义 会` 之后：同一句**抽出 `企鹅 会 游泳`** —— 一条图事实就够，不重编译；
#   ③ `::paraphrase sync` 把内置表写进图：19 条，`list` 逐条 19/19；
#   ④ 存盘重载后仍然成立（同义边随图走，不是内存里的表）。
#
# 用法：sh tests/parasink.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
[ -s "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }

W="$ROOT/_t/parasink"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
cp "$ROOT/tests/fixtures/para_skill.txt" "$W/skill.txt"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
has() { printf '%s' "$2" | grep -q -- "$1"; }

run() { printf '%b' "$1" | ./build_aip --model "$2" 2>&1; }

echo "== ① 图上没有同义边：抽不出（不猜）=="
o=$(run "::paper read $W/skill.txt\n::quit\n" "$W/m.bin")
has '三段式断言' "$o" && ok "没归一就不抽（明说一条都没抽到）" || bad "空图行为" "$(printf '%s' "$o" | grep -E '抽到|·' | head -2)"
printf '%s' "$o" | grep -q '企鹅 会 游泳' && bad "空图却抽出了规范三元组" "" || ok "空图不会凭空归一"

echo "== ② 教一条图上的同义（拿手 → 会）⇒ 同一句抽得出 =="
o=$(run "::teach 拿手 同义 会\n::paper read $W/skill.txt\n::quit\n" "$W/m.bin")
has '企鹅 会 游泳' "$o" && ok '图上的同义边让「企鹅拿手游泳」归一成「企鹅 会 游泳」' || bad "教了还是没归一" "$(printf '%s' "$o" | grep -E '抽到|·' | head -3)"

echo "== ③ 内置表进图：19 条 + list 逐条对得上 =="
o=$(run '::paraphrase sync\n::paraphrase list\n::quit\n' "$W/m.bin")
has '归一表进图：[0-9][0-9]* 条' "$o" && has '同义 19' "$o" && ok "sync 写入（同义 19 条 + 四张小表）" || bad "sync" "$(printf '%s' "$o" | grep 说法 | head -2)"
has '19 / 19' "$o" && ok "list 校验 19/19（图上逐条都有）" || bad "list 校验" "$(printf '%s' "$o" | grep 说法 | tail -2)"

echo "== ④ 存盘重载：同义边随图走 =="
printf '::teach 拿手 同义 会\n::save %s/m2.bin\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" > "$W/save.log" 2>&1
o=$(run "::paper read $W/skill.txt\n::quit\n" "$W/m2.bin")
has '企鹅 会 游泳' "$o" && ok "新进程加载后归一仍生效（图里的边存下来了）" || bad "重载后归一失效" "$(printf '%s' "$o" | grep -E '抽到|·' | head -2)"

echo "== ⑤ 归一顺序：图 > 外部映射 > 内置种子（图上改一条就改行为）=="
o=$(run "::teach 拿手 同义 怕\n::paper read $W/skill.txt\n::quit\n" "$W/m.bin")
has '企鹅 怕 游泳' "$o" && ok "教成「怕」⇒ 归一结果跟着图变（不是写死的）" || bad "图上改方向没生效" "$(printf '%s' "$o" | grep -E '抽到|·' | head -2)"

printf '\n说法归一：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
