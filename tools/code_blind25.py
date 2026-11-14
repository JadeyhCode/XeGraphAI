#!/usr/bin/env python3
"""tools/code_blind25.py — 第十五组“没教过”的题（含参数化边界与全新题）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND25 = [
    dict(name="第七小", desc="写一个程序：读入一行整数（标准输入），输出第七小的数。",
         kind="stdin", data=["9 1 5 3 7 2 8 4\n"], want="8"),
    dict(name="乘以十二", desc="写一个程序：读入一行整数（标准输入），输出每个数乘以十二。",
         kind="stdin", data=["1 2\n"], want="12 24"),
    dict(name="转十二进制", desc="写一个程序：读入一个整数（命令行参数），输出它的十二进制表示。",
         kind="argv", data=["14"], want="12"),
    dict(name="右移十一位", desc="写一个程序：读入一行整数（标准输入），整体右移十一位后输出。",
         kind="stdin", data=["1 2 3 4 5 6 7 8 9 10 11 12\n"], want="2 3 4 5 6 7 8 9 10 11 12 1"),
    dict(name="数组最大公约数", desc="写一个程序：读入一行整数（标准输入），输出它们的最大公约数。",
         kind="stdin", data=["12 18 24\n"], want="6"),
    dict(name="字符串全大写", desc="写一个程序：读入一行字符串（标准输入），判断是否全部是大写字母（输出 yes 或 no）。",
         kind="stdin", data=["ABC\n"], want="yes"),
    dict(name="乘积是否偶数", desc="写一个程序：读入一行整数（标准输入），判断它们的乘积是否为偶数（输出 yes 或 no）。",
         kind="stdin", data=["1 3 4\n"], want="yes"),
    dict(name="数组中位数", desc="写一个程序：读入一行整数（标准输入），输出它们的中位数（向下取整）。",
         kind="stdin", data=["1 2 3 4\n"], want="2"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND25:
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
