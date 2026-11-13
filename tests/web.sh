#!/bin/sh
# tests/web.sh — **网页搜索**：只抽标题/链接/摘要，一律待证；解析器离线可判
#
# 判据（不联网）：
#   ① `::web parse <罐头 HTML>` 抽得出标题/链接/摘要，实体解了、标签没漏；
#   ② 抽不到就照实说"一条也没抽出来"（不猜、不编）；
#   ③ `::web search` 在联网关着时必须拒绝（默认关的纪律）。
# 联网那条路（cn.bing.com）不塞进套件：网络会抖，实测记录见 docs/web.md。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=${BIN:-./build_aip}
MODEL=${1:-model.bin}
[ -x "$BIN" ] || { echo "FAIL: 缺可执行文件 $BIN"; exit 1; }
[ -f "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }
FIX=$ROOT/tests/fixtures/web/bing_sample.html
[ -f "$FIX" ] || { echo "FAIL: 缺罐头 HTML $FIX"; exit 1; }

W="$ROOT/_t/web"; rm -rf "$W"; mkdir -p "$W"
cp "$BIN" "$W/build_aip"; cp "$MODEL" "$W/model.bin"
for f in relations.txt frames.txt patterns.txt lexicon.txt relprops.txt; do
    [ -f "$ROOT/$f" ] && cp "$ROOT/$f" "$W/$f"
done
pass=0; fail=0
ok()  { echo "  [OK]   $1"; pass=$((pass + 1)); }
bad() { echo "  [FAIL] $1"; echo "         证据: $2"; fail=$((fail + 1)); }
run() { ( cd "$W" && printf '%s\n::quit\n' "$1" | ./build_aip --model model.bin 2>&1 ); }

echo "== ① 离线解析：标题/链接/摘要都要抽出来 =="
o=$(run "::web parse $FIX")
printf '%s' "$o" | grep -q '3 条' && ok "抽出 3 条" || bad "条数不对" "$(printf '%s' "$o" | head -3)"
printf '%s' "$o" | grep -q '猫 - 维基百科' && ok "标题抽到" || bad "标题没抽到" "$(printf '%s' "$o" | head -4)"
printf '%s' "$o" | grep -q 'https://zh.wikipedia.org/wiki/' && ok "链接抽到" || bad "链接没抽到" "$(printf '%s' "$o" | head -4)"
printf '%s' "$o" | grep -q '小型猫科动物' && ok "摘要抽到" || bad "摘要没抽到" "$(printf '%s' "$o" | head -6)"
printf '%s' "$o" | grep -q 'grooming & health' && ok "实体解码（&amp; ⇒ &）" || bad "实体没解" "$(printf '%s' "$o" | grep -a 'grooming' | head -2)"
printf '%s' "$o" | grep -q '<li\|<h2\|<p ' && bad "标签漏进答复" "$(printf '%s' "$o" | grep -a '<' | head -2)" || ok "没有标签漏进答复"
printf '%s' "$o" | grep -q '&amp;\|&ensp;' && bad "实体没解干净" "$(printf '%s' "$o" | grep -a '&' | head -2)" || ok "没有残留实体"

echo "== ② 抽不到就照实说（空页面） =="
printf '<html><body><p>nothing here</p></body></html>' > "$W/empty.html"
o=$(run '::web parse empty.html')
printf '%s' "$o" | grep -q '一条也没抽出来' && ok "空页面：明说没抽出来" || bad "空页面乱给结果" "$(printf '%s' "$o" | head -3)"

echo "== ③ 联网关着：必须拒绝（默认关） =="
o=$(run '::web search 猫')
printf '%s' "$o" | grep -q '先 `::net on`\|先 ::net on\|联网这一层默认关' && ok "没开联网就拒绝" || bad "联网没关住" "$(printf '%s' "$o" | head -3)"

echo ""
if [ "$fail" -ne 0 ]; then echo "FAIL web（$pass 过 / $fail 红）"; exit 1; fi
echo "PASS web（$pass 过 / 0 红）"
