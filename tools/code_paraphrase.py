#!/usr/bin/env python3
"""tools/code_paraphrase.py — **迁移尺**：任务不变、说法换掉（不教新知识）。

为什么要有：blind 系列量“新题会不会”，这里量“会的题换个说法还认不认得”。
这才是“迁移”的直接指标：知识是同一批，只有表面措辞变了。
"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

PARA = [
    dict(name="最大换个说法", desc="写一个程序：读入一行整数（标准输入），把最大的那个数找出来。",
         kind="stdin", data=["5 9 1\n"], want="9"),
    dict(name="最小换个说法", desc="写一个程序：读入一行整数（标准输入），找出其中最小的数。",
         kind="stdin", data=["5 9 1\n"], want="1"),
    dict(name="求和换个说法", desc="写一个程序：读入一行整数（标准输入），把它们加起来是多少。",
         kind="stdin", data=["1 2 3\n"], want="6"),
    dict(name="排序换个说法", desc="写一个程序：读入一行整数（标准输入），从高到低排一排。",
         kind="stdin", data=["3 1 2\n"], want="3 2 1"),
    dict(name="计数换个说法", desc="写一个程序：读入一行整数（标准输入），数一数一共有几个数。",
         kind="stdin", data=["4 5 6 7\n"], want="4"),
    dict(name="平均换个说法", desc="写一个程序：读入一行整数（标准输入），算一下平均数。",
         kind="stdin", data=["1 2 3\n"], want="2.00"),
    dict(name="倒序换个说法", desc="写一个程序：读入一行整数（标准输入），把顺序倒过来打印。",
         kind="stdin", data=["1 2 3\n"], want="3 2 1"),
    dict(name="乘积换个说法", desc="写一个程序：读入一行整数（标准输入），把它们乘在一起是多少。",
         kind="stdin", data=["2 3 4\n"], want="24"),
    dict(name="偶数个数换个说法", desc="写一个程序：读入一行整数（标准输入），里面有几个偶数。",
         kind="stdin", data=["1 2 4\n"], want="2"),
    dict(name="字符串长度换个说法", desc="写一个程序：读入一行字符串（标准输入），这串字有几个字符。",
         kind="stdin", data=["abcd\n"], want="4"),
    dict(name="转大写换个说法", desc="写一个程序：读入一行字符串（标准输入），全都变成大写。",
         kind="stdin", data=["ab\n"], want="AB"),
    dict(name="回文换个说法", desc="写一个程序：读入一行字符串（标准输入），正着读反着读一样吗（输出 yes 或 no）。",
         kind="stdin", data=["aba\n"], want="yes"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in PARA:
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
