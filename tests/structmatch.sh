#!/bin/sh
# tests/structmatch.sh — **方案 C：操作类归一（结构匹配）**（§6.837）
#
# 判据：四句“知识里没有的说法”必须经 [结构] 归一到正确算子/任务：
#   峰值→数组最大、排一排→冒泡排序、数目→数组长度、翻个个→数组翻转。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p _t/work
# 预置模型可能早于 opclass.triples（构建哈希只看源码/头文件）⇒ 缺关系就重建
if ! printf '::rel 操作类:取最大\n::quit\n' | ./build_aip --model code/code_model.bin 2>/dev/null | grep -qa -- '--\[词\]-->'; then
    sh code/build_code_model.sh > _t/work/build.log 2>&1
fi
fails=0
check() {
    DESC=$1; WANT=$2
    rm -f _t/work/synth_*
    printf "::act on\n::code synth %s\n::quit\n" "$DESC" | ./build_aip --model code/code_model.bin > _t/work/sm.txt 2>&1
    grep -qa "\[结构\]" _t/work/sm.txt || { printf "  [FAIL] 没走结构匹配：%s\n" "$DESC"; fails=1; return; }
    grep -qa "$WANT" _t/work/sm.txt || { printf "  [FAIL] 归一后没命中 %s：%s\n" "$WANT" "$DESC"; fails=1; return; }
    printf "  [OK] %s ⇒ %s\n" "$DESC" "$WANT"
}
check "写一个程序：读入一行整数（标准输入），输出其中最高的那个。" "数组最大"
check "写一个程序：读入一行整数（标准输入），排一排后输出。" "冒泡排序"
check "写一个程序：读入一行整数（标准输入），它们的数目是多少。" "数组长度"
check "写一个程序：读入一行整数（标准输入），翻个个输出。" "数组翻转"
[ "$fails" -eq 0 ] && printf "PASS structmatch（四个没见过的说法都被操作类归一接住了）\n" || printf "FAIL structmatch\n"
[ "$fails" -eq 0 ]
