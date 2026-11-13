#!/bin/sh
# tests/whynot.sh — **否定问法**：`::why 猫 不 会 飞` 要答「真」（§6.794）
#
# 背景：`::why` 只认三个词，问「A 不 R B」直接报用法 —— 否定事实的依据根本没法问。
# 补上之后踩了一个更隐蔽的坑：`rel_solve` 的 `status` 是**对肯定式**的真值
# （rel.h:1098：求否定成功时 status=-1），所以问否定式时要在**显示上翻转**，
# 不能把 status 再取反一次（第一版就是这么错的：四个问题全答反）。
#
# 判据（四条都要对，正反各两条）：
#   猫 会 飞 → 假（图里有 `猫 不 会 飞`）
#   猫 不 会 飞 → 真
#   猫 会 跳 → 真（图里有 `猫 会 跳`）
#   猫 不 会 跳 → 假
# 再加两条：否定式的推导要显示 `不 会` 那条边；词不在图里要明说。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/whynot"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
ask() { printf '%b\n::quit\n' "$1" | ./build_aip --model "$W/m.bin" 2>&1 | grep -a '\[问\]' | head -1; }
has() { printf '%s' "$2" | grep -q -- "$1"; }

o=$(ask '::why 猫 会 飞'); has '→ 假' "$o" && ok "猫 会 飞 → 假" || bad "猫 会 飞" "$o"
o=$(ask '::why 猫 不 会 飞'); has '不 会 飞 ？ → 真' "$o" && ok "猫 不 会 飞 → 真" || bad "猫 不 会 飞" "$o"
o=$(ask '::why 猫 会 跳'); has '→ 真' "$o" && ok "猫 会 跳 → 真" || bad "猫 会 跳" "$o"
o=$(ask '::why 猫 不 会 跳'); has '不 会 跳 ？ → 假' "$o" && ok "猫 不 会 跳 → 假" || bad "猫 不 会 跳" "$o"

echo "== 否定式的推导要显示那条否定边 =="
o=$(printf '::why 猫 不 会 飞\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1 | grep -a '推导:' | head -1)
has '不 会' "$o" && ok "推导里出现「不 会」" || bad "推导里没有否定边" "$o"

echo "== 词不在图里要明说 =="
o=$(printf '::why 猫 不 会 瞬移\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1 | grep -a '词不在图里\|不知道' | head -1)
[ -n "$o" ] && ok "未知词/未知事实有明确答复" || bad "未知输入没答复" "$(ask '::why 猫 不 会 瞬移')"

printf '\n否定问法：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
