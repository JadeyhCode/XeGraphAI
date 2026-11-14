#!/usr/bin/env python3
"""tools/code_blind22.py — 第十二组“没教过”的题（**同族不同说法**，用来量迁移）。

设计：任务族与 blind21 大体重叠（移位/位运算/包含/次小/进制/平方根/峰值/长度），
但**说法和参数都换掉**。这样“训练 blind21 后跑 blind22”能直接回答：
学的是“这道题”还是“这一类”。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND22 = [
    dict(name="数组右移三位", desc="写一个程序：读入一行整数（标准输入），整体右移三位后输出。",
         kind="stdin", data=["1 2 3 4 5\n"], want="3 4 5 1 2"),
    dict(name="按位或", desc="写一个程序：读入两个整数（命令行参数），输出它们的按位或。",
         kind="argv", data=["6", "3"], want="7"),
    dict(name="字符串含字母z", desc="写一个程序：读入一行字符串（标准输入），判断是否含有字母 z（输出 yes 或 no）。",
         kind="stdin", data=["zoo\n"], want="yes"),
    dict(name="数组第三小", desc="写一个程序：读入一行整数（标准输入），输出第三小的数。",
         kind="stdin", data=["5 1 3 2\n"], want="3"),
    dict(name="十进制转十六进制", desc="写一个程序：读入一个整数（命令行参数），输出它的十六进制表示（小写）。",
         kind="argv", data=["31"], want="1f"),
    dict(name="立方根之和", desc="写一个程序：读入一行整数（标准输入），输出它们立方根整数部分之和。",
         kind="stdin", data=["8 27\n"], want="5"),
    dict(name="数组是否单调", desc="写一个程序：读入一行整数（标准输入），判断是否单调不减（输出 yes 或 no）。",
         kind="stdin", data=["1 2 2 5\n"], want="yes"),
    dict(name="字符串长度加十", desc="写一个程序：读入一行字符串（标准输入），输出长度加十。",
         kind="stdin", data=["abc\n"], want="13"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND22:
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
