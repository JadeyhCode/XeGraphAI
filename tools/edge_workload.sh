#!/bin/sh
# tools/edge_workload.sh — **边命中率的工作负载**（目标：统计现有边的命中率、砍死边）
#
# 为什么需要固定负载：边"没被读过"只有**相对于一段负载**才有意义。
# 这里用一组覆盖典型读取路径的问句（关系查询/否定/多跳/动态关系/算术/未知词），
# 在一个进程里跑完，让 `::edges stats` 记账，最后再打一次统计。
#
# 用法：sh tools/edge_workload.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
M=${1:-$ROOT/model.bin}
[ -x ./build_aip ] || { echo "FAIL: 先编译 build_aip"; exit 1; }
W="$ROOT/_t/edges"; rm -rf "$W"; mkdir -p "$W"
{
  echo '::edges stats'            # 建表（之后开始记账）
  # —— 关系事实（直接读边）——
  echo '猫 属于 动物 吗'
  echo '猫 会 飞 吗'
  echo '猫 会 跳 吗'
  echo '猫 喜欢 什么'
  echo '猫 吃 什么'
  echo '猫 怕 什么'
  echo '鸟 属于 动物 吗'
  echo '鸟 会 飞 吗'
  echo '鸟 会 游 泳 吗'
  echo '狗 属于 动物 吗'
  echo '狗 会 飞 吗'
  echo '鱼 会 游 泳 吗'
  echo '鱼 属于 动物 吗'
  echo '老鼠 怕 猫 吗'
  echo '人 属于 动物 吗'
  # —— 多跳/推理（transitive）——
  echo '猫 属于 生物 吗'
  echo '鸟 属于 生物 吗'
  echo '::why 猫 属于 动物'
  echo '::why 猫 不会 飞'
  echo '::der 猫 属于 动物'
  echo '::der 鸟 属于 生物'
  # —— 动态关系（学一条再查）——
  echo '::teach 小王 拿 钥匙'
  echo '小王 拿 钥匙 吗'
  echo '::why 小王 拿 钥匙'
  echo '::teach 小王 不 拿 雨伞'
  echo '小王 不 拿 雨伞 吗'
  # —— 排除法（用动态关系）——
  echo '甲 拿 红球'
  echo '乙 不 拿 红球'
  echo '::solve 拿 红球 蓝球 -- 甲 乙'
  # —— 算术/单位（不该读任何边，用来确认"没算进去"）——
  echo '3+4 等于 几'
  echo '3 米 等于 多少 厘米'
  # —— 未知/兜底 ——
  echo '麒麟 属于 神兽 吗'
  echo '火星 上 有 水 吗'
  echo '::edges stats'
  echo '::quit'
} > "$W/in.txt"
./build_aip --model "$M" < "$W/in.txt" 2>&1 | grep -aE '边命中|角色 |0 命中按关系' | sed 's/^你 > //'
