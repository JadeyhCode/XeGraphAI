#!/usr/bin/env python3
"""tools/code_blind8.py — 第八份盲测（未教；专挑结构性新要求）。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="第二大的数", desc="写一个程序：读入一行整数，输出第二大的数。",
         kind="stdin", data=["3 9 4\n"], want="4"),
    dict(name="a换成b", desc="写一个程序：把字符串里的每个 a 换成 b。",
         kind="stdin", data=["banana\n"], want="bbnbnb"),
    dict(name="1到100能被3整除的个数", desc="写一个程序：统计 1 到 100 里能被 3 整除的数的个数。",
         kind="argv", data=[], want="33"),
    dict(name="5的阶乘", desc="写一个程序：输出 5 的阶乘。",
         kind="argv", data=[], want="120"),
    dict(name="前N个偶数之和", desc="写一个程序：给定 N，输出前 N 个偶数之和。",
         kind="argv", data=["3"], want="12"),
    dict(name="负数变0", desc="写一个程序：把数组里的负数变成 0。",
         kind="stdin", data=["-1 2 -3\n"], want="0 2 0"),
    dict(name="最长单词长度", desc="写一个程序：求字符串里最长的单词长度。",
         kind="stdin", data=["hi there\n"], want="5"),
    dict(name="10个数从大到小", desc="写一个程序：把 10 个数从大到小排好。",
         kind="stdin", data=["3 9 4 2 1 8 7 6 5 0\n"], want="9 8 7 6 5 4 3 2 1 0"),
]

NEGATIVE = [
    dict(name="排序稳定性证明", desc="写一个程序：证明冒泡排序是稳定的。"),
    dict(name="栈溢出检测", desc="写一个程序：检测栈溢出。"),
    dict(name="内存池", desc="写一个程序：实现一个内存池分配器。"),
    dict(name="协程调度", desc="写一个程序：实现协程调度器。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase8（未教） ==")
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
    print("== negative8（未教，期望拒绝） ==")
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
