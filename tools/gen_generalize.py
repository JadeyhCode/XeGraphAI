#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/gen_generalize.py — **自动生成**泛化台题目（不手写题目、不手写答案）
==========================================================================

题目从三处**独立于蒸馏**的来源生成，所以"题是不是照着答案写的"这个问题不成立：

  ① `relations.txt`（人写的事实）  → 属于链的传递题、上位继承题；答案就是图里的闭包
  ② `AI_RELS[]`（代码里声明的属性）→ 核心关系的对称题（`怕` 声明了 sym）
  ③ `relprops.txt`（蒸馏出来的属性）→ 动态谓词的对称/传递/逆/同义题
      这一族的答案**由表本身给出** —— 它量的是"引擎有没有按表外推"（机制），
      不是"这个属性对不对"（后者靠双跑交集 + 确定性过滤 + 抽检 + 负向对照）。

负向对照（neg）是自动挑的：词池里有、但**表里没有**的谓词 ⇒ 反向问句必须**不乱推**
（答复不许以「对」开头）。这条能抓住"表外乱推广"的假增益。

用法：
  python3 tools/gen_generalize.py                           # 写 tests/generalize.txt
  python3 tools/gen_generalize.py --out /tmp/g.txt --per 4   # 每个族最多 4 条
"""
import argparse
import os
import re
import subprocess
import sys
import time

ENT = ["甲", "乙", "丙", "丁", "戊"]


def read_relprops(path):
    props, syn = {}, []
    if not os.path.exists(path):
        return props, syn
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        w = line.split()
        if w[0] == "prop" and len(w) >= 3:
            e = props.setdefault(w[1], {"trans": False, "sym": False, "inv": None})
            if w[2] in ("trans", "sym"):
                e[w[2]] = True
            elif w[2] == "inv" and len(w) == 4:
                e["inv"] = w[3]
        elif w[0] == "syn" and len(w) == 3:
            syn.append((w[1], w[2]))
    return props, syn


def read_relations(path):
    facts, negs = set(), set()
    if not os.path.exists(path):
        return facts, negs
    for line in open(path, encoding="utf-8"):
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        w = line.split()
        if len(w) == 3:
            facts.add((w[0], w[1], w[2]))
        elif len(w) == 4 and w[1] == "不":
            negs.add((w[0], w[2], w[3]))
    return facts, negs


def pool_check(root, words):
    cmd = "".join("::rel %s\n" % w for w in words) + "::quit\n"
    p = subprocess.run([os.path.join(root, "build_aip"), "--model", os.path.join(root, "model.bin")],
                       input=cmd.encode("utf-8"), cwd=root, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", "replace")
    return set(m.group(1) for m in re.finditer(r"\[([^\]]+)\] 的关系知识", out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--out", default="tests/generalize.txt")
    ap.add_argument("--per", type=int, default=6, help="每族最多几条")
    args = ap.parse_args()
    root = os.path.abspath(args.root)

    facts, negs = read_relations(os.path.join(root, "relations.txt"))
    props, syn = read_relprops(os.path.join(root, "relprops.txt"))

    items = []   # (setup, question, want, family)

    # ---- ① 属于链：传递（答案 = relations.txt 的闭包，与蒸馏无关）----
    up = {}
    for a, r, b in facts:
        if r == "属于":
            up.setdefault(a, set()).add(b)
    def closure(x, depth):
        seen, frontier = set(), [x]
        for _ in range(depth):
            nxt = []
            for y in frontier:
                for z in up.get(y, ()):
                    if z not in seen:
                        seen.add(z); nxt.append(z)
            frontier = nxt
        return seen
    trans_items = []
    for a in sorted(up):
        for c in sorted(closure(a, 3)):
            if (a, "属于", c) in facts:
                continue
            trans_items.append(("", "%s 属于 %s 吗" % (a, c), "^对", "ctl"))
            if len(trans_items) >= args.per:
                break
        if len(trans_items) >= args.per:
            break
    items += trans_items

    # ---- ② 上位继承（会/有 沿属于）：答案 = 图 + 代码里声明的 inherit ----
    inh = []
    for (c, r, y) in sorted(facts):
        if r not in ("有", "会"):
            continue
        for x in sorted(up):
            if c not in up.get(x, ()):          # x 属于 c：属性 y 从 c 继承下来
                continue
            if (x, r, y) in facts or (x, r, y) in negs:
                continue
            inh.append(("", "%s %s %s 吗" % (x, r, y), "^对", "ctl"))
            if len(inh) >= args.per:
                break
        if len(inh) >= args.per:
            break
    items += inh

    # ---- ③ 核心对称：**不收** ----
    # `怕` 在 AI_RELS 里声明了 sym，但汉语里「人怕火」推不出「火怕人」——
    # 拿它当判据会把"声明写得不严谨"当成泛化能力。核心关系只留 传递/继承 两类。

    # ---- ④ 蒸馏表：对称 / 传递 / 逆 / 同义 ----
    neg_cand = ["追", "讨厌", "偷", "抱", "送", "给", "教", "学", "买", "卖", "知道", "相信"]
    pool = pool_check(root, sorted(props.keys()) + [s for p in syn for s in p] + neg_cand)
    order = [w for w in sorted(props) if w in pool]
    fam = []
    for w in order:
        if props[w]["sym"]:
            fam.append(("%s %s %s" % (ENT[0], w, ENT[1]),
                        "%s %s %s 吗" % (ENT[1], w, ENT[0]), "^对", "sym"))
    fam = fam[:args.per]
    items += fam

    fam = []
    for w in order:
        if not props[w]["trans"]:
            continue
        a, b, c, d = ENT[0], ENT[1], ENT[2], ENT[3]
        fam.append(("%s %s %s;%s %s %s" % (a, w, b, b, w, c),
                    "%s %s %s 吗" % (a, w, c), "^对", "trans"))
        fam.append(("%s %s %s;%s %s %s;%s %s %s" % (a, w, b, b, w, c, c, w, d),
                    "%s %s %s 吗" % (a, w, d), "^对", "trans"))
    items += fam[:args.per]

    fam = []
    for w in order:
        inv = props[w].get("inv")
        if not inv:
            continue
        fam.append(("%s %s %s" % (ENT[0], w, ENT[1]),
                    "%s %s %s 吗" % (ENT[1], inv, ENT[0]), "^对", "inv"))
    items += fam[:args.per]

    fam = []
    for x, y in syn:
        if x not in pool and y not in pool:
            continue
        # 让**有属性**的那个词当依据，另一个词当被测词（一条属性覆盖同义组）
        if y in props:
            tgt, other = y, x
        elif x in props:
            tgt, other = x, y
        else:
            continue
        if other not in pool:
            continue
        e = props[tgt]
        if e["sym"]:
            fam.append(("%s %s %s" % (ENT[0], other, ENT[1]),
                        "%s %s %s 吗" % (ENT[1], other, ENT[0]), "^对", "syn"))
        elif e["trans"]:
            fam.append(("%s %s %s;%s %s %s" % (ENT[0], other, ENT[1], ENT[1], other, ENT[2]),
                        "%s %s %s 吗" % (ENT[0], other, ENT[2]), "^对", "syn"))
    items += fam[:args.per]

    # ---- ⑤ 负向对照：词池里有、表里没有的谓词 ⇒ 反向**不许**乱推 ----
    core = ("属于", "包含", "是", "有", "会", "怕", "喜欢", "爱", "吃", "在", "需要")
    cand = [w for w in neg_cand if w in pool and w not in props and w not in core]
    negf = []
    for w in cand[:2]:
        negf.append(("%s %s %s" % (ENT[0], w, ENT[1]),
                     "%s %s %s 吗" % (ENT[1], w, ENT[0]), "!^对", "neg"))
    items += negf

    # ---- 写文件 ----
    hdr = [
        "# tests/generalize.txt — **由 tools/gen_generalize.py 自动生成**，不要手改",
        "#",
        "# 来源：① relations.txt（属于链/上位继承/核心对称：真值来自图与代码声明）",
        "#       ② relprops.txt（蒸馏出来的谓词属性：量的是「引擎按表外推」这层机制）",
        "#       ③ 词池里有、表里没有的谓词（负向对照：反向不许乱推）",
        "# 实体名甲/乙/丙/丁/戊全是现场现造的新名字 ⇒ 答对不是「记过」，是推出来的。",
        "# 生成时间：%s" % time.strftime("%Y-%m-%d %H:%M"),
        "#",
    ]
    # 第一列（前置陈述）为空时写 `-`：POSIX `read` 的 IFS 空白会在**行首**把空字段吞掉
    # （IFS 只有 tab 也算空白）—— 实测 ctl 族整列串位，判据全错。
    lines = hdr + ["\t".join(("-" if not t[0] else t[0],) + tuple(t[1:])) for t in items]
    out = os.path.join(root, args.out)
    with open(out, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    fams = {}
    for _, _, _, f_ in items:
        fams[f_] = fams.get(f_, 0) + 1
    print("[生成] %s：%d 条 %s" % (out, len(items), fams))
    return 0


if __name__ == "__main__":
    sys.exit(main())
