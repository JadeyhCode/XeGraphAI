#!/usr/bin/env python3
"""tools/code_blind20.py — 第十组“没教过”的题（blind11–19 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND20 = [
    dict(name="数组逆序输出", desc="写一个程序：读入一行整数（标准输入），倒序输出这些数。",
         kind="stdin", data=["1 2 3\n"], want="3 2 1"),
    dict(name="数组右移一位", desc="写一个程序：读入一行整数（标准输入），整体右移一位后输出。",
         kind="stdin", data=["1 2 3\n"], want="3 1 2"),
    dict(name="单词个数", desc="写一个程序：读入一行字符串（标准输入），输出单词的个数。",
         kind="stdin", data=["a b c\n"], want="3"),
    dict(name="和的位数", desc="写一个程序：读入一行整数（标准输入），输出所有元素之和的位数。",
         kind="stdin", data=["1 2 3\n"], want="1"),
    dict(name="最大值与第二大", desc="写一个程序：读入一行整数（标准输入），输出最大值与第二大值之和。",
         kind="stdin", data=["5 9 1 7\n"], want="16"),
    dict(name="数组对称", desc="写一个程序：读入一行整数（标准输入），判断数组是否对称（输出 yes 或 no）。",
         kind="stdin", data=["1 2 1\n"], want="yes"),
    dict(name="前缀和", desc="写一个程序：读入一行整数（标准输入），输出前缀和序列。",
         kind="stdin", data=["1 2 3\n"], want="1 3 6"),
    dict(name="平均取整", desc="写一个程序：读入一行整数（标准输入），输出平均值的四舍五入整数。",
         kind="stdin", data=["1 2 4\n"], want="2"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND20:
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
