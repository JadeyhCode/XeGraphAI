#!/bin/sh
# tools/assoc_quality.sh — **自由联想的质量尺**（目标：判断砍共现边会不会伤"联想"）
#
# 为什么需要：`--plain` 砍掉 4 万条共现/回复边后，电池/套件都过，但那些边是随机游走的底座。
# 这里用 `::walk <词>` 量三件事（引擎的 rng 是**固定种子**，所以结果可复现、可 diff）：
#   ① 联想得出来的比例（非空 idea）
#   ② 路径平均步数（走多远）
#   ③ 路径里出现过的**不同词**数量（覆盖面）
#
# 用法：sh tools/assoc_quality.sh <模型A> [<模型B>]   # 给 B 就做对照
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
A=${1:?用法: sh tools/assoc_quality.sh <模型A> [<模型B>]}
B=${2:-}
[ -x ./build_aip ] || { echo "FAIL: 先编译 build_aip"; exit 1; }
W="$ROOT/_t/assoc"; rm -rf "$W"; mkdir -p "$W"
# 种子词：从图里取（关系边的主语优先），保证是池子里的词
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o "$W/edgegen" tools/edge_workload_gen.c -lm 2>/dev/null || true
"$W/edgegen" "$A" 2>/dev/null | awk -F'\t' '{print $1}' | sort -u | head -40 > "$W/seeds.txt" || true
[ -s "$W/seeds.txt" ] || printf '猫\n狗\n鸟\n鱼\n人\n老师\n小孩\n朋友\n' > "$W/seeds.txt"
n=$(grep -c . "$W/seeds.txt")
echo "种子词：$n 个（来自关系边主语）"

measure() {   # $1=模型  $2=标签
    : > "$W/$2.paths"
    : > "$W/$2.ideas"
    while IFS= read -r w; do
        [ -n "$w" ] || continue
        out=$(printf '::free %s\n::quit\n' "$w" | ./build_aip --model "$1" 2>&1)
        printf '%s\n' "$out" | sed -n 's/^.*\[自由联想\] //p' | head -1 >> "$W/$2.ideas"
        printf '%s\n' "$out" | sed -n 's/^.*\[路径\] //p' | head -1 >> "$W/$2.paths"
    done < "$W/seeds.txt"
    nonempty=$(grep -c . "$W/$2.ideas" || true)
    # 路径步数（按空格分词；"路径"那行可能是空）
    steps=$(awk '{ if ($0 == "") print 0; else { n = split($0, a, " "); print n } }' "$W/$2.paths" | awk '{s+=$1} END{print s+0}')
    # 不同词覆盖：把所有路径拼起来去重
    distinct=$(cat "$W/$2.paths" | tr ' ' '\n' | grep -c . || true)
    uniq=$(cat "$W/$2.paths" | tr ' ' '\n' | grep . | sort -u | wc -l)
    printf '  %-6s 联想出 %s/%s 条｜路径总步数 %s（平均 %.1f）｜路径词次 %s，不同词 %s\n' \
        "$2" "$nonempty" "$n" "$steps" "$(awk -v s="$steps" -v n="$n" 'BEGIN{printf "%.1f", (n>0?s/n:0)}')" "$distinct" "$uniq"
}

echo "== 联想质量 =="
measure "$A" "A"
[ -n "$B" ] && measure "$B" "B"
if [ -n "$B" ]; then
    if diff -q "$W/A.ideas" "$W/B.ideas" > /dev/null 2>&1; then
        echo "  联想产物：两份模型**逐条相同**"
    else
        echo "  联想产物：有差异（$(diff "$W/A.ideas" "$W/B.ideas" | grep -c '^<' || true) 条不同）"
    fi
fi
ls -la "$A" ${B:+"$B"} 2>/dev/null | awk '{printf "  %s 字节  %s\n", $5, $9}'
