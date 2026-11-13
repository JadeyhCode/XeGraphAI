#!/usr/bin/env python3
"""tools/code_blind15.py — 第五组“没教过”的题（blind11–14 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND15 = [
    dict(name="最小值下标", desc="写一个程序：读入一行整数（标准输入），输出最小值的下标（从 0 开始）。",
         kind="stdin", data=["5 3 9\n"], want="1"),
    dict(name="负数个数", desc="写一个程序：读入一行整数（标准输入），输出负数的个数。",
         kind="stdin", data=["-1 2 -3\n"], want="2"),
    dict(name="空格个数", desc="写一个程序：读入一行字符串（标准输入），输出空格的个数。",
         kind="stdin", data=["a b c\n"], want="2"),
    dict(name="阶乘", desc="写一个程序：读入一个整数（命令行参数），输出它的阶乘。",
         kind="argv", data=["5"], want="120"),
    dict(name="数字字符个数", desc="写一个程序：读入一行字符串（标准输入），输出其中数字字符的个数。",
         kind="stdin", data=["a1b2\n"], want="2"),
    dict(name="交替和", desc="写一个程序：读入一行整数（标准输入），输出交替和（第一个减第二个加第三个……）。",
         kind="stdin", data=["1 2 3\n"], want="2"),
    dict(name="极差", desc="写一个程序：读入一行整数（标准输入），输出最大值与最小值之差。",
         kind="stdin", data=["4 1 9\n"], want="8"),
    dict(name="数组包含5", desc="写一个程序：读入一行整数（标准输入），判断里面有没有 5（输出 yes 或 no）。",
         kind="stdin", data=["1 5 2\n"], want="yes"),
]

def main():
    model = os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND15:
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
