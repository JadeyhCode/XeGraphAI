#!/usr/bin/env python3
"""tools/code_hidden.py — C 代码合成的**隐藏输入测试**（不是骨架自测）。

为什么要有：
  `tests/codebench.sh` 的判据是"产物自己的 ok/FAIL 行全绿"——骨架自测是自己写的，
  只要骨架能跑就绿，**不验它是否真的按任务描述读输入/写输出**。
  这里补上"隐藏输入"：给 argv/stdin，跑生成的程序，比 stdout（过滤掉自测的 ok/FAIL 行）。
  这样才量得出"写出来的程序是不是解决了描述里的任务"。

协议：
  · 只喂描述（`::code synth`），不教、不喂答案；生成的 .c 由 build_aip 自己写；
  · 每个任务独立清理 `_t/work/synth_*.c`，跑完只取新生成的那个；
  · 隐藏输入/期望只在**判分**时用，不进图；
  · 自测行（^ok / ^FAIL）过滤掉，不计入任务输出。

用法：python3 tools/code_hidden.py [--model code/code_model.bin] [--only battery|heldout]
退出码：battery 全过 = 0，否则 1；heldout 只报告，不作为门。
"""
import argparse, glob, os, re, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

BATTERY = [
    dict(name="1到N求和", desc="写一个程序：读入 N（命令行参数），输出 1 到 N 的和。",
         kind="argv", data=["5"], want="15"),
    dict(name="华氏转摄氏", desc="写一个程序：读入华氏温度（命令行参数），输出摄氏温度（保留两位小数）。",
         kind="argv", data=["212"], want=r"100\.00"),
    dict(name="闰年", desc="写一个程序：读入年份（命令行参数），是闰年输出 1，否则输出 0。",
         kind="argv", data=["2024"], want="1"),
    dict(name="完全平方数", desc="写一个程序：读入一个整数（命令行参数），是完全平方数输出 1，否则输出 0。",
         kind="argv", data=["49"], want="1"),
    dict(name="素数", desc="写一个程序：读入 N（命令行参数），输出 1..N 里所有素数，每行一个。",
         kind="argv", data=["10"], want="2\n3\n5\n7"),
    dict(name="字符串倒序", desc="写一个程序：读入一行字符串（stdin，≤200 字符），倒序输出。",
         kind="stdin", data=["abc\n"], want="cba"),
    dict(name="斐波那契", desc="写一个程序：读入 N，输出斐波那契数列前 N 项（用空格分隔）。",
         kind="argv", data=["6"], want="0 1 1 2 3 5"),
    dict(name="最值平均", desc="写一个程序：读入 N 个整数，输出它们的最大值、最小值、平均值。",
         kind="stdin", data=["3\n1 5 2\n"], want=r"max=5 min=1 avg=2\.6[67]"),
]
HELDOUT = [
    dict(name="平方和", desc="写一个程序：读入 N（命令行参数），输出 1 到 N 的平方和。",
         kind="argv", data=["4"], want="30"),
    dict(name="元音个数", desc="写一个程序：读入一行字符串（stdin），统计其中的元音字母个数并输出。",
         kind="stdin", data=["hello\n"], want="2"),
    dict(name="摄氏转华氏", desc="写一个程序：读入摄氏温度（命令行参数），输出华氏温度（保留两位小数）。",
         kind="argv", data=["100"], want=r"212\.00"),
    dict(name="素数判断", desc="写一个程序：读入一个整数（命令行参数），是素数输出 1，否则输出 0。",
         kind="argv", data=["97"], want="1"),
    dict(name="阶乘", desc="写一个程序：读入 N（命令行参数），输出 N 的阶乘。",
         kind="argv", data=["5"], want="120"),
    dict(name="幂", desc="写一个程序：读入底数和指数（两个命令行参数），输出底数的指数次幂。",
         kind="argv", data=["2", "10"], want="1024"),
    dict(name="各位数字和", desc="写一个程序：读入一个整数（命令行参数），输出它各位数字之和。",
         kind="argv", data=["1234"], want="10"),
    dict(name="三数最大", desc="写一个程序：读入三个整数（命令行参数），输出其中最大的一个。",
         kind="argv", data=["3", "9", "4"], want="9"),
]
# 第二轮新留出：这一组**没有**任务/配方（写进 harness 的时刻还没教），用来量"换一批会不会"
HELDOUT2 = [
    dict(name="整数位数", desc="写一个程序：读入一个整数（命令行参数），输出它的位数。",
         kind="argv", data=["12345"], want="5"),
    dict(name="反转整数", desc="写一个程序：读入一个整数（命令行参数），输出它反转后的整数。",
         kind="argv", data=["123"], want="321"),
    dict(name="偶数判断", desc="写一个程序：读入一个整数（命令行参数），是偶数输出 1，否则输出 0。",
         kind="argv", data=["4"], want="1"),
    dict(name="三数乘积", desc="写一个程序：读入三个整数（命令行参数），输出它们的乘积。",
         kind="argv", data=["2", "3", "4"], want="24"),
    dict(name="回文整数", desc="写一个程序：读入一个整数（命令行参数），是回文数输出 1，否则输出 0。",
         kind="argv", data=["121"], want="1"),
    dict(name="大写字母个数", desc="写一个程序：读入一行字符串（stdin），统计其中大写字母的个数并输出。",
         kind="stdin", data=["AbC\n"], want="2"),
    dict(name="数组和", desc="写一个程序：读入一行用空格分隔的整数（stdin），输出它们的和。",
         kind="stdin", data=["1 2 3\n"], want="6"),
    dict(name="二进制转十进制", desc="写一个程序：读入一个二进制字符串（命令行参数），输出对应的十进制数。",
         kind="argv", data=["1011"], want="11"),
]
# 第三轮新留出：继续不教，只量"配方覆盖之后，换一批还会不会"
HELDOUT3 = [
    dict(name="奇数判断", desc="写一个程序：读入一个整数（命令行参数），是奇数输出 1，否则输出 0。",
         kind="argv", data=["3"], want="1"),
    dict(name="整数平方", desc="写一个程序：读入一个整数（命令行参数），输出它的平方。",
         kind="argv", data=["7"], want="49"),
    dict(name="三个数的和", desc="写一个程序：读入三个整数（命令行参数），输出它们的和。",
         kind="argv", data=["1", "2", "3"], want="6"),
    dict(name="求绝对值", desc="写一个程序：读入一个整数（命令行参数），输出它的绝对值。",
         kind="argv", data=["-5"], want="5"),
    dict(name="小写字母个数", desc="写一个程序：读入一行字符串（stdin），统计其中小写字母的个数并输出。",
         kind="stdin", data=["AbC\n"], want="1"),
    dict(name="数组最大值", desc="写一个程序：读入一行用空格分隔的整数（stdin），输出其中的最大值。",
         kind="stdin", data=["3 9 4\n"], want="9"),
    dict(name="立方和", desc="写一个程序：读入 N（命令行参数），输出 1 到 N 的立方和。",
         kind="argv", data=["3"], want="36"),
    dict(name="字符串长度", desc="写一个程序：读入一行字符串（stdin），输出它的长度。",
         kind="stdin", data=["hello\n"], want="5"),
]
# 第四轮新留出：继续不教，量"配方覆盖 heldout3 之后，换一批还会不会"
HELDOUT4 = [
    dict(name="两数之和", desc="写一个程序：读入两个整数（命令行参数），输出它们的和。",
         kind="argv", data=["2", "3"], want="5"),
    dict(name="三数平均", desc="写一个程序：读入三个整数（命令行参数），输出它们的平均值（保留两位小数）。",
         kind="argv", data=["1", "2", "3"], want=r"2\.00"),
    dict(name="负数判断", desc="写一个程序：读入一个整数（命令行参数），是负数输出 1，否则输出 0。",
         kind="argv", data=["-5"], want="1"),
    dict(name="数组最小值", desc="写一个程序：读入一行用空格分隔的整数（stdin），输出其中的最小值。",
         kind="stdin", data=["3 9 4\n"], want="3"),
    dict(name="空格个数", desc="写一个程序：读入一行字符串（stdin），统计其中空格的个数并输出。",
         kind="stdin", data=["a b\n"], want="1"),
    dict(name="转大写", desc="写一个程序：读入一行字符串（stdin），把它转成大写后输出。",
         kind="stdin", data=["abc\n"], want="ABC"),
    dict(name="偶数和", desc="写一个程序：读入 N（命令行参数），输出 1 到 N 的偶数和。",
         kind="argv", data=["3"], want="2"),
    dict(name="单词个数", desc="写一个程序：读入一行字符串（stdin），统计其中单词的个数并输出。",
         kind="stdin", data=["a b c\n"], want="3"),
]
# 第五轮：**同样的操作、换说法**（heldout4 已教，这里量措辞泛化，不是新操作）
HELDOUT5 = [
    dict(name="两数相加", desc="写一个程序：读入两个数字，输出它们相加的结果。",
         kind="argv", data=["2", "3"], want="5"),
    dict(name="三数均值", desc="写一个程序：读入三个整数，输出它们的均值（保留两位小数）。",
         kind="argv", data=["1", "2", "3"], want=r"2\.00"),
    dict(name="小于零", desc="写一个程序：读入一个整数，判断它是不是小于零；是输出 1，否则 0。",
         kind="argv", data=["-5"], want="1"),
    dict(name="数组最小", desc="写一个程序：读入一行用空格分隔的整数，输出最小的数。",
         kind="stdin", data=["3 9 4\n"], want="3"),
    dict(name="空白个数", desc="写一个程序：读入一行字符串，数一数有多少个空白。",
         kind="stdin", data=["a b\n"], want="1"),
    dict(name="变成大写", desc="写一个程序：读入一行字符串，把它变成大写。",
         kind="stdin", data=["abc\n"], want="ABC"),
    dict(name="偶数之和", desc="写一个程序：读入 N，输出 1 到 N 之间所有偶数之和。",
         kind="argv", data=["3"], want="2"),
    dict(name="词的个数", desc="写一个程序：读入一行字符串，输出其中词的个数。",
         kind="stdin", data=["a b c\n"], want="3"),
]
# 第六轮：操作都教过，只把**输入通道**换成没教过的那个（契约推断的泛化读数）
HELDOUT6 = [
    dict(name="阶乘走标准输入", desc="写一个程序：读入 N（标准输入），输出 N 的阶乘。",
         kind="stdin", data=["5\n"], want="120"),
    dict(name="平方走标准输入", desc="写一个程序：读入一个整数（标准输入），输出它的平方。",
         kind="stdin", data=["7\n"], want="49"),
    dict(name="三数最大走标准输入", desc="写一个程序：读入三个整数（标准输入），输出其中最大的一个。",
         kind="stdin", data=["3 9 4\n"], want="9"),
    dict(name="绝对值走标准输入", desc="写一个程序：读入一个整数（标准输入），输出绝对值。",
         kind="stdin", data=["-5\n"], want="5"),
    dict(name="素数判断走标准输入", desc="写一个程序：读入一个整数（标准输入），是素数输出 1，否则输出 0。",
         kind="stdin", data=["97\n"], want="1"),
    dict(name="字符串长度走命令行", desc="写一个程序：读入一个字符串（命令行参数），输出它的长度。",
         kind="argv", data=["hello"], want="5"),
    dict(name="元音个数走命令行", desc="写一个程序：读入一行字符串（命令行参数），统计其中的元音字母个数。",
         kind="argv", data=["hello"], want="2"),
    dict(name="大写字母走命令行", desc="写一个程序：读入一行字符串（命令行参数），统计大写字母的个数。",
         kind="argv", data=["AbC"], want="2"),
]
HELDOUT7 = [
    dict(name="平方加一", desc="写一个程序：读入一个整数（命令行参数），输出它的平方再加一。",
         kind="argv", data=["7"], want="50"),
    dict(name="平方减一", desc="写一个程序：读入一个整数（命令行参数），输出它的平方再减一。",
         kind="argv", data=["7"], want="48"),
    dict(name="绝对值加一", desc="写一个程序：读入一个整数（命令行参数），输出它的绝对值再加一。",
         kind="argv", data=["-5"], want="6"),
    dict(name="加一平方", desc="写一个程序：读入一个整数（命令行参数），输出它加一之后的平方。",
         kind="argv", data=["3"], want="16"),
    dict(name="反转加一", desc="写一个程序：读入一个整数（命令行参数），输出它反转后再加一。",
         kind="argv", data=["123"], want="322"),
    dict(name="乘二减一", desc="写一个程序：读入一个整数（命令行参数），输出它乘二再减一。",
         kind="argv", data=["4"], want="7"),
    dict(name="平方乘二", desc="写一个程序：读入一个整数（命令行参数），输出它的平方再乘二。",
         kind="argv", data=["5"], want="50"),
    dict(name="平方加一走标准输入", desc="写一个程序：读入一个整数（标准输入），输出它的平方再加一。",
         kind="stdin", data=["7\n"], want="50"),
]
HELDOUT8 = [
    dict(name="两数和乘二", desc="写一个程序：读入两个整数（命令行参数），输出这两个整数的和再乘二。",
         kind="argv", data=["2", "3"], want="10"),
    dict(name="两数差加一", desc="写一个程序：读入两个整数（命令行参数），输出这两个整数的差再加一。",
         kind="argv", data=["5", "2"], want="4"),
    dict(name="两数积减一", desc="写一个程序：读入两个整数（命令行参数），输出这两个整数的积再减一。",
         kind="argv", data=["2", "3"], want="5"),
    dict(name="两数和平方", desc="写一个程序：读入两个整数（命令行参数），输出这两个整数的和再平方。",
         kind="argv", data=["1", "2"], want="9"),
    dict(name="三数和乘二", desc="写一个程序：读入三个整数（命令行参数），输出三个整数的和再乘二。",
         kind="argv", data=["1", "2", "3"], want="12"),
    dict(name="两数和走标准输入", desc="写一个程序：读入两个整数（标准输入），输出这两个整数的和再乘二。",
         kind="stdin", data=["2 3\n"], want="10"),
    dict(name="两数差绝对值", desc="写一个程序：读入两个整数（命令行参数），输出这两个整数的差的绝对值。",
         kind="argv", data=["2", "5"], want="3"),
    dict(name="两数积加一", desc="写一个程序：读入两个整数（命令行参数），输出这两个整数的积再加一。",
         kind="argv", data=["3", "4"], want="13"),
]
HELDOUT9 = [
    dict(name="数组最大加一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出其中最大的数再加一。",
         kind="stdin", data=["3 9 4\n"], want="10"),
    dict(name="数组最小加一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出其中最小的数再加一。",
         kind="stdin", data=["3 9 4\n"], want="4"),
    dict(name="数组最大减一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出其中最大的数再减一。",
         kind="stdin", data=["3 9 4\n"], want="8"),
    dict(name="数组最小乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出其中最小的数再乘二。",
         kind="stdin", data=["3 9 4\n"], want="6"),
    dict(name="数组和乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出数组的和再乘二。",
         kind="stdin", data=["1 2 3\n"], want="12"),
    dict(name="数组平方和加一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出每个数的平方和再加一。",
         kind="stdin", data=["1 2 3\n"], want="15"),
    dict(name="数组最大绝对值", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出其中最大的数的绝对值。",
         kind="stdin", data=["-3 -9 -4\n"], want="3"),
    dict(name="数组和减一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出数组的和再减一。",
         kind="stdin", data=["1 2 3\n"], want="5"),
]
HELDOUT10 = [
    dict(name="长度加一", desc="写一个程序：读入一行字符串（标准输入），输出它的长度再加一。",
         kind="stdin", data=["hello\n"], want="6"),
    dict(name="长度乘二", desc="写一个程序：读入一行字符串（标准输入），输出它的长度再乘二。",
         kind="stdin", data=["hello\n"], want="10"),
    dict(name="单词个数加一", desc="写一个程序：读入一行字符串（标准输入），输出其中词的个数再加一。",
         kind="stdin", data=["a b c\n"], want="4"),
    dict(name="空格个数乘二", desc="写一个程序：读入一行字符串（标准输入），数一数有多少个空格再乘二。",
         kind="stdin", data=["a b\n"], want="2"),
    dict(name="元音个数加一", desc="写一个程序：读入一行字符串（标准输入），统计其中的元音字母个数再加一。",
         kind="stdin", data=["hello\n"], want="3"),
    dict(name="大写个数加一", desc="写一个程序：读入一行字符串（标准输入），统计其中大写字母的个数再加一。",
         kind="stdin", data=["AbC\n"], want="3"),
    dict(name="小写个数乘二", desc="写一个程序：读入一行字符串（标准输入），统计其中小写字母的个数再乘二。",
         kind="stdin", data=["AbC\n"], want="2"),
    dict(name="元音个数走命令行", desc="写一个程序：读入一行字符串（命令行参数），统计其中的元音字母个数再加一。",
         kind="argv", data=["hello"], want="3"),
]
HELDOUT11 = [
    dict(name="转大写再翻转", desc="写一个程序：读入一行字符串（标准输入），把它转成大写再翻转输出。",
         kind="stdin", data=["abc\n"], want="CBA"),
    dict(name="翻转再转大写", desc="写一个程序：读入一行字符串（标准输入），先翻转再转成大写输出。",
         kind="stdin", data=["abc\n"], want="CBA"),
    dict(name="转大写再长度", desc="写一个程序：读入一行字符串（标准输入），把它转成大写再输出长度。",
         kind="stdin", data=["abc\n"], want="3"),
    dict(name="转小写再长度", desc="写一个程序：读入一行字符串（标准输入），把它转成小写再输出长度。",
         kind="stdin", data=["ABC\n"], want="3"),
    dict(name="翻转再长度", desc="写一个程序：读入一行字符串（标准输入），先翻转再输出长度。",
         kind="stdin", data=["hello\n"], want="5"),
    dict(name="大写翻转长度", desc="写一个程序：读入一行字符串（标准输入），转成大写再翻转再输出长度。",
         kind="stdin", data=["abc\n"], want="3"),
    dict(name="转小写走命令行", desc="写一个程序：读入一行字符串（命令行参数），把它转成小写再翻转输出。",
         kind="argv", data=["ABC"], want="cba"),
    dict(name="翻转再转小写", desc="写一个程序：读入一行字符串（标准输入），先翻转再转成小写输出。",
         kind="stdin", data=["ABC\n"], want="cba"),
]
HELDOUT12 = [
    dict(name="较大加一", desc="写一个程序：读入两个整数（命令行参数），输出其中较大的数再加一。",
         kind="argv", data=["2", "3"], want="4"),
    dict(name="较小乘二", desc="写一个程序：读入两个整数（命令行参数），输出其中较小的数再乘二。",
         kind="argv", data=["2", "3"], want="4"),
    dict(name="较大绝对值", desc="写一个程序：读入两个整数（命令行参数），输出其中较大的数的绝对值。",
         kind="argv", data=["-5", "-2"], want="2"),
    dict(name="较小绝对值", desc="写一个程序：读入两个整数（命令行参数），输出其中较小的数的绝对值。",
         kind="argv", data=["-5", "-2"], want="5"),
    dict(name="较大走标准输入", desc="写一个程序：读入两个整数（标准输入），输出其中较大的数再加一。",
         kind="stdin", data=["2 3\n"], want="4"),
    dict(name="较大平方减一", desc="写一个程序：读入两个整数（命令行参数），输出其中较大的数的平方再减一。",
         kind="argv", data=["4", "3"], want="15"),
    dict(name="较大是否大于零", desc="写一个程序：读入两个整数（命令行参数），输出其中较大的数是否大于零。",
         kind="argv", data=["2", "3"], want="1"),
    dict(name="较小绝对值大于二", desc="写一个程序：读入两个整数（命令行参数），输出其中较小的数的绝对值是否大于二。",
         kind="argv", data=["-5", "-2"], want="1"),
]
HELDOUT13 = [
    dict(name="每个数加一再求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再输出数组的和。",
         kind="stdin", data=["1 2 3\n"], want="9"),
    dict(name="每个数平方再求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再输出数组的和。",
         kind="stdin", data=["1 2 3\n"], want="14"),
    dict(name="每个数乘二再求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数乘二，再输出其中最大的数。",
         kind="stdin", data=["1 5 3\n"], want="10"),
    dict(name="每个数平方再求最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再输出其中最小的数。",
         kind="stdin", data=["1 2 3\n"], want="1"),
    dict(name="每个数加一再求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再输出其中最大的数。",
         kind="stdin", data=["1 5 3\n"], want="6"),
    dict(name="每个数乘二再求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数乘二，再输出数组的和。",
         kind="stdin", data=["1 2 3\n"], want="12"),
    dict(name="每个数平方求和减一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再输出数组的和再减一。",
         kind="stdin", data=["1 2 3\n"], want="13"),
    dict(name="每个数加一再求最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再输出其中最小的数。",
         kind="stdin", data=["1 5 3\n"], want="2"),
]
HELDOUT14 = [
    dict(name="平方逐行输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="1\n4\n9"),
    dict(name="加一逐行输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="2\n3\n4"),
    dict(name="乘二逐行输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数乘二，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="2\n4\n6"),
    dict(name="减一逐行输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数减一，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="0\n1\n2"),
    dict(name="平方加一逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再把每个数加一，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="2\n5\n10"),
    dict(name="加一乘二逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再把每个数乘二，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="4\n6\n8"),
    dict(name="平方再求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再输出其中最大的数。",
         kind="stdin", data=["1 2 3\n"], want="9"),
    dict(name="减一再求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数减一，再输出其中最大的数。",
         kind="stdin", data=["1 5 3\n"], want="4"),
]
HELDOUT15 = [
    dict(name="N首行求最大", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，输出其中最大的数。",
         kind="stdin", data=["5\n3 9 4 2 1\n"], want="9"),
    dict(name="N首行求和", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，输出数组的和。",
         kind="stdin", data=["5\n3 9 4 2 1\n"], want="19"),
    dict(name="N首行平方求最大", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，把每个数平方，再输出其中最大的数。",
         kind="stdin", data=["3\n1 2 3\n"], want="9"),
    dict(name="N首行加一逐行", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，把每个数加一，再逐行输出。",
         kind="stdin", data=["3\n1 2 3\n"], want="2\n3\n4"),
    dict(name="N首行求最小", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，输出其中最小的数。",
         kind="stdin", data=["4\n7 2 8 5\n"], want="2"),
    dict(name="N首行乘二求和", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，把每个数乘二，再输出数组的和。",
         kind="stdin", data=["3\n1 2 3\n"], want="12"),
    dict(name="N首行平方和", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，输出每个数的平方和。",
         kind="stdin", data=["3\n1 2 3\n"], want="14"),
    dict(name="N首行减一求最小", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，把每个数减一，再输出其中最小的数。",
         kind="stdin", data=["3\n1 2 3\n"], want="0"),
]
HELDOUT16 = [
    dict(name="最大最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出其中最大的数和最小的数。",
         kind="stdin", data=["3 9 4\n"], want="9 3"),
    dict(name="N首行最大最小", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，输出其中最大的数和最小的数。",
         kind="stdin", data=["5\n3 9 4 2 1\n"], want="9 1"),
    dict(name="平方后最大最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再输出其中最大的数和最小的数。",
         kind="stdin", data=["1 2 3\n"], want="9 1"),
    dict(name="加一后最大最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再输出其中最大的数和最小的数。",
         kind="stdin", data=["1 5 3\n"], want="6 2"),
    dict(name="乘二后最大最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数乘二，再输出其中最大的数和最小的数。",
         kind="stdin", data=["1 5 3\n"], want="10 2"),
    dict(name="和与积", desc="写一个程序：读入两个整数（命令行参数），输出它们的和与积。",
         kind="argv", data=["2", "3"], want="5 6"),
    dict(name="差与积", desc="写一个程序：读入两个整数（命令行参数），输出它们的差与积。",
         kind="argv", data=["5", "2"], want="3 10"),
    dict(name="和与积走标准输入", desc="写一个程序：读入两个整数（标准输入），输出它们的和与积。",
         kind="stdin", data=["2 3\n"], want="5 6"),
]
HELDOUT17 = [
    dict(name="矩阵求和", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出矩阵所有元素的和。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="21"),
    dict(name="矩阵最大", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出矩阵中最大的数。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="6"),
    dict(name="矩阵和乘二", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出矩阵所有元素的和再乘二。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="42"),
    dict(name="矩阵最大加一", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出矩阵中最大的数再加一。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="7"),
    dict(name="每行和逐行", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一行的和，再逐行输出。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="6\n15"),
    dict(name="每行和乘二逐行", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一行的和，再把每个数乘二，再逐行输出。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="12\n30"),
    dict(name="每行和求最大", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一行的和，再输出其中最大的数。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="15"),
    dict(name="每行和求和", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一行的和，再输出数组的和。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="21"),
]
HELDOUT18 = [
    dict(name="最大公约数加一", desc="写一个程序：读入两个整数（命令行参数），输出它们的最大公约数再加一。",
         kind="argv", data=["12", "18"], want="7"),
    dict(name="最小公倍数", desc="写一个程序：读入两个整数（命令行参数），输出它们的最小公倍数。",
         kind="argv", data=["4", "6"], want="12"),
    dict(name="最大公约数乘二", desc="写一个程序：读入两个整数（命令行参数），输出它们的最大公约数再乘二。",
         kind="argv", data=["12", "18"], want="12"),
    dict(name="最小公倍数加一", desc="写一个程序：读入两个整数（命令行参数），输出它们的最小公倍数再加一。",
         kind="argv", data=["4", "6"], want="13"),
    dict(name="是不是素数再加一", desc="写一个程序：读入一个整数（命令行参数），输出它是不是素数再加一。",
         kind="argv", data=["97"], want="2"),
    dict(name="是不是素数走标准输入", desc="写一个程序：读入一个整数（标准输入），输出它是不是素数再加一。",
         kind="stdin", data=["97\n"], want="2"),
    dict(name="最小公倍数走标准输入", desc="写一个程序：读入两个整数（标准输入），输出它们的最小公倍数。",
         kind="stdin", data=["4 6\n"], want="12"),
    dict(name="gcd和lcm", desc="写一个程序：读入两个整数（命令行参数），输出它们的最大公约数和最小公倍数。",
         kind="argv", data=["12", "18"], want="6 36"),
]
HELDOUT19 = [
    dict(name="文件行数", desc="写一个程序：读入一个文件名（命令行参数），统计文件的行数。",
         kind="file", data=["a\nb\nc\n"], want="3"),
    dict(name="文件单词数", desc="写一个程序：读入一个文件名（命令行参数），统计文件的单词数。",
         kind="file", data=["a b c\n"], want="3"),
    dict(name="文件字符数", desc="写一个程序：读入一个文件名（命令行参数），统计文件的字符数。",
         kind="file", data=["abc"], want="3"),
    dict(name="文件行数乘二", desc="写一个程序：读入一个文件名（命令行参数），统计文件的行数再乘二。",
         kind="file", data=["a\nb\nc\n"], want="6"),
    dict(name="文件单词数加一", desc="写一个程序：读入一个文件名（命令行参数），统计文件的单词数再加一。",
         kind="file", data=["a b c\n"], want="4"),
    dict(name="文件行数和单词数", desc="写一个程序：读入一个文件名（命令行参数），统计文件的行数和单词数。",
         kind="file", data=["a b\nc\n"], want="2 3"),
    dict(name="文件单词数乘二", desc="写一个程序：读入一个文件名（命令行参数），统计文件的单词数再乘二。",
         kind="file", data=["a b c\n"], want="6"),
    dict(name="文件行数减一", desc="写一个程序：读入一个文件名（命令行参数），统计文件的行数再减一。",
         kind="file", data=["a\nb\nc\n"], want="2"),
]
HELDOUT20 = [
    dict(name="递归阶乘加一", desc="写一个程序：读入 N（命令行参数），输出 N 的递归阶乘再加一。",
         kind="argv", data=["5"], want="121"),
    dict(name="递归斐波那契乘二", desc="写一个程序：读入 N（命令行参数），输出递归斐波那契的第 N 项再乘二。",
         kind="argv", data=["10"], want="110"),
    dict(name="递归求和减一", desc="写一个程序：读入 N（命令行参数），输出 1 到 N 的递归求和再减一。",
         kind="argv", data=["100"], want="5049"),
    dict(name="递归求和走标准输入", desc="写一个程序：读入 N（标准输入），输出 1 到 N 的递归求和再加一。",
         kind="stdin", data=["10\n"], want="56"),
    dict(name="数组翻转逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组翻转，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="3\n2\n1"),
    dict(name="数组翻转求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组翻转，再输出其中最大的数。",
         kind="stdin", data=["1 5 3\n"], want="5"),
    dict(name="数组翻转求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组翻转，再输出数组的和。",
         kind="stdin", data=["1 2 3\n"], want="6"),
    dict(name="数组翻转加一逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组翻转，再把每个数加一，再逐行输出。",
         kind="stdin", data=["1 2 3\n"], want="4\n3\n2"),
]
HELDOUT21 = [
    dict(name="数组排序求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再输出其中最大的数。",
         kind="stdin", data=["1 5 3\n"], want="5"),
    dict(name="数组排序逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再逐行输出。",
         kind="stdin", data=["3 1 2\n"], want="1\n2\n3"),
    dict(name="数组排序求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再输出数组的和。",
         kind="stdin", data=["3 1 2\n"], want="6"),
    dict(name="数组排序求最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再输出其中最小的数。",
         kind="stdin", data=["3 1 2\n"], want="1"),
    dict(name="数组排序加一逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再把每个数加一，再逐行输出。",
         kind="stdin", data=["3 1 2\n"], want="2\n3\n4"),
    dict(name="数组排序乘二逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再把每个数乘二，再逐行输出。",
         kind="stdin", data=["3 1 2\n"], want="2\n4\n6"),
    dict(name="数组排序减一求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再把每个数减一，再输出数组的和。",
         kind="stdin", data=["1 2 3\n"], want="3"),
    dict(name="数组排序最大减一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再输出其中最大的数再减一。",
         kind="stdin", data=["1 5 3\n"], want="4"),
]
HELDOUT22 = [
    dict(name="曼哈顿距离", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出曼哈顿距离。",
         kind="argv", data=["0", "0", "3", "4"], want="7"),
    dict(name="曼哈顿距离乘二", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出曼哈顿距离再乘二。",
         kind="argv", data=["0", "0", "3", "4"], want="14"),
    dict(name="距离平方", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出距离的平方。",
         kind="argv", data=["0", "0", "3", "4"], want="25"),
    dict(name="距离平方加一", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出距离的平方再加一。",
         kind="argv", data=["0", "0", "3", "4"], want="26"),
    dict(name="距离平方减一", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出距离的平方再减一。",
         kind="argv", data=["0", "0", "3", "4"], want="24"),
    dict(name="中点", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出它们的中点坐标。",
         kind="argv", data=["0", "0", "4", "6"], want="2 3"),
    dict(name="曼哈顿距离加一", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出曼哈顿距离再加一。",
         kind="argv", data=["0", "0", "3", "4"], want="8"),
    dict(name="距离平方乘二", desc="写一个程序：读入两个点的坐标（四个命令行参数），输出距离的平方再乘二。",
         kind="argv", data=["0", "0", "3", "4"], want="50"),
]
HELDOUT23 = [
    dict(name="矩阵加一输出", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵的每个数加一，再输出矩阵。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="2 3 4\n5 6 7"),
    dict(name="矩阵平方输出", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵的每个数平方，再输出矩阵。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="1 4\n9 16"),
    dict(name="矩阵乘二输出", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵的每个数乘二，再输出矩阵。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="2 4\n6 8"),
    dict(name="矩阵转置输出", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵转置，再输出矩阵。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="1 4\n2 5\n3 6"),
    dict(name="矩阵转置求和", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵转置，再输出矩阵所有元素的和。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="21"),
    dict(name="矩阵转置求最大", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵转置，再输出矩阵中最大的数。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="6"),
    dict(name="矩阵加一平方输出", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵的每个数加一，再把矩阵的每个数平方，再输出矩阵。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="4 9\n16 25"),
    dict(name="矩阵转置加一输出", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，把矩阵转置，再把矩阵的每个数加一，再输出矩阵。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="2 4\n3 5"),
]
HELDOUT24 = [
    dict(name="乘以三减一", desc="写一个程序：读入一个整数（命令行参数），输出它乘以 3 再减一。",
         kind="argv", data=["7"], want="20"),
    dict(name="乘以减一", desc="写一个程序：读入一个整数（命令行参数），输出它乘以 5 再减一。",
         kind="argv", data=["7"], want="34"),
    dict(name="加上乘二", desc="写一个程序：读入一个整数（命令行参数），输出它加上 10 再乘二。",
         kind="argv", data=["7"], want="34"),
    dict(name="减去加一", desc="写一个程序：读入一个整数（命令行参数），输出它减去 4 再加一。",
         kind="argv", data=["7"], want="4"),
    dict(name="除以加一", desc="写一个程序：读入一个整数（命令行参数），输出它除以 2 再加一。",
         kind="argv", data=["7"], want="4"),
    dict(name="乘以再平方", desc="写一个程序：读入一个整数（命令行参数），输出它乘以 2 再平方。",
         kind="argv", data=["7"], want="196"),
    dict(name="乘四加一走标准输入", desc="写一个程序：读入一个整数（标准输入），输出它乘以 4 再加一。",
         kind="stdin", data=["7\n"], want="29"),
    dict(name="加一百再减一", desc="写一个程序：读入一个整数（命令行参数），输出它加上 100 再减一。",
         kind="argv", data=["7"], want="106"),
]
HELDOUT25 = [
    dict(name="统计大于5", desc="写一个程序：读入一行用空格分隔的整数（标准输入），统计大于 5 的数的个数。",
         kind="stdin", data=["1 7 3 9 5\n"], want="2"),
    dict(name="统计大于0", desc="写一个程序：读入一行用空格分隔的整数（标准输入），统计大于 0 的数的个数。",
         kind="stdin", data=["-1 2 -3\n"], want="1"),
    dict(name="统计大于10再乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），统计大于 10 的数的个数再乘二。",
         kind="stdin", data=["1 20 30\n"], want="4"),
    dict(name="统计小于3", desc="写一个程序：读入一行用空格分隔的整数（标准输入），统计小于 3 的数的个数。",
         kind="stdin", data=["1 7 3 9\n"], want="1"),
    dict(name="求和大于5", desc="写一个程序：读入一行用空格分隔的整数（标准输入），求和大于 5 的数的和。",
         kind="stdin", data=["1 7 3 9\n"], want="16"),
    dict(name="求和小于5", desc="写一个程序：读入一行用空格分隔的整数（标准输入），求和小于 5 的数的和。",
         kind="stdin", data=["1 7 3\n"], want="4"),
    dict(name="加一后统计大于5", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再统计大于 5 的数的个数。",
         kind="stdin", data=["1 2 3\n"], want="0"),
    dict(name="求和大于0", desc="写一个程序：读入一行用空格分隔的整数（标准输入），求和大于 0 的数的和。",
         kind="stdin", data=["-5 3 7\n"], want="10"),
]
HELDOUT26 = [
    dict(name="数组平均", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出它们的平均数（保留两位小数）。",
         kind="stdin", data=["1 2 3\n"], want="2.00"),
    dict(name="数组平均不整", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出它们的平均数（保留两位小数）。",
         kind="stdin", data=["1 2 4\n"], want="2.33"),
    dict(name="数组平均负数", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出它们的平均数（保留两位小数）。",
         kind="stdin", data=["-1 0 1\n"], want="0.00"),
    dict(name="N首行平均", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，输出它们的平均数（保留两位小数）。",
         kind="stdin", data=["5\n3 9 4 2 1\n"], want="3.80"),
    dict(name="平方后平均", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再输出它们的平均数（保留两位小数）。",
         kind="stdin", data=["1 2 3\n"], want="4.67"),
    dict(name="加一后平均", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再输出它们的平均数（保留两位小数）。",
         kind="stdin", data=["1 2 3\n"], want="3.00"),
    dict(name="矩阵平均", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，求矩阵平均数（保留两位小数）。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="2.50"),
    dict(name="矩阵平均2x3", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，求矩阵平均数（保留两位小数）。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="3.50"),
]
HELDOUT27 = [
    dict(name="区间统计3到9", desc="写一个程序：读入一行用空格分隔的整数（标准输入），统计区间 3 到 9 的数的个数。",
         kind="stdin", data=["1 5 9 12\n"], want="2"),
    dict(name="区间统计0到0", desc="写一个程序：读入一行用空格分隔的整数（标准输入），统计区间 0 到 0 的数的个数。",
         kind="stdin", data=["-1 0 1\n"], want="1"),
    dict(name="区间统计2到5乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），统计区间 2 到 5 的数的个数再乘二。",
         kind="stdin", data=["1 3 4 7\n"], want="4"),
    dict(name="区间求和3到9", desc="写一个程序：读入一行用空格分隔的整数（标准输入），求和区间 3 到 9 的数的和。",
         kind="stdin", data=["1 5 9 12\n"], want="14"),
    dict(name="区间求和0到10", desc="写一个程序：读入一行用空格分隔的整数（标准输入），求和区间 0 到 10 的数的和。",
         kind="stdin", data=["-5 3 7 20\n"], want="10"),
    dict(name="加一后区间统计3到9", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再统计区间 3 到 9 的数的个数。",
         kind="stdin", data=["1 2 3\n"], want="2"),
    dict(name="区间求和2到4", desc="写一个程序：读入一行用空格分隔的整数（标准输入），求和区间 2 到 4 的数的和。",
         kind="stdin", data=["1 2 3 4\n"], want="9"),
    dict(name="区间求和5到15减一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），求和区间 5 到 15 的数的和再减一。",
         kind="stdin", data=["5 10 20\n"], want="14"),
]
HELDOUT28 = [
    dict(name="平方逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再逗号分隔输出。",
         kind="stdin", data=["1 2 3\n"], want="1,4,9"),
    dict(name="加一逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数加一，再逗号分隔输出。",
         kind="stdin", data=["1 2 3\n"], want="2,3,4"),
    dict(name="乘二逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数乘二，再逗号分隔输出。",
         kind="stdin", data=["1 2 3\n"], want="2,4,6"),
    dict(name="翻转逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组翻转，再逗号分隔输出。",
         kind="stdin", data=["1 2 3\n"], want="3,2,1"),
    dict(name="排序逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再逗号分隔输出。",
         kind="stdin", data=["3 1 2\n"], want="1,2,3"),
    dict(name="平方加一逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数平方，再把每个数加一，再逗号分隔输出。",
         kind="stdin", data=["1 2 3\n"], want="2,5,10"),
    dict(name="减一逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把每个数减一，再逗号分隔输出。",
         kind="stdin", data=["1 2 3\n"], want="0,1,2"),
    dict(name="排序加一逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），把数组排序，再把每个数加一，再逗号分隔输出。",
         kind="stdin", data=["3 1 2\n"], want="2,3,4"),
]
HELDOUT29 = [
    dict(name="空格换逗号", desc="写一个程序：读入一行字符串（标准输入），把空格换成逗号后输出。",
         kind="stdin", data=["a b\n"], want="a,b"),
    dict(name="空格换逗号再转大写", desc="写一个程序：读入一行字符串（标准输入），把空格换成逗号，再转成大写后输出。",
         kind="stdin", data=["a b\n"], want="A,B"),
    dict(name="去空格", desc="写一个程序：读入一行字符串（标准输入），去掉空格后输出。",
         kind="stdin", data=["a b c\n"], want="abc"),
    dict(name="去空格再求长度", desc="写一个程序：读入一行字符串（标准输入），去掉空格，再输出长度。",
         kind="stdin", data=["a b c\n"], want="3"),
    dict(name="空格换逗号再翻转", desc="写一个程序：读入一行字符串（标准输入），把空格换成逗号，再翻转后输出。",
         kind="stdin", data=["a b\n"], want="b,a"),
    dict(name="空格换逗号走命令行", desc="写一个程序：读入一行字符串（命令行参数），把空格换成逗号后输出。",
         kind="argv", data=["a b"], want="a,b"),
    dict(name="去空格再转大写", desc="写一个程序：读入一行字符串（标准输入），去掉空格，再转成大写后输出。",
         kind="stdin", data=["a b\n"], want="AB"),
    dict(name="空格换逗号再求长度", desc="写一个程序：读入一行字符串（标准输入），把空格换成逗号，再输出长度。",
         kind="stdin", data=["a b\n"], want="3"),
]
HELDOUT30 = [
    dict(name="从大到小输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），从大到小排列后输出。",
         kind="stdin", data=["3 1 2\n"], want="3 2 1"),
    dict(name="从大到小求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），从大到小排列，再输出其中最大的数。",
         kind="stdin", data=["3 1 2\n"], want="3"),
    dict(name="从大到小求最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），从大到小排列，再输出其中最小的数。",
         kind="stdin", data=["3 1 2\n"], want="1"),
    dict(name="从大到小求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），从大到小排列，再输出数组的和。",
         kind="stdin", data=["3 1 2\n"], want="6"),
    dict(name="从大到小逗号输出", desc="写一个程序：读入一行用空格分隔的整数（标准输入），从大到小排列，再逗号分隔输出。",
         kind="stdin", data=["3 1 2\n"], want="3,2,1"),
    dict(name="从大到小加一逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），从大到小排列，再把每个数加一，再逐行输出。",
         kind="stdin", data=["3 1 2\n"], want="4\n3\n2"),
    dict(name="从大到小平方", desc="写一个程序：读入一行用空格分隔的整数（标准输入），从大到小排列，再把每个数平方，再逐行输出。",
         kind="stdin", data=["3 1 2\n"], want="9\n4\n1"),
    dict(name="N首行从大到小", desc="写一个程序：读入整数（标准输入），第一行是 N（个数），第二行是 N 个整数，从大到小排列后输出。",
         kind="stdin", data=["5\n3 9 4 2 1\n"], want="9 4 3 2 1"),
]
HELDOUT31 = [
    dict(name="删掉数字", desc="写一个程序：读入一行字符串（标准输入），删掉数字后输出。",
         kind="stdin", data=["a1b2\n"], want="ab"),
    dict(name="删掉字母", desc="写一个程序：读入一行字符串（标准输入），删掉字母后输出。",
         kind="stdin", data=["a1b2\n"], want="12"),
    dict(name="只留数字", desc="写一个程序：读入一行字符串（标准输入），只留数字后输出。",
         kind="stdin", data=["a1b2\n"], want="12"),
    dict(name="删数字走命令行", desc="写一个程序：读入一行字符串（命令行参数），删掉数字后输出。",
         kind="argv", data=["a1b2"], want="ab"),
    dict(name="删掉数字再转大写", desc="写一个程序：读入一行字符串（标准输入），删掉数字，再转成大写后输出。",
         kind="stdin", data=["ab1\n"], want="AB"),
    dict(name="删掉字母再求长度", desc="写一个程序：读入一行字符串（标准输入），删掉字母，再输出长度。",
         kind="stdin", data=["a1b2\n"], want="2"),
    dict(name="只留数字再求长度", desc="写一个程序：读入一行字符串（标准输入），只留数字，再输出长度。",
         kind="stdin", data=["a1b2\n"], want="2"),
    dict(name="删掉数字再翻转", desc="写一个程序：读入一行字符串（标准输入），删掉数字，再翻转后输出。",
         kind="stdin", data=["ab1\n"], want="ba"),
]
HELDOUT32 = [
    dict(name="数组去重", desc="写一个程序：读入一行用空格分隔的整数（标准输入），数组去重后输出。",
         kind="stdin", data=["1 2 2 3\n"], want="1 2 3"),
    dict(name="去重再求和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），去掉重复的数，再输出数组的和。",
         kind="stdin", data=["1 2 2 3\n"], want="6"),
    dict(name="去重再求最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），去掉重复的数，再输出其中最大的数。",
         kind="stdin", data=["1 2 2 3\n"], want="3"),
    dict(name="不同值个数", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出不同值的个数。",
         kind="stdin", data=["1 2 2 3\n"], want="3"),
    dict(name="去重再排序", desc="写一个程序：读入一行用空格分隔的整数（标准输入），数组去重，再从大到小排列后输出。",
         kind="stdin", data=["3 1 3 2\n"], want="3 2 1"),
    dict(name="去重再翻转", desc="写一个程序：读入一行用空格分隔的整数（标准输入），数组去重，再翻转后输出。",
         kind="stdin", data=["1 2 2 3\n"], want="3 2 1"),
    dict(name="不同值个数乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出不同值的个数再乘二。",
         kind="stdin", data=["1 2 2 3\n"], want="6"),
    dict(name="去重再逐行", desc="写一个程序：读入一行用空格分隔的整数（标准输入），数组去重，再逐行输出。",
         kind="stdin", data=["1 2 2 3\n"], want="1\n2\n3"),
]
HELDOUT33 = [
    dict(name="每列和逐行", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再逐行输出。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="5\n7\n9"),
    dict(name="每列和求和", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再输出数组的和。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="21"),
    dict(name="每列和求最大", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再输出其中最大的数。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="9"),
    dict(name="每列和乘二逐行", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再把每个数乘二，再逐行输出。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="10\n14\n18"),
    dict(name="每列和加一", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再把每个数加一，再逐行输出。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="6\n8\n10"),
    dict(name="每列和降序", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再从大到小排列后输出。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="9 7 5"),
    dict(name="每列和求最小", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再输出其中最小的数。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="5"),
    dict(name="每列和翻转", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列的和，再翻转后输出。",
         kind="stdin", data=["2 3\n1 2 3\n4 5 6\n"], want="9 7 5"),
]
HELDOUT34 = [
    dict(name="区间元素和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素的和。",
         kind="stdin", data=["1 2 3 4 5\n"], want="9"),
    dict(name="区间元素和乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素的和再乘二。",
         kind="stdin", data=["1 2 3 4 5\n"], want="18"),
    dict(name="区间元素最大", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素中最大的。",
         kind="stdin", data=["1 2 3 4 5\n"], want="4"),
    dict(name="区间元素最小", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素中最小的。",
         kind="stdin", data=["1 2 3 4 5\n"], want="2"),
    dict(name="区间元素和加一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素的和再加一。",
         kind="stdin", data=["1 2 3 4 5\n"], want="10"),
    dict(name="区间元素最大减一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素中最大的再减一。",
         kind="stdin", data=["1 2 3 4 5\n"], want="3"),
    dict(name="区间元素最小乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素中最小的再乘二。",
         kind="stdin", data=["1 2 3 4 5\n"], want="4"),
    dict(name="区间元素和再减一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出第 2 到第 4 个元素的和再减一。",
         kind="stdin", data=["1 2 3 4 5\n"], want="8"),
]
HELDOUT35 = [
    dict(name="每列最大逐行", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列中最大的，再逐行输出。",
         kind="stdin", data=["2 3\n1 9 3\n4 5 6\n"], want="4\n9\n6"),
    dict(name="每列最大求和", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列中最大的，再输出数组的和。",
         kind="stdin", data=["2 3\n1 9 3\n4 5 6\n"], want="19"),
    dict(name="每列最大求最大", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列中最大的，再输出其中最大的数。",
         kind="stdin", data=["2 3\n1 9 3\n4 5 6\n"], want="9"),
    dict(name="每列最大乘二逐行", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列中最大的，再把每个数乘二，再逐行输出。",
         kind="stdin", data=["2 3\n1 9 3\n4 5 6\n"], want="8\n18\n12"),
    dict(name="数组众数", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出出现次数最多的数。",
         kind="stdin", data=["1 2 2 3\n"], want="2"),
    dict(name="众数乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出出现次数最多的数再乘二。",
         kind="stdin", data=["1 2 2 3\n"], want="4"),
    dict(name="众数加一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出出现次数最多的数再加一。",
         kind="stdin", data=["1 2 2 3\n"], want="3"),
    dict(name="每列最大翻转", desc="写一个程序：读入矩阵（标准输入），第一行是行数和列数，接着是矩阵元素，输出每一列中最大的，再翻转后输出。",
         kind="stdin", data=["2 3\n1 9 3\n4 5 6\n"], want="6 9 4"),
]
HELDOUT36 = [
    dict(name="argv数组最大", desc="写一个程序：读入若干整数（命令行参数），输出其中最大的数。",
         kind="argv", data=["3", "9", "4"], want="9"),
    dict(name="argv数组求和", desc="写一个程序：读入若干整数（命令行参数），输出数组的和。",
         kind="argv", data=["1", "2", "3"], want="6"),
    dict(name="argv数组降序", desc="写一个程序：读入若干整数（命令行参数），从大到小排列后输出。",
         kind="argv", data=["3", "1", "2"], want="3 2 1"),
    dict(name="argv数组去重", desc="写一个程序：读入若干整数（命令行参数），数组去重后输出。",
         kind="argv", data=["1", "2", "2", "3"], want="1 2 3"),
    dict(name="argv数组第1个", desc="写一个程序：读入若干整数（命令行参数），排序后输出第 1 个元素。",
         kind="argv", data=["3", "1", "2"], want="1"),
    dict(name="argv数组加一逐行", desc="写一个程序：读入若干整数（命令行参数），把每个数加一，再逐行输出。",
         kind="argv", data=["1", "2", "3"], want="2\n3\n4"),
    dict(name="argv数组平均", desc="写一个程序：读入若干整数（命令行参数），输出它们的平均数（保留两位小数）。",
         kind="argv", data=["1", "2", "3"], want="2.00"),
    dict(name="argv数组众数", desc="写一个程序：读入若干整数（命令行参数），输出出现次数最多的数。",
         kind="argv", data=["1", "2", "2", "3"], want="2"),
]
HELDOUT37 = [
    dict(name="大于5的个数", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出大于 5 的数的个数。",
         kind="stdin", data=["3 9 4\n"], want="1"),
    dict(name="大于5的个数乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出大于 5 的数的个数再乘二。",
         kind="stdin", data=["3 9 4\n"], want="2"),
    dict(name="小于3的个数", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出小于 3 的数的个数。",
         kind="stdin", data=["1 5 2\n"], want="2"),
    dict(name="等于2的个数", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出等于 2 的数的个数。",
         kind="stdin", data=["2 2 3\n"], want="2"),
    dict(name="大于5的数的和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出大于 5 的数的和。",
         kind="stdin", data=["3 9 4\n"], want="9"),
    dict(name="小于3的数的和", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出小于 3 的数的和。",
         kind="stdin", data=["1 5 2\n"], want="3"),
    dict(name="大于2的个数加一", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出大于 2 的数的个数再加一。",
         kind="stdin", data=["3 1 4\n"], want="3"),
    dict(name="等于1的个数乘二", desc="写一个程序：读入一行用空格分隔的整数（标准输入），输出等于 1 的数的个数再乘二。",
         kind="stdin", data=["1 2 1\n"], want="4"),
]
HELDOUT38 = [
    dict(name="区间整除和", desc="写一个程序：读入一行整数（标准输入），输出 1 到 100 里所有能被 7 整除的数的和。",
         kind="stdin", data=["1\n"], want="735"),
    dict(name="区间整除和乘二", desc="写一个程序：读入一行整数（标准输入），输出 1 到 50 里所有能被 5 整除的数的和再乘二。",
         kind="stdin", data=["1\n"], want="550"),
    dict(name="和与积", desc="写一个程序：读入三个整数（命令行参数），输出它们的和与积。",
         kind="argv", data=["1", "2", "3"], want="6 6"),
    dict(name="和与积含负数", desc="写一个程序：读入三个整数（命令行参数），输出它们的和与积。",
         kind="argv", data=["-1", "2", "3"], want="4 -6"),
    dict(name="去极值平均", desc="写一个程序：读入一行整数（标准输入），输出去掉最大值和最小值之后的平均。",
         kind="stdin", data=["1 2 3 4\n"], want="2.50"),
    dict(name="去极值平均乘二", desc="写一个程序：读入一行整数（标准输入），输出去掉最大值和最小值之后的平均再乘二。",
         kind="stdin", data=["1 2 3 4\n"], want="20"),
    dict(name="首字母大写", desc="写一个程序：读入一行字符串（标准输入），把每个单词首字母大写。",
         kind="stdin", data=["hello world\n"], want="Hello World"),
    dict(name="首字母大写再逆序", desc="写一个程序：读入一行字符串（标准输入），把每个单词首字母大写，再倒序输出。",
         kind="stdin", data=["ab cd\n"], want="dC bA"),
]

def run_synth(model, desc):
    for f in glob.glob(os.path.join(ROOT, "_t/work/synth_*.c")):
        try: os.remove(f)
        except OSError: pass
    inp = f"::act on\n::code synth {desc}\n::quit\n"
    p = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=inp.encode(), cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=180)
    out = p.stdout.decode("utf-8", "replace")
    files = sorted(glob.glob(os.path.join(ROOT, "_t/work/synth_*.c")),
                   key=os.path.getmtime)
    return (files[-1] if files else None), out

def clean_out(s):
    lines = []
    for ln in s.replace("\r", "").split("\n"):
        if re.match(r"^(ok|FAIL)\b", ln):
            continue
        lines.append(ln.rstrip())
    while lines and not lines[-1]:
        lines.pop()
    return "\n".join(lines)

def judge(spec, model):
    path, out = run_synth(model, spec["desc"])
    if not path:
        return "NO_MATCH", "没有生成 .c（多半是图上没有匹配的任务）", out
    exe = os.path.join(ROOT, "_t/work", "hidden_" + spec["name"])
    cc = subprocess.run(["gcc", "-std=c11", "-O2", "-o", exe, path, "-lm"],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
    if cc.returncode != 0:
        return "COMPILE_FAIL", cc.stdout.decode("utf-8", "replace")[-300:], out
    try:
        if spec["kind"] == "argv":
            p = subprocess.run([exe] + spec["data"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=10)
        elif spec["kind"] == "file":
            fpath = os.path.join(ROOT, "_t/work", "hidden_" + spec["name"] + ".txt")
            with open(fpath, "w", encoding="utf-8") as fh:
                fh.write(spec["data"][0])
            p = subprocess.run([exe, fpath], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=10)
        else:
            p = subprocess.run([exe], input=spec["data"][0].encode(),
                               stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
        got = clean_out(p.stdout.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return "TIMEOUT", "程序 10s 没退出", out
    want = spec["want"]
    ok = re.fullmatch(want, got) is not None
    return ("PASS" if ok else "WRONG"), got, out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.path.join(ROOT, "code/code_model.bin"))
    ap.add_argument("--only", choices=["battery", "heldout", "heldout2", "heldout3", "heldout4", "heldout5", "heldout6", "heldout7", "heldout8", "heldout9", "heldout10", "heldout11", "heldout12", "heldout13", "heldout14", "heldout15", "heldout16", "heldout17", "heldout18", "heldout19", "heldout20", "heldout21", "heldout22", "heldout23", "heldout24", "heldout25", "heldout26", "heldout27", "heldout28", "heldout29", "heldout30", "heldout31", "heldout32", "heldout33", "heldout34", "heldout35", "heldout36", "heldout37", "heldout38"], default=None)
    args = ap.parse_args()
    model = args.model if os.path.isabs(args.model) else os.path.join(ROOT, args.model)
    if not os.path.exists(model):
        print(f"FAIL: 缺模型 {model}（先跑 sh code/build_code_model.sh）")
        return 2
    if not os.path.exists(os.path.join(ROOT, "build_aip")):
        print("FAIL: 缺 build_aip")
        return 2
    os.makedirs(os.path.join(ROOT, "_t/work"), exist_ok=True)
    groups = []
    if args.only in (None, "battery"): groups.append(("battery", BATTERY))
    if args.only in (None, "heldout"): groups.append(("heldout", HELDOUT))
    if args.only in (None, "heldout2"): groups.append(("heldout2", HELDOUT2))
    if args.only in (None, "heldout3"): groups.append(("heldout3", HELDOUT3))
    if args.only in (None, "heldout4"): groups.append(("heldout4", HELDOUT4))
    if args.only in (None, "heldout5"): groups.append(("heldout5", HELDOUT5))
    if args.only in (None, "heldout6"): groups.append(("heldout6", HELDOUT6))
    if args.only in (None, "heldout7"): groups.append(("heldout7", HELDOUT7))
    if args.only in (None, "heldout8"): groups.append(("heldout8", HELDOUT8))
    if args.only in (None, "heldout9"): groups.append(("heldout9", HELDOUT9))
    if args.only in (None, "heldout10"): groups.append(("heldout10", HELDOUT10))
    if args.only in (None, "heldout11"): groups.append(("heldout11", HELDOUT11))
    if args.only in (None, "heldout12"): groups.append(("heldout12", HELDOUT12))
    if args.only in (None, "heldout13"): groups.append(("heldout13", HELDOUT13))
    if args.only in (None, "heldout14"): groups.append(("heldout14", HELDOUT14))
    if args.only in (None, "heldout15"): groups.append(("heldout15", HELDOUT15))
    if args.only in (None, "heldout16"): groups.append(("heldout16", HELDOUT16))
    if args.only in (None, "heldout17"): groups.append(("heldout17", HELDOUT17))
    if args.only in (None, "heldout18"): groups.append(("heldout18", HELDOUT18))
    if args.only in (None, "heldout19"): groups.append(("heldout19", HELDOUT19))
    if args.only in (None, "heldout20"): groups.append(("heldout20", HELDOUT20))
    if args.only in (None, "heldout21"): groups.append(("heldout21", HELDOUT21))
    if args.only in (None, "heldout22"): groups.append(("heldout22", HELDOUT22))
    if args.only in (None, "heldout23"): groups.append(("heldout23", HELDOUT23))
    if args.only in (None, "heldout24"): groups.append(("heldout24", HELDOUT24))
    if args.only in (None, "heldout25"): groups.append(("heldout25", HELDOUT25))
    if args.only in (None, "heldout26"): groups.append(("heldout26", HELDOUT26))
    if args.only in (None, "heldout27"): groups.append(("heldout27", HELDOUT27))
    if args.only in (None, "heldout28"): groups.append(("heldout28", HELDOUT28))
    if args.only in (None, "heldout29"): groups.append(("heldout29", HELDOUT29))
    if args.only in (None, "heldout30"): groups.append(("heldout30", HELDOUT30))
    if args.only in (None, "heldout31"): groups.append(("heldout31", HELDOUT31))
    if args.only in (None, "heldout32"): groups.append(("heldout32", HELDOUT32))
    if args.only in (None, "heldout33"): groups.append(("heldout33", HELDOUT33))
    if args.only in (None, "heldout34"): groups.append(("heldout34", HELDOUT34))
    if args.only in (None, "heldout35"): groups.append(("heldout35", HELDOUT35))
    if args.only in (None, "heldout36"): groups.append(("heldout36", HELDOUT36))
    if args.only in (None, "heldout37"): groups.append(("heldout37", HELDOUT37))
    if args.only in (None, "heldout38"): groups.append(("heldout38", HELDOUT38))
    battery_ok = True
    for gname, tasks in groups:
        print(f"== {gname} ==")
        npass = 0
        for t in tasks:
            status, got, out = judge(t, model)
            if status == "PASS":
                npass += 1
                print(f"  [PASS] {t['name']}")
            else:
                print(f"  [{status}] {t['name']}")
                print(f"         描述: {t['desc']}")
                print(f"         得到: {got!r}")
                print(f"         期望: {t['want']!r}")
                line = next((l for l in out.splitlines() if "[合成]" in l), "")
                if line: print(f"         合成: {line.strip()[:160]}")
        print(f"  => {npass} / {len(tasks)}")
        if gname == "battery":
            battery_ok = (npass == len(tasks))
    return 0 if battery_ok else 1

if __name__ == "__main__":
    sys.exit(main())
