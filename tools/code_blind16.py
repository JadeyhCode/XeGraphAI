#!/usr/bin/env python3
"""tools/code_blind16.py — 第六组“没教过”的题（blind11–15 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND16 = [
    dict(name="元素乘积", desc="写一个程序：读入一行整数（标准输入），输出所有元素的乘积。",
         kind="stdin", data=["2 3 4\n"], want="24"),
    dict(name="大写字母个数", desc="写一个程序：读入一行字符串（标准输入），输出大写字母的个数。",
         kind="stdin", data=["aBcD\n"], want="2"),
    dict(name="最大值下标", desc="写一个程序：读入一行整数（标准输入），输出最大值的下标（从 0 开始）。",
         kind="stdin", data=["5 9 3\n"], want="1"),
    dict(name="闰年判断", desc="写一个程序：读入一个年份（命令行参数），判断是不是闰年（输出 yes 或 no）。",
         kind="argv", data=["2024"], want="yes"),
    dict(name="相邻和最大", desc="写一个程序：读入一行整数（标准输入），输出相邻两个数之和的最大值。",
         kind="stdin", data=["1 5 3\n"], want="8"),
    dict(name="数组有重复", desc="写一个程序：读入一行整数（标准输入），判断是否有重复的数（输出 yes 或 no）。",
         kind="stdin", data=["1 2 2\n"], want="yes"),
    dict(name="偶数个数", desc="写一个程序：读入一行整数（标准输入），输出偶数的个数。",
         kind="stdin", data=["1 2 4\n"], want="2"),
    dict(name="数组和再平方", desc="写一个程序：读入一行整数（标准输入），输出所有元素和的平方。",
         kind="stdin", data=["1 2 3\n"], want="36"),
]

def main():
    model = os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND16:
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
