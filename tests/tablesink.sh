#!/bin/sh
# tests/tablesink.sh — **四张小表也能从图里扩展**（§6.784）
#
# 归一/切分里有四张"小表"原来写死在函数里：宾语后缀（的一种/一类…）、宾语前缀（够/于/在…）、
# 主语后缀（生活/居住）、可拆动词（吃/喝/用…）。现在它们是数据表，并且**图里的 `<库> --[有]--> <词>`
# 会作为扩展追加进去** —— 教一条就生效，不用重编译。
#
# 判据（全是行为）：
#   ① 没教之前：`企鹅属于鸟类的某类。` 抽成 `企鹅 属于 鸟类的某类`（没剥）；
#   ② `::teach 后缀库 有 的某类` 之后：同一句抽成 `企鹅 属于 鸟类`；
#   ③ `::teach 拆分动词库 有 啃` 之后：`企鹅喜欢啃鱼。` 抽成 `企鹅 啃 鱼`；
#   ④ `::paraphrase sync` 把四张表也写进图（条数对得上）；
#   ⑤ 存盘重载之后 ② 仍然成立。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/tablesink"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
printf '企鹅属于鸟类的某类。\n' > "$W/suf.txt"
printf '企鹅喜欢啃鱼。\n' > "$W/verb.txt"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
has() { printf '%s' "$2" | grep -q -- "$1"; }
run() { printf '%b' "$1" | ./build_aip --model "$2" 2>&1; }

echo "== ① 没教：未知后缀不剥 =="
o=$(run "::paper read $W/suf.txt\n::quit\n" "$W/m.bin")
has '企鹅 属于 鸟类的某类' "$o" && ok "未教时照实抽成「鸟类的某类」" || bad "未教时就剥了？" "$(printf '%s' "$o" | grep '·' | head -2)"

echo "== ② 教一条后缀进图 ⇒ 剥掉 =="
o=$(run "::teach 后缀库 有 的某类\n::paper read $W/suf.txt\n::quit\n" "$W/m.bin")
has '企鹅 属于 鸟类' "$o" && ok "图上的后缀让「鸟类的某类」剥成「鸟类」" || bad "教了没剥" "$(printf '%s' "$o" | grep '·' | head -2)"
printf '%s' "$o" | grep -q '鸟类的某类' && bad "教了之后还留着旧抽法" "$(printf '%s' "$o" | grep '·' | head -2)" || ok "旧抽法不再出现"

echo "== ③ 教一个可拆动词进图 ⇒ 宾语里的动词拆出来 =="
o=$(run "::teach 拆分动词库 有 啃\n::paper read $W/verb.txt\n::quit\n" "$W/m.bin")
has '企鹅 啃 鱼' "$o" && ok "「喜欢啃鱼」拆成 关系=啃 宾语=鱼" || bad "动词没拆" "$(printf '%s' "$o" | grep '·' | head -2)"

echo "== ④ sync 把四张表也写进图 =="
o=$(run '::paraphrase sync\n::quit\n' "$W/m.bin")
has '四张小表' "$o" && ok "sync 报出「同义 + 外部 + 四张小表」" || bad "sync 文案" "$(printf '%s' "$o" | grep 说法 | head -2)"
N=$(printf '%s' "$o" | sed -n 's/.*归一表进图：\([0-9][0-9]*\) 条.*/\1/p' | head -1)
[ "${N:-0}" -ge 40 ] && ok "写入条数 ≥40（实测 $N）" || bad "写入条数偏少（$N）" "$(printf '%s' "$o" | grep 说法 | head -1)"

echo "== ⑤ 存盘重载：图上的表项还在 =="
printf '::teach 后缀库 有 的某类\n::save %s/m2.bin\n::quit\n' "$W" | ./build_aip --model "$W/m.bin" > "$W/save.log" 2>&1
o=$(run "::paper read $W/suf.txt\n::quit\n" "$W/m2.bin")
has '企鹅 属于 鸟类' "$o" && ok "重载后仍然剥得掉（表项随图走）" || bad "重载后失效" "$(printf '%s' "$o" | grep '·' | head -2)"

printf '\n小表下沉：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
