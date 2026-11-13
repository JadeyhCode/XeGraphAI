#!/bin/sh
# tests/edges.sh — **边命中率统计**（目标：统计现有边的命中率、砍死边）
#
# 判据（看护"统计"这件事本身，不测"砍"）：
#   ① `::edges stats` 第一次调用会建表，并报出统计范围 > 0；
#   ② 问一条图里有的关系问题之后，**被读过的边数必须增加**（证明记账接在真正的读路径上）；
#   ③ `::edges dead N` 只列"有关系位"的边，且条数 ≤ 关系边总数；
#   ④ `::edges prune` 默认**拒绝执行**（实测砍了会掉分：67→56/65），必须显式 --yes。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
M=${1:-$ROOT/model.bin}
[ -x ./build_aip ] || sh tests/prebuilt.sh aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/edges_test"; rm -rf "$W"; mkdir -p "$W"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
run() { printf '%b\n::quit\n' "$1" | ./build_aip --model "$M" 2>&1; }
num() { printf '%s' "$1" | sed -n "s/.*$2\([0-9]\+\)$3.*/\1/p" | head -1; }

echo "== ① 建表：统计范围要 > 0 =="
o=$(run '::edges stats')
n=$(printf '%s' "$o" | grep -a '条边进统计范围' | head -1 | grep -oE '[0-9]+' | head -1)
[ "${n:-0}" -gt 1000 ] && ok "进统计范围 $n 条边" || bad "统计范围异常" "$(printf '%s' "$o" | grep -a '边命中' | head -2)"

echo "== ② 问一条事实题 ⇒ 被读过的边增加 =="
o=$(run '::edges stats\n::edges stats\n猫 属于 动物 吗\n::edges stats')
hits=$(printf '%s' "$o" | grep -a '范围 .*被读过' | grep -oE '被读过 [0-9]+' | grep -oE '[0-9]+' || true)
first=$(printf '%s' "$hits" | sed -n 1p); last=$(printf '%s' "$hits" | tail -1)
[ "${last:-0}" -gt "${first:-0}" ] && ok "读过边数 ${first:-?} → $last" \
    || bad "问完没增加" "first=$first last=$last"

echo "== ③ 死边清单只含关系边 =="
o=$(run '::edges stats\n猫 属于 动物 吗\n::edges dead 5')
d=$(printf '%s' "$o" | grep -a '0 命中边共' | head -1 | grep -oE '[0-9]+' | head -1)
[ -n "$d" ] && [ "${d:-0}" -le 1000 ] && ok "死边 $d 条（≤1000，说明只数了关系边）" \
    || bad "死边计数异常" "$(printf '%s' "$o" | grep -a '死边' | head -2)"

echo "== ④ prune 默认拒绝（实测会掉分）=="
o=$(run '::edges prune /tmp/should_not_exist.bin')
printf '%s' "$o" | grep -q '拒绝执行' && ok "默认拒绝" "拒绝执行" || bad "默认没拒绝" "$(printf '%s' "$o" | grep -a '边命中' | head -2)"
[ -e /tmp/should_not_exist.bin ] && bad "竟然写出了文件" /tmp/should_not_exist.bin || ok "没有写出文件" ""

echo "== ⑤ dump → hitload → prune 往返（空位图 + --plain 应砍光）=="
run '::edges stats\n::edges dump '"$W"'/h.bin' > /dev/null 2>&1
[ -s "$W/h.bin" ] && ok "dump 写出位图" "$(wc -c < "$W/h.bin") 字节" || bad "dump 没写" ""
o=$(run "::edges stats\n::edges hitload $W/h.bin\n::edges prune $W/all.bin --yes --plain")
printf '%s' "$o" | grep -q '砍掉' && ok "hitload + prune --plain 能跑" "$(printf '%s' "$o" | grep -a '砍掉' | sed 's/.*砍掉 //' | cut -c1-30)" \
    || bad "prune 流程失败" "$(printf '%s' "$o" | grep -a '边命中' | tail -2)"
[ -s "$W/all.bin" ] && ok "pruned 模型写出来了" "$(wc -c < "$W/all.bin") 字节" || bad "没写出模型" ""

echo "== ⑥ 联想质量守卫（共现边是随机游走的底座，不能被悄悄砍没）=="
o=$(run '::free 猫')
printf '%s' "$o" | grep -q '\[路径\]' && ok "::free 猫 有联想产物" "$(printf '%s' "$o" | grep -a '自由联想' | head -1 | sed 's/.*自由联想\] //' | cut -c1-16)" \
    || bad "联想空了" "$(printf '%s' "$o" | grep -a '自由联想\|路径' | head -2)"
n=$(printf '%s' "$o" | sed -n 's/.*\[路径\] //p' | head -1 | wc -w)
[ "${n:-0}" -ge 2 ] && ok "路径至少 2 步（当前 $n 步）" "ok" || bad "路径太短（$n 步）" "共现边可能被砍坏了"

printf '\n边命中：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
