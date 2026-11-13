#!/usr/bin/env python3
"""tools/code_blind21.py — 第十一组“没教过”的题（blind11–20 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND21 = [
    dict(name="数组左移两位", desc="写一个程序：读入一行整数（标准输入），整体左移两位后输出。",
         kind="stdin", data=["1 2 3 4\n"], want="3 4 1 2"),
    dict(name="按位与", desc="写一个程序：读入两个整数（命令行参数），输出它们的按位与。",
         kind="argv", data=["6", "3"], want="2"),
    dict(name="字符串包含字母a", desc="写一个程序：读入一行字符串（标准输入），判断是否包含字母 a（输出 yes 或 no）。",
         kind="stdin", data=["cat\n"], want="yes"),
    dict(name="数组次小值", desc="写一个程序：读入一行整数（标准输入），输出第二小的数。",
         kind="stdin", data=["5 1 3\n"], want="3"),
    dict(name="十进制转八进制", desc="写一个程序：读入一个整数（命令行参数），输出它的八进制表示。",
         kind="argv", data=["8"], want="10"),
    dict(name="平方根之和", desc="写一个程序：读入一行整数（标准输入），输出它们整数平方根之和。",
         kind="stdin", data=["9 16\n"], want="7"),
    dict(name="数组峰值", desc="写一个程序：读入一行整数（标准输入），输出是否只有一个峰值（输出 yes 或 no）。",
         kind="stdin", data=["1 3 2\n"], want="yes"),
    dict(name="字符串长度乘二", desc="写一个程序：读入一行字符串（标准输入），输出长度的两倍。",
         kind="stdin", data=["abc\n"], want="6"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND21:
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
