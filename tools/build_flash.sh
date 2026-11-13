#!/bin/sh
# tools/build_flash.sh — 构建 **flash 版模型**（轻量发布：只保留"在测过的负载里被读过"的边）
#
# 做法与 tools/edge_prune.sh 相同（每题一进程 + 位图并集），最后用 `::edges prune --plain`：
#   · 关系边（238 条）几乎全保留；
#   · 共现/回复边只保留测试负载里被选中的那部分。
# 代价（实测，见 docs/model-flash.md）：**自由联想会变浅** —— 40 个种子的游走从平均 5.4 步掉到 1 步。
#
# 用法：sh tools/build_flash.sh [源模型] [输出]     默认 model.bin → model_flash.bin
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
SRC=${1:-$ROOT/model.bin}
OUT=${2:-$ROOT/model_flash.bin}
[ -x ./build_aip ] || { echo "FAIL: 先编译 build_aip"; exit 1; }
W="$ROOT/_t/flash"; rm -rf "$W"; mkdir -p "$W"
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o "$W/edgegen" tools/edge_workload_gen.c -lm || {
    echo "FAIL: 编不出 edge_workload_gen"; exit 1; }

# ---------- 负载：每条关系边问一遍 + 电池纯问句 + 联想的开放问句 ----------
"$W/edgegen" "$SRC" > "$W/edges.tsv" 2>/dev/null
: > "$W/qs.txt"
awk -F'\t' '{ if ($4 == "neg") printf "%s 不 %s %s 吗\n", $1, $2, $3; else printf "%s %s %s 吗\n", $1, $2, $3 }' "$W/edges.tsv" >> "$W/qs.txt"
grep -v '^#' tests/fixtures/bench/battery.txt | cut -f3 | sed 's/\\n/\n/g' \
    | grep -v '^$' | grep -v '^::' >> "$W/qs.txt" || true
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
你 在 想 什 么
给 我 讲 讲 你 的 想法
天气 真 好
你 有 什 么 爱好
GEN
nq=$(grep -c . "$W/qs.txt")

# ---------- ① 每题一个进程跑一遍，导出位图 ----------
i=0
while IFS= read -r q; do
    [ -n "$q" ] || continue
    i=$((i + 1))
    printf '::edges stats\n%b\n::edges dump %s/hits_%04d.bin\n::quit\n' "$q" "$W" "$i" \
        | ./build_aip --model "$SRC" > /dev/null 2>&1 || true
done < "$W/qs.txt"
nh=$(ls "$W"/hits_*.bin 2>/dev/null | wc -l)
echo "负载 $nq 句 ⇒ 位图 $nh 份"

# ---------- ② 干净模型 + 位图并集 → --plain 剪枝 ----------
{
  echo '::edges stats'
  for f in "$W"/hits_*.bin; do echo "::edges hitload $f"; done
  echo '::edges stats'
  echo "::edges prune $W/flash_raw.bin --yes --plain"
  echo '::quit'
} > "$W/pr.txt"
./build_aip --model "$SRC" < "$W/pr.txt" > "$W/pr.out" 2>&1
grep -aE '范围 .*被读过|有关系位（关系事实）|砍掉' "$W/pr.out" | sed 's/^你 > //' | tail -3
[ -s "$W/flash_raw.bin" ] || { echo "FAIL: 没生成 flash 模型"; exit 1; }
cp "$W/flash_raw.bin" "$OUT"

# ---------- ③ 验收：电池 + 体积 ----------
sh tools/benchmark.sh "$OUT" --fast > "$W/bench.log" 2>&1 || true
echo "  flash 电池：$(grep -a '合计：' "$W/bench.log" | tail -1 | sed 's/.*合计：//')"
ls -la "$SRC" "$OUT" | awk '{printf "  %s 字节  %s\n", $5, $9}'
printf '::quit\n' | ./build_aip --model "$OUT" 2>&1 | grep -a '符号节点=' | sed -n 's/.*边=\([0-9]*\).*/  flash 边数：\1/p' | head -1
