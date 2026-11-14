#!/usr/bin/env python3
"""tools/code_blind23.py — 第十三组“没教过”的题：**全部落在已归纳的族里、但参数是新的**。

用来回答：概念泛化（第k小/大、乘以k、长度加k、右移k、任意进制）到底能覆盖多远。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND23 = [
    dict(name="第四小", desc="写一个程序：读入一行整数（标准输入），输出第四小的数。",
         kind="stdin", data=["9 1 5 3 7 2\n"], want="5"),
    dict(name="第六大", desc="写一个程序：读入一行整数（标准输入），输出第六大的数。",
         kind="stdin", data=["9 1 5 3 7 2\n"], want="1"),
    dict(name="乘以九", desc="写一个程序：读入一行整数（标准输入），输出每个数乘以九。",
         kind="stdin", data=["1 2 3\n"], want="9 18 27"),
    dict(name="加七", desc="写一个程序：读入一行整数（标准输入），输出每个数加上七。",
         kind="stdin", data=["1 2 3\n"], want="8 9 10"),
    dict(name="长度加七", desc="写一个程序：读入一行字符串（标准输入），输出长度加七。",
         kind="stdin", data=["abc\n"], want="10"),
    dict(name="右移五位", desc="写一个程序：读入一行整数（标准输入），整体右移五位后输出。",
         kind="stdin", data=["1 2 3 4 5 6 7\n"], want="3 4 5 6 7 1 2"),
    dict(name="转七进制", desc="写一个程序：读入一个整数（命令行参数），输出它的七进制表示。",
         kind="argv", data=["50"], want="101"),
    dict(name="按位异或", desc="写一个程序：读入两个整数（命令行参数），输出它们的按位异或。",
         kind="argv", data=["6", "3"], want="5"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND23:
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
