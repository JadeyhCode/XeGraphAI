#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把类型化搜索解出来的题写进知识（任务/骨架），让 C 管线也能答（§6.841 的学习环节）。

用法：
  python3 tools/code_learn.py            # 解析 blind26 → 写 code/knowledge/searched.triples
  python3 tools/code_learn.py --dry      # 只打印，不写
"""
import os
import random
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_search as S

OUT = os.path.join(ROOT, "code", "knowledge", "searched.triples")

# 每题一个**足够窄**的说法（别用“长度/个数”，会劫持别的题）
LEARN_KW = {
    # blind27
    "去重": "去掉重复",
    "去重后长度": "去重后输出一共",
    "去重后第二小": "去重后输出第二小",
    "前缀和最大": "前缀和里的最大",
    "前缀和最小": "前缀和里的最小",
    "乘二后极差": "之后输出极差",
    "加五后方差": "之后输出方差",
    "反转后前缀和最大": "反转后再输出前缀和",
    # blind26
    "极差": "极差",
    "平方和": "平方和",
    "偶数个数": "偶数的个数",
    "排序后最大": "排序后输出最大",
    "加三后平均": "每个数加",
    "大写后长度": "转成大写后输出长度",
    "反转后取最小": "反转后输出最小",
    "乘二后求和": "每个数乘",
}


def triples_for(task, chain):
    name = "搜索-" + task["name"]
    kw = LEARN_KW.get(task["name"], task["name"])
    shape = task["shape"]
    src = S.gen_program(shape, chain)
    if not src:
        return []
    lines = ["任务:%s\t关键词\t%s" % (name, kw)]
    # 通用让位：命令行参数 / 1 到 N / 第一行给 N / 每行 / 矩阵 这些形态交给原任务，
    # 别让搜索结果劫持（实测“平方和”会抢隐藏组的 “1 到 N 的平方和”）。
    exs = ["命令行参数", "1 到", "第一行", "每行"]
    # 形状隔离：数组题别抢字符串题（实测“去掉重复的字符”被数组去重抢走）
    exs += ["字符串", "字符"] if task["shape"] == "arr" else ["数组", "整数"]
    for ex in exs:
        lines.append("任务:%s\t排除\t%s" % (name, ex))
    lines += ["任务:%s\t骨架\t骨架:%s" % (name, name),
             "骨架:%s\t语言\tC11" % name,
             "骨架:%s\t依赖\tstdio.h stdlib.h string.h" % name]
    # 续行格式：每行末尾反斜杠（与 skeletons.triples 一致）
    code = "\\\n".join(src.strip("\n").split("\n"))
    lines.append("骨架:%s\t代码\t%s" % (name, code))
    return lines


def main(argv):
    dry = "--dry" in argv
    rng = random.Random(20261114)
    rows, out = 0, []
    TASKS = list(S.BLIND26) + list(S.BLIND27)   # 两套一起学，整份重写
    for t in TASKS:
        chain, why = S.search(t["desc"], rng, t["gold"], t["shape"])
        if not chain:
            print("  [SKIP] %s（%s）" % (t["name"], why))
            continue
        rows += 1
        out.extend(triples_for(t, chain))
        print("  [LEARN] %s ⇒ %s" % (t["name"], " → ".join(n for n, _ in chain)))
    body = "\n".join(out) + ("\n" if out else "")
    if dry:
        print(body)
        return 0
    # 整份重写（确定性、幂等）：不做增量合并，避免重复追加
    with open(OUT, "w", encoding="utf-8") as f:
        f.write("# 由 tools/code_learn.py 生成（类型化搜索结果 → 任务/骨架；整份重写）\n")
        f.write(body)
    print("=> 学到 %d 题，写入 %d 行 → %s" % (rows, len(out), OUT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
