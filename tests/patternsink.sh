#!/bin/sh
# tests/patternsink.sh — **说法层两张表从图里也能来**（§6.797）
#
# 判据（都是行为，且是"关掉文件"的对照实验）：
#   ① `::pattern sync` 把 `[lead]`/`[confuse]` 写成图事实（`引导语库 --[有]--> 请问?`），
#      `::pattern list` 报内存与图里的条数；
#   ② **关掉文件**（`AIP_NODATA=1`）+ 图里有表 ⇒ 同一句话的答复与"文件在"时**逐字相同**；
#   ③ 关掉文件 + 图里没表 ⇒ 引导语不再被剥（同一句掉成"不太懂"）—— 证明 ② 是图在起作用；
#   ④ 教一条新引导语（`::teach 引导语库 有 斗胆?`；**不能用已经蒸馏进来的词**——教它不会让表变长）后，内存表条数 +1（立刻生效）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/patternsink"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
cp "$ROOT/patterns.txt" "$W/patterns.txt"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
has() { printf '%s' "$2" | grep -q -- "$1"; }
ask() { printf '%b\n::quit\n' "$1" | env ${2:-} ./build_aip --model "${3:-$W/m.bin}" 2>&1; }

echo "== ① sync：两张表进图 =="
o=$(cd "$W" && printf '::pattern sync\n::pattern list\n::quit\n' | ./../..//build_aip --model m.bin 2>&1)
has '进图' "$o" && ok "sync 写入" || bad "sync 没写" "$(printf '%s' "$o" | grep -a '说法层' | head -2)"
has '引导语库' "$o" && has '没听懂库' "$o" && ok "list 报两个库" || bad "list 内容" "$(printf '%s' "$o" | grep -a '库' | head -2)"

echo "== ①b list 要逐桶报全 19 张表（§6.814）=="
n=$(printf '::pattern list\n::quit\n' | "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -ac '内存（文件+图）' || true)
[ "${n:-0}" -ge 19 ] && ok "list 报出 $n 行（≥19）" "" || bad "list 只报了 ${n:-0} 行" ""

echo "== ② 关掉文件 + 图里有表 ⇒ 答复与文件在时逐字相同 =="
Q='请问 猫 会 飞 吗'
a=$(ask "$Q" "" "$W/m.bin")
b=$(ask "$Q" "AIP_NODATA=1" "$W/m.bin.synced")
# 先把 sync 后的模型存出来（图里带表）
(cd "$W" && printf '::pattern sync\n::save m2.bin\n::quit\n' | "$ROOT/build_aip" --model m.bin > /dev/null 2>&1)
b=$(ask "$Q" "AIP_NODATA=1" "$W/m2.bin")
ra=$(printf '%s' "$a" | grep -a 'AI > ' | head -1)
rb=$(printf '%s' "$b" | grep -a 'AI > ' | head -1)
[ -n "$ra" ] && [ "$ra" = "$rb" ] && ok "两条来源答复相同：$(printf '%s' "$ra" | sed 's/.*AI > //')" \
    || bad "答复不同" "文件=$ra / 图=$rb"

echo "== ③ 关掉文件 + 图里没表 ⇒ 引导语不被剥（对照） =="
c=$(ask "$Q" "AIP_NODATA=1" "$W/m.bin")
rc=$(printf '%s' "$c" | grep -a 'AI > ' | head -1)
[ -n "$rc" ] && [ "$rc" != "$ra" ] && ok "空图时退化成兜底（$(printf '%s' "$rc" | sed 's/.*AI > //')）" \
    || bad "关文件+空图仍答出同样内容（说明不是图在起作用）" "$rc"

echo "== ④ 教一条新引导语立刻进内存表 =="
o=$( (cd "$W" && printf '::pattern list\n::teach 引导语库 有 斗胆?\n::pattern list\n::quit\n' | "$ROOT/build_aip" --model m.bin 2>&1) )
n1=$(printf '%s' "$o" | grep -a '引导语库：内存' | head -1 | sed 's/.*内存（文件+图）\([0-9]*\) 条.*/\1/')
n2=$(printf '%s' "$o" | grep -a '引导语库：内存' | tail -1 | sed 's/.*内存（文件+图）\([0-9]*\) 条.*/\1/')
[ "${n1:-0}" -gt 0 ] && [ "${n2:-0}" -gt "${n1:-0}" ] && ok "教之后内存表 $n1 → $n2" \
    || bad "教之后内存表没变" "$n1 → $n2"

echo "== ⑤ 量词 / 拒答两桶也能从图里来（§6.805）=="
# 先在沙箱里 sync 并另存
(cd "$W" && printf '::pattern sync\n::save m3.bin\n::quit\n' | "$ROOT/build_aip" --model m.bin > /dev/null 2>&1)
r1=$(printf '猫 多高\n::quit\n' | AIP_NODATA=measure "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
r2=$(printf '猫 多高\n::quit\n' | AIP_NODATA=measure "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$r1" | grep -q '不 会 量' && ok "关文件+图里有 ⇒ 量词行为恢复" "$(printf '%s' "$r1" | sed 's/.*AI > //' | cut -c1-20)" \
    || bad "量词没从图里恢复" "$r1"
[ "$r1" != "$r2" ] && ok "关文件+图里没有 ⇒ 行为不同（对照）" "$(printf '%s' "$r2" | sed 's/.*AI > //' | cut -c1-20)" \
    || bad "有没有图都一样（说明不是图在起作用）" "$r2"
q='量子纠缠 是 什么 原理'
r3=$(printf '%s\n::quit\n' "$q" | AIP_NODATA=refuse "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
r4=$(printf '%s\n::quit\n' "$q" | AIP_NODATA=refuse "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
[ -n "$r3" ] && [ "$r3" != "$r4" ] && ok "拒答说法从图里来（对照不同）" "$(printf '%s' "$r3" | sed 's/.*AI > //' | cut -c1-16)" \
    || bad "拒答说法没从图里来" "图=$r3 无图=$r4"

echo "== ⑥ 同义词也能从图里来（§6.806）=="
q1='猫 属于 动物'
r1=$(printf '%s\n猫咪 属于 动物 吗\n::quit\n' "$q1" | AIP_NODATA=syn "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | tail -1)
r2=$(printf '%s\n猫咪 属于 动物 吗\n::quit\n' "$q1" | AIP_NODATA=syn "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | tail -1)
printf '%s' "$r1" | grep -q '对' && ok "关 syn + 图里有 ⇒ 猫咪 被认成 猫" "$(printf '%s' "$r1" | sed 's/.*AI > //' | cut -c1-16)" \
    || bad "同义词没从图里恢复" "$r1"
[ "$r1" != "$r2" ] && ok "关 syn + 图里没有 ⇒ 行为不同（对照）" "$(printf '%s' "$r2" | sed 's/.*AI > //' | cut -c1-16)" \
    || bad "有没有图都一样" "$r2"

echo "== ⑦ 教一个同义词立刻生效（不用改文件/重编译）=="
o=$(printf '::teach 同义词库 有 喵咪\n::teach 喵咪 同义 猫\n猫 属于 动物\n喵咪 属于 动物 吗\n::quit\n' | "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | tail -1)
printf '%s' "$o" | grep -q '对' && ok "教完立刻整词替换" "$(printf '%s' "$o" | sed 's/.*AI > //' | cut -c1-20)" \
    || bad "教了同义词不生效" "$o"

echo "== ⑧ 词缀 / 句尾两桶也能从图里来（§6.807）=="
a1=$(printf '人们 属于 什么\n::quit\n' | AIP_NODATA=affix "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
a2=$(printf '人们 属于 什么\n::quit\n' | AIP_NODATA=affix "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$a1" | grep -q '人 属于 动物' && ok "关 affix + 图里有 ⇒ 人们 认成 人" "$(printf '%s' "$a1" | sed 's/.*AI > //' | cut -c1-18)" \
    || bad "词缀没从图里恢复" "$a1"
[ "$a1" != "$a2" ] && ok "关 affix + 图里没有 ⇒ 行为不同（对照）" "" || bad "有没有图都一样" "$a2"
t1=$(printf '猫 会 飞 吗 啊\n::quit\n' | AIP_NODATA=tail "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
t2=$(printf '猫 会 飞 吗 啊\n::quit\n' | AIP_NODATA=tail "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$t1" | grep -q '不 对' && ok "关 tail + 图里有 ⇒ 句尾语气词被剥" "$(printf '%s' "$t1" | sed 's/.*AI > //' | cut -c1-18)" \
    || bad "句尾没从图里恢复" "$t1"
[ "$t1" != "$t2" ] && ok "关 tail + 图里没有 ⇒ 行为不同（对照）" "" || bad "有没有图都一样" "$t2"

echo "== ⑨ 填充词 / 词类两桶也能从图里来（§6.808）=="
f1=$(printf '猫 真的 会 飞 吗\n::quit\n' | AIP_NODATA=fill "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
f2=$(printf '猫 真的 会 飞 吗\n::quit\n' | AIP_NODATA=fill "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$f1" | grep -q '不 对' && ok "关 fill + 图里有 ⇒ 真的 被剥掉" "$(printf '%s' "$f1" | sed 's/.*AI > //' | cut -c1-18)" \
    || bad "填充词没从图里恢复" "$f1"
[ "$f1" != "$f2" ] && ok "关 fill + 图里没有 ⇒ 行为不同（对照）" "" || bad "有没有图都一样" "$f2"
c1=$(printf '今天 天气 怎么样\n::quit\n' | AIP_NODATA=class "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
c2=$(printf '今天 天气 怎么样\n::quit\n' | AIP_NODATA=class "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$c1" | grep -q '天气 我 不 知道' && ok "关 class + 图里有 ⇒ 天气类认出来" "$(printf '%s' "$c1" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "词类没从图里恢复" "$c1"
[ "$c1" != "$c2" ] && ok "关 class + 图里没有 ⇒ 行为不同（对照）" "" || bad "有没有图都一样" "$c2"

echo "== ⑩ 答复模板（[say]）也能从图里来（§6.809）=="
s1=$(printf '猫 会 飞 吗\n::quit\n' | AIP_NODATA=say "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
s2=$(printf '猫 会 飞 吗\n::quit\n' | AIP_NODATA=say "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$s1" | grep -q '不 对' && ok "关 say + 图里有 ⇒ 判词模板回来了" "$(printf '%s' "$s1" | sed 's/.*AI > //' | cut -c1-16)" \
    || bad "答复模板没从图里恢复" "$s1"
printf '%s' "$s2" | grep -q '不 对' && bad "无图也能答出判词（对照失效）" "$s2" || ok "关 say + 图里没有 ⇒ 判词丢了（对照）" "$(printf '%s' "$s2" | sed 's/.*AI > //' | cut -c1-16)"

echo "== ⑪ 改写规则（[shape]，242 行）也能从图里来（§6.810）=="
p1=$(printf '猫 你 喜欢 什么\n::quit\n' | AIP_NODATA=shape "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
p2=$(printf '猫 你 喜欢 什么\n::quit\n' | AIP_NODATA=shape "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$p1" | grep -q '我 记 的 是' && ok "关 shape + 图里有 ⇒ 宾语前置规则回来了" "$(printf '%s' "$p1" | sed 's/.*AI > //' | cut -c1-18)" \
    || bad "改写规则没从图里恢复" "$p1"
[ "$p1" != "$p2" ] && ok "关 shape + 图里没有 ⇒ 行为不同（对照）" "$(printf '%s' "$p2" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "有没有图都一样" "$p2"

echo "== ⑫ 答复规则（[reply]，87 条）也能从图里来（§6.811）=="
q1=$(printf '讲个笑话\n::quit\n' | AIP_NODATA=reply "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
q2=$(printf '讲个笑话\n::quit\n' | AIP_NODATA=reply "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$q1" | grep -q '不 会' && ok "关 reply + 图里有 ⇒ 拒答规则回来了" "$(printf '%s' "$q1" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "答复规则没从图里恢复" "$q1"
[ "$q1" != "$q2" ] && ok "关 reply + 图里没有 ⇒ 行为不同（对照）" "$(printf '%s' "$q2" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "有没有图都一样" "$q2"

echo "== ⑬ 疑问词 / 裸回话两桶也能从图里来（§6.812）=="
w1=$(printf '猫 呢\n::quit\n' | AIP_NODATA=qword "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
w2=$(printf '猫 呢\n::quit\n' | AIP_NODATA=qword "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$w1" | grep -q '不 太 懂' && ok "关 qword + 图里有 ⇒ 疑问词认得" "$(printf '%s' "$w1" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "疑问词没从图里恢复" "$w1"
[ "$w1" != "$w2" ] && ok "关 qword + 图里没有 ⇒ 行为不同（对照）" "$(printf '%s' "$w2" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "有没有图都一样" "$w2"
b1=$(printf '对\n::quit\n' | AIP_NODATA=bare "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
b2=$(printf '对\n::quit\n' | AIP_NODATA=bare "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$b1" | grep -q '嗯' && ok "关 bare + 图里有 ⇒ 裸回话认得" "$(printf '%s' "$b1" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "裸回话没从图里恢复" "$b1"
[ "$b1" != "$b2" ] && ok "关 bare + 图里没有 ⇒ 行为不同（对照）" "$(printf '%s' "$b2" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "有没有图都一样" "$b2"

echo "== ⑭ 别名 / 占位词 / 代词也能从图里来（§6.813）=="
al1=$(printf '猫 拥有 尾巴\n::quit\n' | AIP_NODATA=alias "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
al2=$(printf '猫 拥有 尾巴\n::quit\n' | AIP_NODATA=alias "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$al1" | grep -q '我 知道' && ok "关 alias + 图里有 ⇒ 拥有 认成 有" "$(printf '%s' "$al1" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "关系别名没从图里恢复" "$al1"
[ "$al1" != "$al2" ] && ok "关 alias + 图里没有 ⇒ 行为不同（对照）" "" || bad "有没有图都一样" "$al2"
sl1=$(printf '哪一类 属于 动物\n::quit\n' | AIP_NODATA=slot "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | head -1)
sl2=$(printf '哪一类 属于 动物\n::quit\n' | AIP_NODATA=slot "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | head -1)
printf '%s' "$sl1" | grep -q '属于 动物 的 有' && ok "关 slot + 图里有 ⇒ 占位词认得" "$(printf '%s' "$sl1" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "占位词没从图里恢复" "$sl1"
[ "$sl1" != "$sl2" ] && ok "关 slot + 图里没有 ⇒ 行为不同（对照）" "" || bad "有没有图都一样" "$sl2"
pr1=$(printf '猫 属于 动物\n它 属于 动物 吗\n::quit\n' | AIP_NODATA=pron "$ROOT/build_aip" --model "$W/m3.bin" 2>&1 | grep -a 'AI > ' | tail -1)
pr2=$(printf '猫 属于 动物\n它 属于 动物 吗\n::quit\n' | AIP_NODATA=pron "$ROOT/build_aip" --model "$W/m.bin" 2>&1 | grep -a 'AI > ' | tail -1)
printf '%s' "$pr1" | grep -q '对' && ok "关 pron + 图里有 ⇒ 它 指代 猫" "$(printf '%s' "$pr1" | sed 's/.*AI > //' | cut -c1-14)" \
    || bad "代词没从图里恢复" "$pr1"
[ "$pr1" != "$pr2" ] && ok "关 pron + 图里没有 ⇒ 行为不同（对照）" "" || bad "有没有图都一样" "$pr2"

printf '\n说法层下沉：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
