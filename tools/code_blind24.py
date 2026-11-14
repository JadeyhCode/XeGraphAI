#!/usr/bin/env python3
"""tools/code_blind24.py — 第十四组“没教过”的题：**全部在已学族之外**。

目的：给“泛化到族外”一个读数（blind23 是族内换参数，6/8；这一组应显著更低）。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND24 = [
    dict(name="罗马转整数", desc="写一个程序：读入一个罗马数字字符串（标准输入），输出对应的整数。",
         kind="stdin", data=["XIV\n"], want="14"),
    dict(name="旋转后最大子段和", desc="写一个程序：读入一行整数（标准输入），把数组循环右移一位后再求最大子段和。",
         kind="stdin", data=["1 -2 3 4\n"], want="8"),
    dict(name="方差整数", desc="写一个程序：读入一行整数（标准输入），输出它们的方差（整数部分，向下取整）。",
         kind="stdin", data=["1 2 3\n"], want="0"),
    dict(name="首字母大写", desc="写一个程序：读入一行英文（标准输入），把每个单词的首字母改成大写后输出。",
         kind="stdin", data=["hello world\n"], want="Hello World"),
    dict(name="最大质因数", desc="写一个程序：读入一个整数（命令行参数），输出它最大的质因数。",
         kind="argv", data=["28"], want="7"),
    dict(name="矩阵转置对角线和", desc="写一个程序：读入矩阵（标准输入），输出转置后主对角线元素之和。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="5"),
    dict(name="字符串去空格", desc="写一个程序：读入一行字符串（标准输入），去掉所有空格后输出。",
         kind="stdin", data=["a b  c\n"], want="abc"),
    dict(name="鸡兔同笼", desc="写一个程序：读入头数和脚数（命令行参数），输出鸡的只数。",
         kind="argv", data=["5", "14"], want="3"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND24:
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
