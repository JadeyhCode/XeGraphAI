#!/usr/bin/env python3
"""tools/code_blind10.py — 第十份盲测（未教）。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="a换成z", desc="写一个程序：把一段文字里的所有 a 换成 z。",
         kind="stdin", data=["banana\n"], want="bznznz"),
    dict(name="1到9的阶乘之和", desc="写一个程序：输出 1 到 9 的阶乘之和。",
         kind="argv", data=[], want="409113"),
    dict(name="大于平均的个数", desc="写一个程序：给一串数，输出比平均值大的数的个数。",
         kind="stdin", data=["1 2 3 4\n"], want="2"),
    dict(name="矩阵转置输出", desc="写一个程序：把矩阵转置后输出。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="1 3\n2 4"),
    dict(name="有多少个数字", desc="写一个程序：数一数字符串里有多少个数字。",
         kind="stdin", data=["a1b2c3\n"], want="3"),
]

NEGATIVE = [
    dict(name="贪心算法", desc="写一个程序：用贪心算法解决背包问题。"),
    dict(name="动态规划", desc="写一个程序：用动态规划求最长上升子序列。"),
    dict(name="B树", desc="写一个程序：实现 B 树的插入和查找。"),
    dict(name="内核模块", desc="写一个程序：写一个 Linux 内核模块。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase10（未教） ==")
    p = {"pass": 0, "refuse": 0, "wrong": 0}
    for t in PARAPHRASE:
        status, got, out = ch.judge(t, MODEL)
        if status == "PASS":
            p["pass"] += 1; print(f"  [PASS]   {t['name']}")
        elif status == "NO_MATCH":
            p["refuse"] += 1; print(f"  [REFUSE] {t['name']}")
        else:
            p["wrong"] += 1
            print(f"  [WRONG]  {t['name']}  得到: {got!r} 期望: {t['want']!r}")
            line = next((l for l in out.splitlines() if "[合成]" in l), "")
            if line: print(f"           合成: {line.strip()[:130]}")
    print(f"  => pass {p['pass']} / refuse {p['refuse']} / wrong {p['wrong']}")
    print("== negative10（未教，期望拒绝） ==")
    n = {"refuse": 0, "wrong": 0}
    for t in NEGATIVE:
        status, got, out = probe_negative(t)
        if status == "NO_MATCH":
            n["refuse"] += 1; print(f"  [REFUSE] {t['name']}")
        else:
            n["wrong"] += 1; print(f"  [WRONG]  {t['name']}  硬答")
    print(f"  => refuse {n['refuse']} / wrong {n['wrong']}")


if __name__ == "__main__":
    main()
