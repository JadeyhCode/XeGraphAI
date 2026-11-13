#!/usr/bin/env python3
"""tools/code_blind14.py — 第四组“没教过”的题（blind11/12/13 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND14 = [
    dict(name="二进制1的个数", desc="写一个程序：读入一个整数（命令行参数），输出它二进制里 1 的个数。",
         kind="argv", data=["7"], want="3"),
    dict(name="各位数字之和", desc="写一个程序：读入一个整数（命令行参数），输出它各位数字之和。",
         kind="argv", data=["1234"], want="10"),
    dict(name="是否升序", desc="写一个程序：读入一行整数（标准输入），判断是否已经从小到大有序（输出 yes 或 no）。",
         kind="stdin", data=["1 2 3\n"], want="yes"),
    dict(name="数组平方和", desc="写一个程序：读入一行整数（标准输入），输出它们平方的和。",
         kind="stdin", data=["1 2 3\n"], want="14"),
    dict(name="转小写", desc="写一个程序：读入一行字符串（标准输入），把大写字母都改成小写后输出。",
         kind="stdin", data=["AbC\n"], want="abc"),
    dict(name="相邻最大差", desc="写一个程序：读入一行整数（标准输入），输出相邻两个数之差的最大值。",
         kind="stdin", data=["1 5 3\n"], want="4"),
    dict(name="是否2的幂", desc="写一个程序：读入一个整数（命令行参数），判断它是不是 2 的幂（输出 yes 或 no）。",
         kind="argv", data=["8"], want="yes"),
    dict(name="首尾相同", desc="写一个程序：读入一行字符串（标准输入），判断首尾字符是否相同（输出 yes 或 no）。",
         kind="stdin", data=["aba\n"], want="yes"),
]

def main():
    model = os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND14:
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
