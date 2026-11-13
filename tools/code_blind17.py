#!/usr/bin/env python3
"""tools/code_blind17.py — 第七组“没教过”的题（blind11–16 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND17 = [
    dict(name="极值之和", desc="写一个程序：读入一行整数（标准输入），输出最大值与最小值之和。",
         kind="stdin", data=["1 5 3\n"], want="6"),
    dict(name="字母个数", desc="写一个程序：读入一行字符串（标准输入），输出字母的个数。",
         kind="stdin", data=["a1B!\n"], want="2"),
    dict(name="全为正数", desc="写一个程序：读入一行整数（标准输入），判断是不是全为正数（输出 yes 或 no）。",
         kind="stdin", data=["1 2 3\n"], want="yes"),
    dict(name="去空格", desc="写一个程序：读入一行字符串（标准输入），去掉所有空格后输出。",
         kind="stdin", data=["a b c\n"], want="abc"),
    dict(name="平方数个数", desc="写一个程序：读入一行整数（标准输入），输出其中完全平方数的个数。",
         kind="stdin", data=["1 2 4\n"], want="2"),
    dict(name="全是数字", desc="写一个程序：读入一行字符串（标准输入），判断是不是全是数字（输出 yes 或 no）。",
         kind="stdin", data=["123\n"], want="yes"),
    dict(name="数组长度", desc="写一个程序：读入一行整数（标准输入），输出一共有多少个数。",
         kind="stdin", data=["1 2 3 4\n"], want="4"),
    dict(name="两倍和", desc="写一个程序：读入一行整数（标准输入），输出所有元素和的两倍。",
         kind="stdin", data=["1 2 3\n"], want="12"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND17:
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
