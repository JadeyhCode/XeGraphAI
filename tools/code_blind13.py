#!/usr/bin/env python3
"""tools/code_blind13.py — 第三组“没教过”的题（blind11/12 都已被喂饱）。"""
import os, sys
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from code_hidden import judge, ROOT

BLIND13 = [
    dict(name="数字反转", desc="写一个程序：读入一个整数（命令行参数），把它的数字倒过来输出。",
         kind="argv", data=["123"], want="321"),
    dict(name="括号匹配", desc="写一个程序：读入一行字符串（标准输入），判断括号是否配对（输出 yes 或 no）。",
         kind="stdin", data=["(a[b])\n"], want="yes"),
    dict(name="元音计数", desc="写一个程序：读入一行字符串（标准输入），统计元音字母的个数。",
         kind="stdin", data=["hello\n"], want="2"),
    dict(name="数组去重排序", desc="写一个程序：读入一行整数（标准输入），去重后从小到大输出。",
         kind="stdin", data=["3 1 3 2\n"], want="1 2 3"),
    dict(name="罗马数字", desc="写一个程序：读入一个整数（命令行参数），输出它的罗马数字表示。",
         kind="argv", data=["14"], want="XIV"),
    dict(name="单词顺序反转", desc="写一个程序：读入一行字符串（标准输入），把单词顺序倒过来输出。",
         kind="stdin", data=["a b c\n"], want="c b a"),
    dict(name="第k小", desc="写一个程序：读入一行整数（标准输入），输出第 2 小的数。",
         kind="stdin", data=["5 3 9 1\n"], want="3"),
    dict(name="整数位数", desc="写一个程序：读入一个整数（命令行参数），输出它的位数。",
         kind="argv", data=["90210"], want="5"),
]

def main():
    model = os.environ.get("AIP_CODE_MODEL") or os.path.join(ROOT, "code/code_model.bin")
    p = r = w = 0
    for t in BLIND13:
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
