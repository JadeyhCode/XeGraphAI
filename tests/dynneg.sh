#!/bin/sh
# tests/dynneg.sh — **动态关系的否定**：存得对、显示得对、查得对、能拿来求解（§6.793）
#
# 背景：`::teach 乙 不 拿 红球` 说"教会了"，但边上**没打否定位**，
# 于是 `::rel 乙` 显示成「乙 --[拿]--> 红球」（账面就是错的），`::solve` 也查不到约束。
# 根因两处：① `rel_direct` 的动态分支没排除否定边、`rel_direct_neg` 直接拒绝动态关系；
# ② `::solve` 用 `& REL_BIT(rid)` 判事实 —— 动态关系（rid ≥ REL_DYN_BASE）永远查不到。
#
# 判据：
#   ① 教一条动态否定 ⇒ `::rel` 显示 `--[不 拿]-->`；
#   ② 用动态关系做 3×3 排除法（::law unique 拿）⇒ 推出完整答案表；
#   ③ 约束不够时仍说"推不出来"（不猜）；
#   ④ 存盘重载后 ① 仍成立。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/dynneg"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
has() { printf '%s' "$2" | grep -q -- "$1"; }
run() { printf '%b' "$1" | ./build_aip --model "$2" 2>&1; }

echo "== ① 动态否定：显示要带「不」=="
o=$(run '::teach 乙 不 拿 红球\n::rel 乙\n::quit\n' "$W/m.bin")
has '\[不 拿\]' "$o" && ok "::rel 显示 --[不 拿]-->" || bad "显示成肯定边" "$(printf '%s' "$o" | grep -a '拿' | head -2)"
has '\-\-\[拿\]' "$o" && bad "同一对词又出现肯定边" "$(printf '%s' "$o" | grep -a '拿' | head -2)" || ok "没有同时冒出一条肯定边"

echo "== ② 动态关系也能拿来求解（3×3 排除法）=="
o=$(run '红球 是 球\n蓝球 是 球\n绿球 是 球\n甲 是 人\n乙 是 人\n丙 是 人\n甲 拿 红球\n乙 不 拿 红球\n丙 不 拿 蓝球\n::law unique 拿\n::solve 拿 红球 蓝球 绿球 -- 甲 乙 丙\n::quit\n' "$W/m.bin")
has '乙 拿 蓝球' "$o" && ok "推出 乙=蓝球（列排除）" || bad "没推出来" "$(printf '%s' "$o" | grep -a '求解' | head -4 | tr '\n' ' ')"
has '丙 拿 绿球' "$o" && ok "推出 丙=绿球（行排除）" || bad "第三行没推" "$(printf '%s' "$o" | grep -a '⇒' | head -4 | tr '\n' ' ')"

echo "== ③ 约束不够：仍说推不出来（不猜）=="
o=$(run '红球 是 球\n蓝球 是 球\n绿球 是 球\n甲 是 人\n乙 是 人\n丙 是 人\n甲 拿 红球\n::law unique 拿\n::solve 拿 红球 蓝球 绿球 -- 甲 乙 丙\n::quit\n' "$W/m.bin")
has '没有能被排除法逼出来的结论' "$o" && ok "约束不够时明说（不猜）" || bad "硬猜了" "$(printf '%s' "$o" | grep -a '求解' | tail -2 | tr '\n' ' ')"

echo "== ④ 存盘重载：否定位还在 =="
printf '::teach 乙 不 拿 红球\n::save %s/m2.bin\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" > "$W/save.log" 2>&1
o=$(run '::rel 乙\n::quit\n' "$W/m2.bin")
has '\[不 拿\]' "$o" && ok "重载后仍显示 --[不 拿]-->" || bad "重载后否定丢了" "$(printf '%s' "$o" | grep -a '拿' | head -2)"

printf '\n动态否定：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
