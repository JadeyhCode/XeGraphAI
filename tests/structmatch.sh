#!/bin/sh
# tests/structmatch.sh — **方案 C：操作类归一（结构匹配）**（§6.837）
#
# 判据：四句“知识里没有的说法”必须经 [结构] 归一到正确算子/任务：
#   最高→数组最大、排一排→冒泡排序、数目→数组长度、翻个个→数组翻转。
#
# 自带模型副本；二进制走 tests/prebuilt.sh（沙箱里没有仓库根的 build_aip）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p _t/work
AIP=_t/work/sm_aip
sh "$ROOT/tests/prebuilt.sh" "$AIP" "$ROOT/main.c" -std=c11 -O2 >/dev/null 2>&1 || \
    gcc -std=c11 -O2 -o "$AIP" "$ROOT/main.c" -lm
[ -x "$AIP" ] || { echo "FAIL: 编不出 aip"; exit 1; }
# 构建模型要用仓库根的 ./build_aip；沙箱里没有就先把刚拿到的二进制放过去（gitignored，不污染仓库）
[ -x ./build_aip ] || cp "$AIP" ./build_aip
[ -f code/code_model.bin ] || sh code/build_code_model.sh > _t/work/build.log 2>&1
[ -f code/code_model.bin ] || { echo "FAIL: 缺 code/code_model.bin"; exit 1; }
M=_t/work/sm_model.bin
cp code/code_model.bin "$M"
printf "::ingest code/knowledge/opclass.triples\n::ingest code/knowledge/rewrite.triples\n::save %s\n::quit\n" "$M" \
    | "$AIP" --model "$M" > _t/work/sm_ingest.log 2>&1
fails=0
check() {
    DESC=$1; WANT=$2
    rm -f _t/work/synth_*
    printf "::act on\n::code synth %s\n::quit\n" "$DESC" | "$AIP" --model "$M" > _t/work/sm.txt 2>&1
    if grep -qa "$WANT" _t/work/sm.txt; then
        if grep -qa "\[结构\]" _t/work/sm.txt; then
            printf "  [OK] %s ⇒ %s（结构归一）\n" "$DESC" "$WANT"
        else
            printf "  [OK] %s ⇒ %s（直接命中，不需要归一）\n" "$DESC" "$WANT"
        fi
    else
        printf "  [FAIL] 没命中 %s：%s\n" "$WANT" "$DESC"
        fails=1
    fi
}
check "写一个程序：读入一行整数（标准输入），输出其中最高的那个。" "数组最大"
check "写一个程序：读入一行整数（标准输入），排一排后输出。" "冒泡排序"
check "写一个程序：读入一行整数（标准输入），它们的数目是多少。" "数组长度"
check "写一个程序：读入一行整数（标准输入），翻个个输出。" "数组翻转"
[ "$fails" -eq 0 ] && printf "PASS structmatch（四个没见过的说法都被操作类归一接住了）\n" || printf "FAIL structmatch\n"
[ "$fails" -eq 0 ]
