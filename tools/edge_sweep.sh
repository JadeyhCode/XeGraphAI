#!/bin/sh
# tools/edge_sweep.sh — **把图里每条关系边都问一遍**，测命中率（目标：统计边的命中率、砍死边）
#
# 与 edge_workload.sh 的区别：那个是"人手挑的问句"；这个是**从图里生成**的，
# 每条关系边都问一次（`A R B 吗` + 否定式），所以 0 命中 = **这条边读不出来**，而不是"没问到"。
#
# 用法：sh tools/edge_sweep.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd); cd "$ROOT"
M=${1:-$ROOT/model.bin}
[ -x ./build_aip ] || { echo "FAIL: 先编译 build_aip"; exit 1; }
gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o /tmp/edgegen tools/edge_workload_gen.c -lm 2>/dev/null || {
    echo "FAIL: 编不出 edge_workload_gen"; exit 1; }
W="$ROOT/_t/sweep"; rm -rf "$W"; mkdir -p "$W"
/tmp/edgegen "$M" > "$W/edges.tsv" 2>"$W/gen.log"
echo "生成负载：$(wc -l < "$W/edges.tsv") 条关系边（$(tail -1 "$W/gen.log")）"
{
  echo '::edges stats'
  awk -F'\t' '{ if ($4 == "neg") printf "%s 不 %s %s 吗\n", $1, $2, $3; else printf "%s %s %s 吗\n", $1, $2, $3 }' "$W/edges.tsv"
  awk -F'\t' '{ if ($4 == "pos") printf "::why %s %s %s\n", $1, $2, $3 }' "$W/edges.tsv" | head -80
  echo '::edges stats'
  echo '::edges dead 40'
  echo '::edges stats'
  echo '::quit'
} > "$W/in.txt"
./build_aip --model "$M" < "$W/in.txt" > "$W/out.txt" 2>&1
grep -aE '边命中|角色 |0 命中按关系|死边|^  不|^  [^ ]+ (不 )?[^ ]+ ' "$W/out.txt" | sed 's/^你 > //' | head -60
