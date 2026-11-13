#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""tools/distill_wrappers.py — 让本地千问只干"生成说法变体"，真假由学生自己裁决
================================================================================

为什么改成这样（上一版的教训）：把 0.6B 当数据工厂，它产出 `水 属于 水`、`[问] <X> 音乐`
这种垃圾，而我写的门太弱（只查重复不查自环、只看"答复里有没有主题词"），
于是分数是假的。**教师只该提供"语言变体"，不该提供"事实"和"结构"。**

两件事分开：

  ① 骨架（说法变体）：教师给**礼貌前缀 / 句尾**候选（劳驾、麻烦问一下、多谢…）。
     学法是仓库已有的 `[lead]` / `[tail]`（一行一条，词级）⇒ **教一个前缀，所有问句都受益**
     —— 这就是举一反三。
  ② 知识：教师只**提议**三元组，机械门（词池/关系/自环/重复/分类冲突）过完写进
     `distill/claims.txt`，**不进事实层**；要不要信由用户 `::claim accept` 决定。

判据（留出，且是"包装前后一致"这种可计算的东西）：
  · 基线：`前缀 + 问句` 答不出来（兜底）⇒ 这是真缺口；
  · 学后：同一批答得出来 ⇒ 学到了；
  · 举一反三：教过前缀 P 之后，**没教过的问句**套 P 也要答得出来。

用法：
  python3 tools/distill_wrappers.py --teacher "http://127.0.0.1:18321/v1|<key>|qwen3-0.6b"
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
import urllib.request

SYSTEM = "你是一个中文语言助手。只输出要求的内容，每行一条，不要编号、不要解释。"

WRAP_PROMPT = """列出 {n} 个中文里用在**问句开头**的礼貌/铺垫说法（2-4 个字），
例如：劳驾、请问、麻烦问一下、想请教、打扰一下。
只输出这些说法，每行一个。"""

TAIL_PROMPT = """列出 {n} 个中文里用在**问句结尾**的客气说法（2-4 个字），
例如：谢谢、多谢、拜托了、不胜感激。
只输出这些说法，每行一个。"""

FACTS_PROMPT = """用中文常识写 {n} 条事实三元组，每行一条，格式严格是：
主语 关系 宾语
关系只能是：属于 有 会 怕 喜欢 吃 在 需要
主语和宾语只能从这些词里选：{words}
只输出这些行，不要编号、不要解释。"""

RELS = ["属于", "有", "会", "怕", "喜欢", "吃", "在", "需要"]
SEED_WORDS = ("猫 狗 老鼠 鸟 鱼 花 草 树 山 河 海 水 火 雪 雨 风 云 太阳 月亮 星星 "
              "书 笔 茶 咖啡 音乐 画画 游泳 唱歌 写字 做饭 读书 旅行 电影 游戏 运动 "
              "睡觉 动物 植物 食物 水果 饮料 人 老师 学生 小孩 朋友 家人 生物 世界 时间")

# 留出问句：都是 relations.txt / model.bin 里**已经会**的事实（基线必须答得出）
SEED_Q = [
    "猫 属于 动物 吗", "狗 属于 动物 吗", "鱼 属于 动物 吗", "花 属于 植物 吗",
    "猫 有 毛 吗", "猫 会 抓老鼠 吗", "鸟 会 飞 吗", "猫 怕 狗 吗",
]
FALLBACK = ("不太懂", "不 太 懂", "不知道", "不 知道", "没学过", "还没学过", "答不上",
            "不确定", "真不会", "这个我")


def chat(url, key, model, user, temperature=0.4, max_tokens=300):
    body = json.dumps({"model": model,
                       "messages": [{"role": "system", "content": SYSTEM},
                                    {"role": "user", "content": user}],
                       "temperature": temperature, "max_tokens": max_tokens,
                       "stream": False}).encode("utf-8")
    req = urllib.request.Request(url.rstrip("/") + "/chat/completions", data=body,
                                 headers={"Content-Type": "application/json",
                                          "Authorization": "Bearer " + key})
    with urllib.request.urlopen(req, timeout=180) as r:
        d = json.loads(r.read().decode("utf-8"))
    return d["choices"][0]["message"]["content"]


def pool_check(root, words):
    cmd = "".join("::rel %s\n" % w for w in words) + "::quit\n"
    p = subprocess.run([os.path.join(root, "build_aip"), "--model", os.path.join(root, "model.bin")],
                       input=cmd.encode("utf-8"), cwd=root, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT)
    return set(m.group(1) for m in re.finditer(r"\[([^\]]+)\] 的关系知识",
                                               p.stdout.decode("utf-8", "replace")))


def ask(sandbox, q):
    p = subprocess.run([os.path.join(sandbox, "build_aip"), "--model",
                        os.path.join(sandbox, "model.bin")],
                       input=(q + "\n::quit\n").encode("utf-8"), cwd=sandbox,
                       stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    got = re.findall(r"AI > (.*)", p.stdout.decode("utf-8", "replace"))
    return got[-1].strip() if got else ""


def answered(text):
    return bool(text) and not any(f in text for f in FALLBACK)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--root", default=".")
    ap.add_argument("--teacher", required=True, help="url|key|model")
    ap.add_argument("--out-dir", default="distill")
    ap.add_argument("--n", type=int, default=12)
    ap.add_argument("--n-know", type=int, default=12)
    ap.add_argument("--dry", action="store_true")
    args = ap.parse_args()
    root = os.path.abspath(args.root)
    url, key, model = args.teacher.split("|")
    outdir = os.path.join(root, args.out_dir)
    os.makedirs(outdir, exist_ok=True)

    # ---------- ① 说法变体（教师只干这个） ----------
    def gen(prompt, n):
        try:
            raw = chat(url, key, model, prompt.format(n=n))
        except Exception as e:
            print("  [网络] %s" % e)
            return []
        out = []
        # 0.6B 常常返回**一行用「、」分隔**（实测），不一定一行一个 ⇒ 按多种分隔符切
        for line in re.split(r"[\n、，,;；]+", raw):
            w = line.strip().strip("。.-—·0123456789 ")
            if not w or len(w) > 8:
                continue
            if re.search(r"[0-9A-Za-z<>$#=]", w):
                continue
            if w in out:
                continue
            out.append(w)
        return out

    leads = gen(WRAP_PROMPT, args.n)
    tails = gen(TAIL_PROMPT, args.n)
    with open(os.path.join(outdir, "raw_wrappers.txt"), "w", encoding="utf-8") as f:
        f.write("[lead]\n" + "\n".join(leads) + "\n[tail]\n" + "\n".join(tails) + "\n")
    print("[教师] 前缀 %d 个：%s" % (len(leads), " ".join(leads)))
    print("[教师] 句尾 %d 个：%s" % (len(tails), " ".join(tails)))

    # ---------- ② 知识：只提议，过机械门，进待证 ----------
    alive = pool_check(root, SEED_WORDS.split())
    words = [w for w in SEED_WORDS.split() if w in alive]
    facts, seen = [], set()
    try:
        rawk = chat(url, key, model, FACTS_PROMPT.format(n=args.n_know, words=" ".join(words)))
    except Exception as e:
        rawk = ""
        print("  [网络] %s" % e)
    open(os.path.join(outdir, "raw_facts.txt"), "w", encoding="utf-8").write(rawk)
    relf = os.path.join(root, "relations.txt")
    old = set()
    if os.path.exists(relf):
        for line in open(relf, encoding="utf-8"):
            w = line.split("#", 1)[0].split()
            if len(w) == 3:
                old.add((w[0], w[1], w[2]))
    for line in re.split(r"[\n；;]+", rawk):
        w = line.strip().strip("0123456789.、) ").split()
        if len(w) != 3 or w[1] not in RELS:
            continue
        s, r, o = w
        if s == o or s not in alive or o not in alive or (s, r, o) in old or s in seen:
            continue
        if r == "属于" and any(a == s and rr == "属于" and x != o for (a, rr, x) in old):
            continue
        seen.add(s)
        facts.append((s, r, o))
    with open(os.path.join(outdir, "claims.txt"), "w", encoding="utf-8") as f:
        f.write("# 教师提议的事（来源：%s，%s）—— **待证**，不进事实层；\n"
                "# 要信就走 `::claim` 确认。格式：<主语> <关系> <宾语>\n"
                % (model, time.strftime("%Y-%m-%d %H:%M")))
        for s, r, o in facts:
            f.write("%s %s %s\n" % (s, r, o))
    print("[知识] 过门后 %d 条（只过**结构**门：自环/重复/池外/分类冲突；"
          "**语义真假没有验证** ⇒ 只进待证，不进事实层）" % len(facts))

    # ---------- ③ 沙箱：基线 / 学后 ----------
    sb = os.path.join(root, "_t", "wrap")
    shutil.rmtree(sb, ignore_errors=True)
    os.makedirs(sb)
    for f in ("build_aip", "model.bin", "relations.txt", "frames.txt", "patterns.txt",
              "lexicon.txt", "relprops.txt"):
        src = os.path.join(root, f)
        if os.path.exists(src):
            shutil.copy2(src, os.path.join(sb, f))

    def wrap(kind, w, q):
        if kind == "lead":
            return "%s %s" % (w, q)
        return "%s %s" % (q[:-1] if q.endswith("吗") else q, w + " 吗" if q.endswith("吗") else w)

    def probe(wrappers, kind, qs):
        hit = 0
        for w in wrappers:
            for q in qs:
                if answered(ask(sb, wrap(kind, w, q))):
                    hit += 1
        return hit, len(wrappers) * len(qs)

    # **基线必须先把仓库里已有的这些行摘掉**：上一版没摘，仓库早就学会了，
    # 于是量出来"基线 20/20"——判据当场失效（这也是返工的一部分）。
    pat0 = os.path.join(sb, "patterns.txt")
    allpat = open(pat0, encoding="utf-8").read() if os.path.exists(pat0) else ""
    mine = set(leads) | set(tails)
    keep = []
    for line in allpat.splitlines():
        w = line.strip()
        if w.startswith("[lead] ") or w.startswith("[tail] "):
            word = w.split(None, 1)[1].strip()
            if word in mine:
                continue
        keep.append(line)
    open(pat0, "w", encoding="utf-8").write("\n".join(keep) + "\n")

    holdout = SEED_Q[4:]          # 留出：用来判"举一反三"的问句
    base_q = SEED_Q[:4]           # 基线体检用
    b_lead, t_lead = probe(leads, "lead", base_q)
    print("[基线] 前缀+问句 答得出：%d/%d" % (b_lead, t_lead))

    # 学：把前缀/句尾写回 patterns.txt（就是仓库已有的数据表，一行一条）
    pat = os.path.join(sb, "patterns.txt")
    open(pat, "w", encoding="utf-8").write(allpat)
    add = ["", "# ---- 蒸馏说法（教师：%s，%s）：一行一条 ----" % (model, time.strftime("%Y-%m-%d"))]
    add += ["[lead] %s" % w for w in leads]
    add += ["[tail] %s" % w for w in tails]
    open(pat, "a", encoding="utf-8").write("\n".join(add) + "\n")

    a_lead, _ = probe(leads, "lead", base_q)
    h_lead, h_tot = probe(leads, "lead", holdout)
    b_tail, t_tail = 0, 0
    print("[学后] 前缀+问句 答得出：%d/%d" % (a_lead, t_lead))
    print("[举一反三] 教过的前缀 + **没教过**的问句：%d/%d" % (h_lead, h_tot))

    rep = ("前缀基线 %d/%d → 学后 %d/%d；留出问句 %d/%d\n"
           % (b_lead, t_lead, a_lead, t_lead, h_lead, h_tot))
    open(os.path.join(outdir, "report.txt"), "w", encoding="utf-8").write(rep)
    print(rep.strip())
    return 0


if __name__ == "__main__":
    sys.exit(main())
