#!/usr/bin/env python3
"""tools/code_blind12.py — 第二组“没教过”的经典题（blind11 已被喂饱，需要新的量尺）。

blind11（12 道）在 2026-11-13 已全部答对 —— 它不再是盲测。这组 8 道是**新**的、
图里没有专门知识的题，用来量“真·泛化”：答对算惊喜，拒绝是如实，答错要记下来。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND12 = [
    dict(name="快速排序", desc="写一个程序：读入一行整数（标准输入），用快速排序从小到大排好输出。",
         kind="stdin", data=["3 1 2\n"], want="1 2 3"),
    dict(name="最大子段和", desc="写一个程序：读入一行整数（标准输入），输出最大的连续子段和。",
         kind="stdin", data=["-2 1 -3 4 -1 2 1 -5 4\n"], want="6"),
    dict(name="筛法素数个数", desc="写一个程序：读入一个整数（命令行参数），输出 1 到它之间素数的个数。",
         kind="argv", data=["30"], want="10"),
    dict(name="字符串去重", desc="写一个程序：读入一行字符串（标准输入），去掉重复字符（保持第一次出现的顺序）后输出。",
         kind="stdin", data=["abac\n"], want="abc"),
    dict(name="任意进制转换", desc="写一个程序：读入一个整数（命令行参数），输出它的七进制表示。",
         kind="argv", data=["100"], want="202"),
    dict(name="归并排序", desc="写一个程序：读入一行整数（标准输入），用归并排序从小到大排好输出。",
         kind="stdin", data=["4 2 9 1\n"], want="1 2 4 9"),
    dict(name="逆序对个数", desc="写一个程序：读入一行整数（标准输入），输出逆序对的个数。",
         kind="stdin", data=["2 1 4 3\n"], want="2"),
    dict(name="字符串最长单词", desc="写一个程序：读入一行字符串（标准输入），输出最长单词的长度。",
         kind="stdin", data=["ab cde f\n"], want="3"),
]

def main():
    model = os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND12:
        st, got, out = judge(t, model)
        tag = {"PASS": "PASS", "REFUSE": "REFUSE", "NO_MATCH": "REFUSE"}.get(st, st)
        print(f"  [{tag}] {t['name']}")
        if st == "PASS": p += 1
        elif st in ("NO_MATCH", "REFUSE"): r += 1
        else:
            w += 1
            print(f"         得到: {got!r}  期望: {t['want']!r}")
    print(f"  => pass {p} / refuse {r} / wrong {w}")
    return 0

if __name__ == "__main__":
    sys.exit(main())
