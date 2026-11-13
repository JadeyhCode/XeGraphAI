#!/usr/bin/env python3
"""tools/code_blind2.py — 第二份盲测（第一份已被"教过"，不能再当泛化读数）。

规则：描述刻意用**新说法**（尽量避开已登记关键词）；负样本换成**新的**不会任务。
判据同 code_blind.py：PASS / REFUSE / WRONG；负样本只期望 REFUSE。

用法：python3 tools/code_blind2.py
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="各位数相加", desc="写一个程序：给一个整数，求它各位数相加的结果。",
         kind="argv", data=["1234"], want="10"),
    dict(name="三个里挑最小", desc="写一个程序：从三个整数里挑出最小的打印。",
         kind="argv", data=["3", "9", "4"], want="3"),
    dict(name="有多少个字符", desc="写一个程序：读一行文本，输出它有多少个字符。",
         kind="stdin", data=["hello\n"], want="5"),
    dict(name="十进制换二进制", desc="写一个程序：把十进制数换成二进制打印。",
         kind="argv", data=["10"], want="1010"),
    dict(name="最大公因数", desc="写一个程序：求两个整数的最大公因数。",
         kind="argv", data=["12", "18"], want="6"),
    dict(name="挑出最大的", desc="写一个程序：输入一串整数，挑出其中最大的。",
         kind="stdin", data=["3 9 4\n"], want="9"),
    dict(name="从小到大排好", desc="写一个程序：把数组里的数从小到大排好再打印。",
         kind="stdin", data=["3 1 2\n"], want="1 2 3"),
    dict(name="1 加到 50", desc="写一个程序：求 1 加到 50。",
         kind="argv", data=[], want="1275"),
    dict(name="多少个元音", desc="写一个程序：统计一段话里有多少个元音。",
         kind="stdin", data=["hello\n"], want="2"),
    dict(name="能被三整除", desc="写一个程序：判断一个数能不能被 3 整除。",
         kind="argv", data=["9"], want="1"),
]

NEGATIVE = [
    dict(name="括号匹配", desc="写一个程序：判断字符串里的括号是否匹配。"),
    dict(name="快速排序实现", desc="写一个程序：实现快速排序算法。"),
    dict(name="图最短路", desc="写一个程序：求图里两点之间的最短路径。"),
    dict(name="哈希表", desc="写一个程序：实现一个哈希表并统计词频。"),
    dict(name="文件重命名", desc="写一个程序：把文件重命名。"),
    dict(name="进程通信", desc="写一个程序：两个进程通过管道传数据。"),
    dict(name="JSON 解析", desc="写一个程序：解析一段 JSON 并打印字段。"),
    dict(name="中文分词", desc="写一个程序：对一句中文做分词。"),
]


def probe_negative(t):
    path, out = ch.run_synth(MODEL, t["desc"])
    if not path:
        return "NO_MATCH", "", out
    return "WRONG", "(生成了程序)", out


def main():
    if not os.path.exists(MODEL):
        print(f"FAIL: 缺模型 {MODEL}")
        return 2
    print("== paraphrase2（新说法） ==")
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

    print("== negative2（新的不会任务，期望拒绝） ==")
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
