#!/usr/bin/env python3
"""tools/code_blind4.py — 第四份盲测（未教，作为当前诚实读数）。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="求平均", desc="写一个程序：输入若干整数，求它们的平均（保留两位小数）。",
         kind="stdin", data=["1 2 3\n"], want="2.00"),
    dict(name="左右翻转字符串", desc="写一个程序：把字符串左右翻转。",
         kind="stdin", data=["abc\n"], want="cba"),
    dict(name="相除的余数", desc="写一个程序：给两个数，输出它们相除的余数。",
         kind="argv", data=["7", "3"], want="1"),
    dict(name="算阶乘", desc="写一个程序：算出 5 的阶乘。",
         kind="argv", data=[], want="120"),
    dict(name="是不是闰年", desc="写一个程序：判断年份是不是闰年。",
         kind="argv", data=["2024"], want="1"),
    dict(name="从大到小", desc="写一个程序：把一串数字从大到小排列。",
         kind="stdin", data=["3 1 2\n"], want="3 2 1"),
]

NEGATIVE = [
    dict(name="扫雷", desc="写一个程序：实现扫雷游戏。"),
    dict(name="迷宫最短路", desc="写一个程序：找出迷宫的最短路径。"),
    dict(name="学生成绩管理", desc="写一个程序：管理学生成绩并排名。"),
    dict(name="加密解密", desc="写一个程序：把一段文字加密再解密。"),
    dict(name="查询天气", desc="写一个程序：联网查询天气。"),
    dict(name="画正弦曲线", desc="写一个程序：在终端画正弦曲线。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase4（未教） ==")
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

    print("== negative4（未教，期望拒绝） ==")
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
