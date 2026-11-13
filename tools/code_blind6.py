#!/usr/bin/env python3
"""tools/code_blind6.py — 第六份盲测（未教，作为当前诚实读数）。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="数组乘二打印", desc="写一个程序：把数组里的数乘以 2 再打印出来。",
         kind="stdin", data=["1 2 3\n"], want="2 4 6"),
    dict(name="1到5的立方和", desc="写一个程序：输出 1 到 5 的立方和。",
         kind="argv", data=[], want="225"),
    dict(name="每个字符变大写", desc="写一个程序：把字符串里的每个字符变成大写。",
         kind="stdin", data=["abc\n"], want="ABC"),
    dict(name="三个数里最大", desc="写一个程序：求三个数里最大的。",
         kind="argv", data=["3", "9", "4"], want="9"),
]

NEGATIVE = [
    dict(name="贪吃蛇", desc="写一个程序：实现贪吃蛇游戏。"),
    dict(name="五子棋", desc="写一个程序：实现五子棋对战。"),
    dict(name="编译器", desc="写一个程序：把 C 代码编译成汇编。"),
    dict(name="调度算法", desc="写一个程序：实现操作系统的进程调度。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase6（未教） ==")
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
    print("== negative6（未教，期望拒绝） ==")
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
