#!/usr/bin/env python3
"""tools/code_blind19.py — 第九组“没教过”的题（blind11–18 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND19 = [
    dict(name="都相等", desc="写一个程序：读入一行整数（标准输入），判断是否全部相等（输出 yes 或 no）。",
         kind="stdin", data=["2 2 2\n"], want="yes"),
    dict(name="最后一个字符", desc="写一个程序：读入一行字符串（标准输入），输出最后一个字符。",
         kind="stdin", data=["abc\n"], want="c"),
    dict(name="第三大", desc="写一个程序：读入一行整数（标准输入），输出第三大的数。",
         kind="stdin", data=["5 9 1 7\n"], want="5"),
    dict(name="偶数之和", desc="写一个程序：读入一个整数 N（命令行参数），输出 1 到 N 之间所有偶数的和。",
         kind="argv", data=["10"], want="30"),
    dict(name="每个乘二", desc="写一个程序：读入一行整数（标准输入），把每个元素乘以 2 后输出。",
         kind="stdin", data=["1 2 3\n"], want="2 4 6"),
    dict(name="相邻差", desc="写一个程序：读入一行整数（标准输入），输出相邻元素之差。",
         kind="stdin", data=["1 4 9\n"], want="3 5"),
    dict(name="包含偶数", desc="写一个程序：读入一行整数（标准输入），判断里面有没有偶数（输出 yes 或 no）。",
         kind="stdin", data=["1 3 4\n"], want="yes"),
    dict(name="有几个7", desc="写一个程序：读入一个整数（命令行参数），输出它里面有几个数字 7。",
         kind="argv", data=["7707"], want="2"),
]

def main():
    model = os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND19:
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
