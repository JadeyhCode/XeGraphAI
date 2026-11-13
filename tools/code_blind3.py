#!/usr/bin/env python3
"""tools/code_blind3.py — 第三份盲测（前两份都已被"教过"）。

第三份**暂不教**，留作后续轮次的诚实读数：描述用更口语的说法，负样本换新任务。

用法：python3 tools/code_blind3.py
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="数字加起来", desc="写一个程序：给个整数，把它里面的数字全部加起来。",
         kind="argv", data=["1234"], want="10"),
    dict(name="谁最小", desc="写一个程序：从三个数里找出最小的那个。",
         kind="argv", data=["3", "9", "4"], want="3"),
    dict(name="几个字母", desc="写一个程序：数一下这行有几个字符。",
         kind="stdin", data=["hello\n"], want="5"),
    dict(name="转成二进制", desc="写一个程序：给一个数，转成二进制写出来。",
         kind="argv", data=["10"], want="1010"),
    dict(name="约数最大", desc="写一个程序：求两个数的最大公约数。",
         kind="argv", data=["12", "18"], want="6"),
    dict(name="最大的那个", desc="写一个程序：一串数里最大的那个打出来。",
         kind="stdin", data=["3 9 4\n"], want="9"),
    dict(name="排一排", desc="写一个程序：把这些数按从小到大排一排再打出来。",
         kind="stdin", data=["3 1 2\n"], want="1 2 3"),
    dict(name="一直加到 20", desc="写一个程序：从 1 一直加到 20。",
         kind="argv", data=[], want="210"),
    dict(name="元音几个", desc="写一个程序：这句话里有几个元音？",
         kind="stdin", data=["hello\n"], want="2"),
    dict(name="是不是三的倍数", desc="写一个程序：看一个数是不是 3 的倍数。",
         kind="argv", data=["9"], want="1"),
]

NEGATIVE = [
    dict(name="石头剪刀布", desc="写一个程序：和电脑玩石头剪刀布。"),
    dict(name="解一元二次方程", desc="写一个程序：解一元二次方程并打印根。"),
    dict(name="统计词频", desc="写一个程序：统计一段英文里每个单词出现的次数。"),
    dict(name="矩阵求逆", desc="写一个程序：求一个 3x3 矩阵的逆。"),
    dict(name="压缩文件", desc="写一个程序：用 gzip 压缩一个文件。"),
    dict(name="画棋盘", desc="写一个程序：在终端画一个棋盘图案。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    if not os.path.exists(MODEL):
        print(f"FAIL: 缺模型 {MODEL}")
        return 2
    print("== paraphrase3（未教） ==")
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

    print("== negative3（未教，期望拒绝） ==")
    n = {"refuse": 0, "wrong": 0}
    for t in NEGATIVE:
        status, got, out = probe_negative(t)
        if status == "NO_MATCH":
            n["refuse"] += 1; print(f"  [REFUSE] {t['name']}")
        else:
            n["wrong"] += 1; print(f"  [WRONG]  {t['name']}  硬答")
            line = next((l for l in out.splitlines() if "[合成]" in l), "")
            if line: print(f"           合成: {line.strip()[:130]}")
    print(f"  => refuse {n['refuse']} / wrong {n['wrong']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
