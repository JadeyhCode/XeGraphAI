#!/usr/bin/env python3
"""tools/code_blind7.py — 第七份盲测（未教；只看"错答"是不是结构性问题）。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="三数平均", desc="写一个程序：给三个整数，输出它们的平均值，保留两位小数。",
         kind="argv", data=["1", "2", "3"], want="2.00"),
    dict(name="1到6的阶乘", desc="写一个程序：把 1 到 6 的阶乘算出来。",
         kind="argv", data=[], want="720"),
    dict(name="五个整数最大", desc="写一个程序：读取 5 个整数，输出最大的那个。",
         kind="stdin", data=["3 9 4 2 1\n"], want="9"),
    dict(name="小写字母数量", desc="写一个程序：统计字符串里小写字母的数量。",
         kind="stdin", data=["AbC\n"], want="1"),
    dict(name="整数转二进制", desc="写一个程序：把整数转成二进制输出。",
         kind="argv", data=["10"], want="1010"),
    dict(name="两个整数较大者", desc="写一个程序：读入两个整数，输出较大者。",
         kind="argv", data=["2", "9"], want="9"),
    dict(name="长度乘二", desc="写一个程序：输出字符串长度乘 2。",
         kind="stdin", data=["hello\n"], want="10"),
    dict(name="排序后第一个", desc="写一个程序：把数组排序后输出第 1 个元素。",
         kind="stdin", data=["3 1 2\n"], want="1"),
]

NEGATIVE = [
    dict(name="词法分析器", desc="写一个程序：实现一个 C 词法分析器。"),
    dict(name="解释器", desc="写一个程序：实现一个表达式解释器。"),
    dict(name="游戏引擎", desc="写一个程序：写一个 2D 游戏引擎。"),
    dict(name="编译器优化", desc="写一个程序：对中间代码做死代码消除。"),
    dict(name="分布式锁", desc="写一个程序：实现分布式锁。"),
    dict(name="图像滤镜", desc="写一个程序：给图片加高斯模糊。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase7（未教） ==")
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
    print("== negative7（未教，期望拒绝） ==")
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
