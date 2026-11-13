#!/bin/sh
# tools/edge_prune.sh — **砍死边**（目标：统计边的命中率、砍死边）
#
# 三条踩过的坑，决定了现在的形状：
#   ① **问句会让它学习**（紧凑问句被当陈述：实测 3 句 +23 条边）⇒ 边下标漂移，同进程统计不可靠；
#   ② 图一变，`(token, 下标)` 的全局映射就错位（新边串到下一个 token 的区间里）；
#   ③ 一份负载不够：只看"每条关系边问一遍"会漏掉电池里**紧凑问句**用的边，砍了就掉分。
# 现在的做法：**每题一个进程**（进程内只跑一句，图不变）+ 位图并集，最后在干净模型上剪枝。
#
# 用法：sh tools/edge_prune.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
M=${1:-$ROOT/model.bin}
[ -x ./build_aip ] || { echo "FAIL: 先编译 build_aip"; exit 1; }
W="$ROOT/_t/prune"; rm -rf "$W"; mkdir -p "$W"
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o "$W/edgegen" tools/edge_workload_gen.c -lm || {
    echo "FAIL: 编不出 edge_workload_gen"; exit 1; }
"$W/edgegen" "$M" > "$W/edges.tsv" 2>"$W/gen.log"
: > "$W/qs.txt"
awk -F'\t' '{ if ($4 == "neg") printf "%s 不 %s %s 吗\n", $1, $2, $3; else printf "%s %s %s 吗\n", $1, $2, $3 }' "$W/edges.tsv" >> "$W/qs.txt"
grep -v '^#' tests/fixtures/bench/battery.txt | cut -f3 | sed 's/\\n/\n/g' \
    | grep -v '^$' | grep -v '^::' >> "$W/qs.txt" || true
# 生成层负载：没有知识可查的问句会走"联想/帧生成"，那是 4 万条共现/回复边唯一的用武之地
cat >> "$W/qs.txt" <<'GEN'
火星 上 有 什么
你 觉得 呢
讲 个 故事 吧
今天 心情 怎么样
你 喜欢 什么
为 什么 天 是 蓝 的
你 最近 在 忙 什么
说 点 别的
你 会 写 诗 吗
这 件 事 你 怎么 看
我 有点 累
你 在 想 什么
给 我 讲 讲 你 的 想法
天气 真 好
你 有 什么 爱好
GEN
nq=$(grep -c . "$W/qs.txt")
echo "负载：$nq 句（每题一个进程，避免学习改图导致下标漂移）"

i=0
while IFS= read -r q; do
    [ -n "$q" ] || continue
    i=$((i + 1))
    printf '::edges stats\n%b\n::edges dump %s/hits_%04d.bin\n::quit\n' "$q" "$W" "$i" \
        | ./build_aip --model "$M" > /dev/null 2>&1 || true
done < "$W/qs.txt"
nh=$(ls "$W"/hits_*.bin 2>/dev/null | wc -l)
echo "位图：$nh 份（$nq 句）"
[ "$nh" -gt 0 ] || { echo "FAIL: 没有位图"; exit 1; }

# 干净模型 + 位图并集 → 剪枝
{
  echo '::edges stats'
  for f in "$W"/hits_*.bin; do echo "::edges hitload $f"; done
  echo '::edges stats'
  echo "::edges prune $W/pruned.bin --yes"
  echo '::quit'
} > "$W/pr.txt"
./build_aip --model "$M" < "$W/pr.txt" > "$W/pr.out" 2>&1
grep -aE '范围 .*被读过|有关系位（关系事实）|砍掉' "$W/pr.out" | sed 's/^你 > //' | tail -3
[ -s "$W/pruned.bin" ] || { echo "FAIL: 没生成 pruned.bin"; exit 1; }

# 逐题比对
cat > "$W/verify.txt" <<'QS'
猫 属于 动物 吗
猫 会 飞 吗
猫 喜欢 什么
鸟 会 飞 吗
狗 会 飞 吗
鱼 会 游 泳 吗
老鼠 怕 猫 吗
人 属于 动物 吗
老师 有 学生 吗
小孩 怕 黑 吗
猫 属于 生物 吗
猫有翅膀吗
猫属于植物吗
鱼有鳞吗
鸟有什么
猫 有几 条 腿
::why 猫 属于 动物
::why 猫 不会 飞
::der 鸟 属于 生物
::quit
QS
./build_aip --model "$M"             < "$W/verify.txt" 2>&1 | grep -a 'AI >\|推导\]' > "$W/a.txt"
./build_aip --model "$W/pruned.bin"  < "$W/verify.txt" 2>&1 | grep -a 'AI >\|推导\]' > "$W/b.txt"
if diff -q "$W/a.txt" "$W/b.txt" > /dev/null 2>&1; then
    echo "  逐题比对：19 条问题，答案**逐字相同**"
else
    echo "  [FAIL] 逐题比对有差异："; diff "$W/a.txt" "$W/b.txt" | head -8
fi

sh tools/benchmark.sh "$W/pruned.bin" --fast > "$W/bench.log" 2>&1 || true
echo "  pruned 电池：$(grep -a '合计：' "$W/bench.log" | tail -1 | sed 's/.*合计：//')"
echo "  原模型电池：$(sh tools/benchmark.sh "$M" --fast 2>&1 | grep -a '合计：' | tail -1 | sed 's/.*合计：//')"
ls -la "$M" "$W/pruned.bin" | awk '{printf "  %s 字节  %s\n", $5, $9}'
