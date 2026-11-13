#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/distill_relprop.py — 从本地小模型**多教师交叉**蒸馏「谓词代数」
================================================================================

蒸馏什么：**谓词怎么推广**（trans/sym/inv/syn），不是问句的答案。
  trans  A X B, B X C ⇒ A X C 恒成立           （可传递）
  sym    A X B ⇔ B X A                          （对称）
  inv    A X B ⇔ B Y A 的那个 Y                 （逆关系）
  syn    二place 用法上可互换的词               （同义 ⇒ 代数同）

为什么这是"泛化"而不是"记答案"：
  · 表里没有一条针对具体实体的事实；教 `甲 认识 乙` 之后 `乙 认识 甲` 是学生自己按表推的；
  · 判据是 `tests/generalize.sh`（题目由 `tools/gen_generalize.py` 自动生成，实体名现场新造）；
  · `AIP_NOALG=1` 关表 ⇒ 新能力必须**随之消失**，否则增益不是这条机制带来的。

小模型的可靠性问题（实测 qwen3-0.6b）：它会说「了解」对称、「保护」可传递 —— 语义上是错的。
所以协议里**没有"单教师说了算"这一步**：
  1. 词表：只用**模型词池里真有**的谓词（池检查确定性：批量 `::rel` 看"在/不在图里"）。
  2. **多教师 × 双跑取交集**：每个教师跑两次（temp 0.2 / 0.8），
     一个字段只有在**所有教师的所有跑次**都说是时才保留。不同模型犯的错不一样，
     交集能把"某个模型的口头禅"滤掉（这也是蒸馏里标准的 agreement gate）。
  3. 确定性过滤器：inv/syn 必须在池里、inv ≠ 自己、逆关系不许一对多；syn 只留池里的词。
  4. 输出里带每个字段的"同意票数"，抽检时能看出哪些是险过。

用法：
  python3 tools/distill_relprop.py --out relprops.txt \
      --teacher http://127.0.0.1:18321/v1|sk-...|qwen3-0.6b \
      --teacher http://127.0.0.1:18322/v1|sk-...|qwen2.5-coder-1.5b --audit 20
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.request

# 候选谓词：常见「二place」动词/形容词。会不会用只看**词池**（pool_check）。
CANDIDATES = [
    "认识", "知道", "了解", "熟悉", "喜欢", "讨厌", "恨", "怕", "尊敬", "佩服",
    "羡慕", "嫉妒", "相信", "怀疑", "同意", "反对", "支持", "帮助", "陪伴", "保护",
    "照顾", "追", "逃", "咬", "打", "骂", "夸", "骗", "偷", "抢", "送", "给",
    "借", "还", "教", "学", "买", "卖", "交换", "分享", "使用", "制造", "修理",
    "发现", "找到", "记得", "忘记", "需要", "包含", "包括", "属于", "超过", "大于",
    "小于", "等于", "相同", "一样", "不同", "相反", "相邻", "连接", "组成", "构成",
    "变成", "成为", "像", "类似", "在", "有", "会", "吃", "喝", "看", "听",
]

SYSTEM = "你是一个严谨的中文语义学助手。只输出一行合法 JSON，不要解释、不要代码块围栏。"

PROMPT = """判断中文谓词「{w}」在二place 关系 `A {w} B` 里的代数性质，只输出 JSON：
{{"trans": true/false, "sym": true/false, "inv": "词"或null, "syn": []}}

判据（严格，拿不准就 false/null）：
- trans：任意 A,B,C，`A {w} B` 且 `B {w} C` ⇒ `A {w} C` 恒成立。
- sym：`A {w} B` ⇔ `B {w} A` 恒成立。
- inv：存在 Y 使 `A {w} B` ⇔ `B Y A` 严格恒等；没有就 null。
- syn：与「{w}」在 `A _ B` 里可互换的词，最多 2 个；没有就 []。
"""


def chat(url, key, model, prompt, temperature):
    body = json.dumps({
        "model": model,
        "messages": [{"role": "system", "content": SYSTEM},
                     {"role": "user", "content": prompt}],
        "temperature": temperature,
        "max_tokens": 120,
        "stream": False,
    }).encode("utf-8")
    req = urllib.request.Request(url.rstrip("/") + "/chat/completions", data=body,
                                 headers={"Content-Type": "application/json",
                                          "Authorization": "Bearer " + key})
    with urllib.request.urlopen(req, timeout=120) as r:
        data = json.loads(r.read().decode("utf-8"))
    return data["choices"][0]["message"]["content"]


def parse_json(text):
    if not text:
        return None
    m = re.search(r"\{.*\}", text, re.S)
    if not m:
        return None
    try:
        d = json.loads(m.group(0))
    except Exception:
        return None
    if not isinstance(d, dict):
        return None
    inv = d.get("inv")
    if not (isinstance(inv, str) and inv and inv.lower() != "null"):
        inv = None
    syn = d.get("syn")
    syn = [w for w in syn if isinstance(w, str)] if isinstance(syn, list) else []
    return {"trans": d.get("trans") is True, "sym": d.get("sym") is True,
            "inv": inv, "syn": syn}


def pool_check(root, words):
    """确定性检查：这些词在模型词池里吗？（起一个进程，批量 `::rel`）"""
    cmd = "".join("::rel %s\n" % w for w in words) + "::quit\n"
    p = subprocess.run([os.path.join(root, "build_aip"), "--model", os.path.join(root, "model.bin")],
                       input=cmd.encode("utf-8"), cwd=root, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    out = p.stdout.decode("utf-8", "replace")
    return set(m.group(1) for m in re.finditer(r"\[([^\]]+)\] 的关系知识", out))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--out", default="relprops.txt")
    ap.add_argument("--teacher", action="append", default=[],
                    help="url|key|model，可重复（多教师交叉）")
    ap.add_argument("--raw-dir", default="_bench/distill")
    ap.add_argument("--audit", type=int, default=0)
    ap.add_argument("--dry", action="store_true")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    if not args.teacher:
        print("FAIL: 至少一个 --teacher url|key|model")
        return 2
    teachers = []
    for t in args.teacher:
        parts = t.split("|")
        if len(parts) != 3:
            print("FAIL: --teacher 格式是 url|key|model：%s" % t)
            return 2
        teachers.append(parts)

    alive = pool_check(root, CANDIDATES)
    words = [w for w in CANDIDATES if w in alive]
    dead = [w for w in CANDIDATES if w not in alive]
    print("[池检查] %d 个候选，%d 个在词池里（不在的：%s）" % (len(CANDIDATES), len(words), " ".join(dead)))
    if not words:
        print("FAIL: 一个谓词都不在池里")
        return 1

    os.makedirs(os.path.join(root, args.raw_dir), exist_ok=True)
    votes = {w: {"trans": 0, "sym": 0, "inv": {}, "syn": {}} for w in words}
    n_vote = 0
    t0 = time.time()
    for (url, key, model) in teachers:
        for temp in (0.2, 0.8):
            got = {}
            for w in words:
                try:
                    txt = chat(url, key, model, PROMPT.format(w=w), temp)
                except Exception as e:
                    print("  [网络] %s/%s: %s" % (model, w, e))
                    txt = ""
                got[w] = txt
                d = parse_json(txt)
                n_vote += 1
                if not d:
                    continue
                if d["trans"]:
                    votes[w]["trans"] += 1
                if d["sym"]:
                    votes[w]["sym"] += 1
                if d["inv"]:
                    votes[w]["inv"][d["inv"]] = votes[w]["inv"].get(d["inv"], 0) + 1
                for s in d["syn"]:
                    votes[w]["syn"][s] = votes[w]["syn"].get(s, 0) + 1
            rd = os.path.join(root, args.raw_dir, "%s_t%s.json" % (model, temp))
            with open(rd, "w", encoding="utf-8") as f:
                json.dump(got, f, ensure_ascii=False, indent=1)
            print("[蒸馏] %s temp=%s 完成 -> %s" % (model, temp, rd))

    # 交集 = 所有 (教师×跑次) 都说是
    n_all = len(teachers) * 2
    props, all_yes = {}, 0
    for w in words:
        v = votes[w]
        e = {"trans": v["trans"] == n_all, "sym": v["sym"] == n_all,
             "inv": next((k for k, c in v["inv"].items() if c == n_all), None),
             "syn": [k for k, c in v["syn"].items() if c == n_all]}
        if e["trans"] or e["sym"] or e["inv"] or e["syn"]:
            props[w] = e
            all_yes += 1
    print("[交集] %d/%d 个谓词有字段通过 %d 票一致；%d 个谓词被否掉"
          % (all_yes, len(words), n_all, len(words) - all_yes))

    # 确定性过滤
    for w, e in list(props.items()):
        if e["inv"] and (e["inv"] not in alive or e["inv"] == w):
            e["inv"] = None
    invmap = {}
    for w, e in props.items():
        if e["inv"]:
            invmap.setdefault(e["inv"], []).append(w)
    for target, owners in invmap.items():
        if len(owners) > 1:                    # 逆关系一对多 ⇒ 全丢
            for o in owners:
                props[o]["inv"] = None
    for w, e in props.items():
        e["syn"] = [s for s in e["syn"] if s in alive and s != w][:2]

    lines = ["# relprops.txt — 谓词代数（数据，可审计）", "#"]
    lines.append("# 来源：本地小模型多教师交叉蒸馏 %s"
                 % "、".join("%s" % m for (_, _, m) in teachers))
    lines.append("# 时间：%s" % time.strftime("%Y-%m-%d %H:%M"))
    lines.append("# 协议：每教师双跑（temp 0.2/0.8），字段级**全票一致**才保留 + 池检查/逆关系一致性过滤")
    lines.append("# 原始输出：%s/<模型>_t*.json" % args.raw_dir)
    lines.append("# 纪律：这里只有**谓词怎么推广**，没有一条具体实体的答案；AIP_NOALG=1 整张表关掉。")
    lines.append("#")
    n_t = n_s = n_i = n_y = 0
    for w in sorted(props):
        e = props[w]
        if e["trans"]:
            lines.append("prop %s trans" % w); n_t += 1
        if e["sym"]:
            lines.append("prop %s sym" % w); n_s += 1
        if e["inv"]:
            lines.append("prop %s inv %s" % (w, e["inv"])); n_i += 1
        for s in e["syn"]:
            if s < w:
                lines.append("syn %s %s" % (w, s)); n_y += 1
    lines.append("")
    lines.append("# 统计：trans=%d sym=%d inv=%d syn=%d；%d 个谓词入选" % (n_t, n_s, n_i, n_y, len(props)))
    text = "\n".join(lines) + "\n"

    if args.audit:
        print("[抽检] %d 条（raw 在 %s）：" % (min(args.audit, len(props)), args.raw_dir))
        for w in sorted(props)[:args.audit]:
            e = props[w]
            print("  %-4s trans=%-5s sym=%-5s inv=%-4s syn=%s"
                  % (w, e["trans"], e["sym"], e["inv"] or "-", ",".join(e["syn"]) or "-"))

    print("[结果] trans=%d sym=%d inv=%d syn=%d（%d 谓词）；%d 次请求，耗时 %.1fs"
          % (n_t, n_s, n_i, n_y, len(props), n_vote, time.time() - t0))
    if args.dry:
        print(text)
        return 0
    with open(os.path.join(root, args.out), "w", encoding="utf-8") as f:
        f.write(text)
    print("[写盘] %s" % os.path.join(root, args.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
