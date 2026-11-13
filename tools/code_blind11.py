#!/usr/bin/env python3
"""tools/code_blind11.py — 第十一份盲测：12 个**经典 C 练习**（大多没教过），
用来量"教科书级新任务"的真实通过率。"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="汉诺塔", desc="写一个程序：输出 3 层汉诺塔的移动步骤。",
         kind="argv", data=[], want="7"),
    dict(name="八皇后", desc="写一个程序：输出八皇后问题的解的个数。",
         kind="argv", data=[], want="92"),
    dict(name="约瑟夫环", desc="写一个程序：10 个人围成圈，数到 3 出列，输出最后剩下的人的编号。",
         kind="argv", data=[], want="4"),
    dict(name="凯撒加密", desc="写一个程序：把字符串里的字母往后移 3 位（凯撒加密）。",
         kind="stdin", data=["abc\n"], want="def"),
    dict(name="矩阵对角线和", desc="写一个程序：读入矩阵（标准输入），输出主对角线上元素的和。",
         kind="stdin", data=["2 2\n1 2\n3 4\n"], want="5"),
    dict(name="数组循环左移", desc="写一个程序：读入一行整数（标准输入），把数组循环左移 2 位后输出。",
         kind="stdin", data=["1 2 3 4 5\n"], want="3 4 5 1 2"),
    dict(name="单词首字母大写", desc="写一个程序：把每个单词的首字母变成大写。",
         kind="stdin", data=["hello world\n"], want="Hello World"),
    dict(name="字符串压缩", desc="写一个程序：把连续重复的字符压缩成 字符+次数 的形式。",
         kind="stdin", data=["aaabb\n"], want="a3b2"),
    dict(name="数字转英文", desc="写一个程序：把 0 到 9 的数字转成英文单词。",
         kind="argv", data=["7"], want="seven"),
    dict(name="平方根整数", desc="写一个程序：读入一个整数，输出它的整数平方根（向下取整）。",
         kind="argv", data=["17"], want="4"),
    dict(name="日期差", desc="写一个程序：读入两个日期，输出相差的天数。",
         kind="argv", data=["2024", "1", "1", "2024", "1", "31"], want="30"),
    dict(name="大数乘法", desc="写一个程序：读入两个大整数（字符串），输出它们的乘积。",
         kind="argv", data=["12345678901234567890", "2"], want="24691357802469135780"),
]

NEGATIVE = [
    dict(name="正则引擎", desc="写一个程序：实现一个正则表达式引擎。"),
    dict(name="编译器", desc="写一个程序：把 C 编译成汇编。"),
    dict(name="数据库索引", desc="写一个程序：实现 B+ 树索引。"),
    dict(name="神经网络", desc="写一个程序：训练一个小型神经网络。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    return ("NO_MATCH", "", out) if not path else ("WRONG", "(生成了程序)", out)


def main():
    print("== paraphrase11（经典练习，大多未教） ==")
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
    print("== negative11 ==")
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
