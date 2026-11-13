#!/usr/bin/env python3
"""tools/code_blind5.py — 第五份盲测（未教，作为当前诚实读数）。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="两数加起来乘三", desc="写一个程序：把两个数加起来再乘 3。",
         kind="argv", data=["2", "3"], want="15"),
    dict(name="十个斐波那契", desc="写一个程序：输出 10 个斐波那契数。",
         kind="argv", data=[], want="0 1 1 2 3 5 8 13 21 34"),
    dict(name="几个大写字母", desc="写一个程序：数一数字符串里有几个大写字母。",
         kind="stdin", data=["AbC\n"], want="2"),
    dict(name="求和再除以个数", desc="写一个程序：把数组里的数求和后除以个数。",
         kind="stdin", data=["1 2 3\n"], want="2.00"),
    dict(name="1到10的平方和", desc="写一个程序：输出 1 到 10 的平方和。",
         kind="argv", data=[], want="385"),
    dict(name="是不是回文", desc="写一个程序：判断一个字符串是不是回文。",
         kind="stdin", data=["level\n"], want="1"),
]

NEGATIVE = [
    dict(name="走迷宫", desc="写一个程序：让角色自动走迷宫。"),
    dict(name="计算器界面", desc="写一个程序：画一个计算器界面。"),
    dict(name="网络爬虫", desc="写一个程序：抓取网页标题。"),
    dict(name="人脸识别", desc="写一个程序：识别照片里的人脸。"),
    dict(name="数据库连接", desc="写一个程序：连接数据库并查询。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase5（未教） ==")
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

    print("== negative5（未教，期望拒绝） ==")
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
