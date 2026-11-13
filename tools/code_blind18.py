#!/usr/bin/env python3
"""tools/code_blind18.py — 第八组“没教过”的题（blind11–17 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND18 = [
    dict(name="转大写", desc="写一个程序：读入一行字符串（标准输入），把小写字母都改成大写后输出。",
         kind="stdin", data=["aBc\n"], want="ABC"),
    dict(name="负数之和", desc="写一个程序：读入一行整数（标准输入），输出所有负数的和。",
         kind="stdin", data=["-1 2 -3\n"], want="-4"),
    dict(name="奇数个数", desc="写一个程序：读入一行整数（标准输入），输出奇数的个数。",
         kind="stdin", data=["1 2 3\n"], want="2"),
    dict(name="最大值位置差", desc="写一个程序：读入一行整数（标准输入），输出最大值与最小值位置之间的距离。",
         kind="stdin", data=["5 1 9\n"], want="2"),
    dict(name="字符串反转再大写", desc="写一个程序：读入一行字符串（标准输入），倒过来并把字母都变成大写。",
         kind="stdin", data=["ab\n"], want="BA"),
    dict(name="数组负数和", desc="写一个程序：读入一行整数（标准输入），输出负数元素的和。",
         kind="stdin", data=["-2 5 -3\n"], want="-5"),
    dict(name="整数之和", desc="写一个程序：读入两个整数（命令行参数），输出它们的和。",
         kind="argv", data=["2", "3"], want="5"),
    dict(name="字符变数字", desc="写一个程序：读入一行字符串（标准输入），输出数字字符之和。",
         kind="stdin", data=["a1b2\n"], want="3"),
]

def main():
    model = os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND18:
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
