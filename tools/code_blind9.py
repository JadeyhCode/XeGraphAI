#!/usr/bin/env python3
"""tools/code_blind9.py — 第九份盲测（未教；本轮**只测不教**）。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="平均值两位小数", desc="写一个程序：输入一行数，输出它们的平均值（两位小数）。",
         kind="stdin", data=["1 2 3\n"], want="2.00"),
    dict(name="数空格", desc="写一个程序：数一下这句话里有多少个空格。",
         kind="stdin", data=["a b c\n"], want="2"),
    dict(name="是不是素数", desc="写一个程序：给一个整数，输出它是不是素数。",
         kind="argv", data=["97"], want="1"),
    dict(name="1到6的和", desc="写一个程序：输出 1 到 6 的和。",
         kind="argv", data=[], want="21"),
    dict(name="数组倒过来输出", desc="写一个程序：把数组里的数倒过来再输出。",
         kind="stdin", data=["1 2 3\n"], want="3 2 1"),
    dict(name="删掉数字", desc="写一个程序：把字符串里的数字都删掉。",
         kind="stdin", data=["a1b2\n"], want="ab"),
]

NEGATIVE = [
    dict(name="图着色", desc="写一个程序：给图着色，相邻点颜色不同。"),
    dict(name="拓扑排序", desc="写一个程序：对有向图做拓扑排序。"),
    dict(name="视频解码", desc="写一个程序：解码一段视频。"),
    dict(name="分布式共识", desc="写一个程序：实现 Raft 共识算法。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase9（未教，只测不教） ==")
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
    print("== negative9（未教，期望拒绝） ==")
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
