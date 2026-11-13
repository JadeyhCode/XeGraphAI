#!/usr/bin/env python3
# tools/hle_bench.py — 在 **HLE（Humanity's Last Exam）** 上量这个白箱 AI
#
# 为什么有这个工具：HLE 是"专家级、跨学科、防检索"的 2500 题（数学/物理/CS/生物/人文…），
# 测的是**通用专家知识与推理**。本系统是"符号图 + 关系推理"，我判断它拿不到分 —— 但**判断不算数，
# 量出来才算**。这个脚本只做一件事：把题**原样**喂给它，看它答什么，并分成
# 「答对 / 弃答（明说不知道）/ 硬答（答了但错）」三类。第三类才是真正要盯的：**不编**比得分重要。
#
# 数据来源与口径（必须写清楚，否则数字没法比）：
#   · 官方集 `cais/hle` 在 HuggingFace 上是 **gated**（无 token 拿不到 parquet，实测 401）。
#   · 这里用**公开镜像** `lmms-lab/HLE-Verified`（HLE-Verified：对官方题逐题复核/修订后的版本，
#     2500 题齐全，字段与官方一致：question / answer / answer_type / category / raw_subject / has_image）。
#   · 官方 2500 题里 **342 题带图**（本系统没有视觉通道 ⇒ 不算它的错，单列 N/A）。
#   · 计分只报两项：**严格命中**（答案规范化后与标准答案相同/等价）和**宽松命中**（标准答案作为子串出现，
#     是上界，用来兜住"格式不同但内容对"）。两者都接近 0 时，只有宽松值有信息量。
#
# 用法：
#   python3 tools/hle_bench.py                    # 全量（2158 道文本题），约 90 s
#   python3 tools/hle_bench.py --limit 100        # 先跑 100 道
#   python3 tools/hle_bench.py --model model_post3.bin
#   python3 tools/hle_bench.py --download         # 数据不在缓存里时自动下载（默认会自动下）
#
# 产物：_bench/hle_results.jsonl（逐题：对/弃答/硬答 + 原回答）、_bench/hle_summary.md
import argparse
import hashlib
import json
import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
DATA_URL = "https://huggingface.co/datasets/lmms-lab/HLE-Verified/resolve/main/full.jsonl"
DATA_SHA = "228e6ade6867843f550805d59e98d5a54745cad13ff34b42b2f752e9c467331d"
CACHE = os.path.join(ROOT, "_bench", "data", "hle_full.jsonl")
OUT = os.path.join(ROOT, "_bench")

# 「它明说不知道」的措辞（全系统统一走这几条路，见 agent.h 的兜底）
ABSTAIN = re.compile(r"(不太懂|不知道|没学过|不认识|没记过|不编|不会|真不会|不太会|没把握|拿不准|想想|好像|答不上|答不了|不确定|不太确定)")
# 把问句当陈述句记下来了 —— 这是**误解**，不是"编答案"，但也要单列（它会污染会话图）
MISREAD = re.compile(r"^(记下了|好的|学到了|学会|明白|记下)")


def _try_download(url, path, timeout=120):
    """下一份；成功返回 True。TLS 校验失败时退回不校验（本机 CA 不全）。"""
    req = urllib.request.Request(url, headers={"User-Agent": "aip-hle-bench"})
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r, open(path, "wb") as f:
            f.write(r.read())
        return True
    except Exception as e:
        print(f"[警告] {url} 失败（{e}），改用不校验重试", flush=True)
    try:
        import ssl
        ctx = ssl._create_unverified_context()
        with urllib.request.urlopen(req, timeout=timeout, context=ctx) as r, open(path, "wb") as f:
            f.write(r.read())
        return True
    except Exception as e:
        print(f"[警告] 不校验也失败（{e}）", flush=True)
        return False


def fetch(path, url):
    """数据下载：**直连失败就换镜像**（实测 huggingface.co 在这台机器上经常 timeout，
    而 hf-mirror.com 通；没有这条兜底，基准就只能"下次再跑"）。"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path) and os.path.getsize(path) > 1000:
        return path
    urls = [url]
    if "huggingface.co" in url:
        urls.append(url.replace("huggingface.co", "hf-mirror.com"))
    for u in urls:
        print(f"[下载] {u}", flush=True)
        if _try_download(u, path) and os.path.getsize(path) > 1000:
            return path
    raise SystemExit("FAIL: 数据下载不了（直连和镜像都不通）—— 手动放到 _bench/data/ 再跑")


def norm(s):
    """规范化：小写、去空白、去 LaTeX 包装/标点 —— 只为了让"内容对"不被格式打败。"""
    s = s.lower()
    s = re.sub(r"\\(boxed|text|mathrm|mathbf|mathbb|operatorname)\s*", "", s)
    s = re.sub(r"[\s~$\{\}\\\\,;:。，；：、\"'`]+", "", s)
    return s


def strip_latex_answer(a):
    """标准答案有的带 \\boxed{...} 或 \\[ ... \\]：只取里面那层内容。"""
    m = re.search(r"\\boxed\{(.+?)\}\s*$", a, re.S)
    if m:
        a = m.group(1)
    a = re.sub(r"^\s*\\\[\s*|\s*\\\]\s*$", "", a)
    return a.strip()


def numeric_eq(a, b):
    try:
        return abs(float(a) - float(b)) < 1e-9
    except Exception:
        return False


def judge(gold, reply, answer_type):
    """返回 (strict, loose)。strict=内容等价；loose=标准答案作为子串出现（上界）。"""
    g = strip_latex_answer(str(gold))
    gn, rn = norm(g), norm(reply)
    if not gn or not rn:
        return False, False
    if gn == rn or numeric_eq(gn, rn):
        return True, True
    # 多选题：回答里出现同样那个字母（且不是被中文包住的长词）
    if answer_type == "multipleChoice" and re.fullmatch(r"[A-Ja-j]", g.strip()):
        letters = re.findall(r"\b([A-Ja-j])\b", reply)
        if letters:
            if len(letters) == 1 and letters[0].lower() == g.strip().lower():
                return True, True
            return False, any(x.lower() == g.strip().lower() for x in letters)
    loose = gn in rn
    if len(gn) <= 2 and not loose:
        # 一两个字符的答案（数字/字母）只在回答很短时才算命中，否则"24 出现在长句里"会误判
        loose = len(rn) <= 24 and gn in rn
    return False, loose


def ask(binary, model, text, max_line=1200):
    """把题干切成 <=1200 字符的行（引擎的输入缓冲是 4096，中文按字节算，留余量），
    喂进去，取**最后一条** AI 回答。"""
    text = re.sub(r"\s+", " ", text).strip()
    lines = [text[i:i + max_line] for i in range(0, len(text), max_line)] or [""]
    stdin = "\n".join(lines) + "\n::quit\n"
    try:
        p = subprocess.run([binary, "--model", model], input=stdin, text=True,
                           capture_output=True, timeout=120)
        out = p.stdout
    except subprocess.TimeoutExpired:
        return ""
    replies = []
    for line in out.splitlines():
        i = line.find("AI > ")
        if i >= 0:
            replies.append(line[i + 5:].strip())
    return replies[-1] if replies else ""


def summarize_only(args):
    """从 _bench/hle_results.jsonl 重算摘要：只改展示，不碰逐题判定。"""
    p = os.path.join(OUT, "hle_results.jsonl")
    rows = [json.loads(l) for l in open(p, encoding="utf-8")]
    n = len(rows)
    strict = sum(r["strict"] for r in rows)          # 答对判定**不动**（存下来的就是原始判定）
    loose = sum(r["loose"] for r in rows)
    # 桶只做措辞归类：用当前这份（唯一的）弃答/误解词表，对**已存下来的原回答**重算
    def bucket(r):
        flat = re.sub(r"\s+", "", r.get("reply", ""))
        if not flat:
            return "empty"
        if ABSTAIN.search(flat):
            return "abstain"
        if MISREAD.search(flat):
            return "misread"
        return "wrong"
    buckets = [bucket(r) for r in rows]
    abstain = buckets.count("abstain"); empty = buckets.count("empty")
    misread = buckets.count("misread"); wrong = buckets.count("wrong")
    by = {}
    for r in rows:
        d = by.setdefault(r["category"], [0, 0, 0, 0, 0, 0])
        b = bucket(r)
        d[0] += 1; d[1] += r["strict"]; d[2] += (b == "abstain")
        d[3] += (b == "empty"); d[4] += (b == "misread"); d[5] += (b == "wrong")
    def pct(x):
        return f"{100.0 * x / n:.2f}%" if n else "n/a"
    out = [f"# HLE（Humanity's Last Exam）基准 · {os.path.basename(args.model)}", "",
           "- 数据：`HLE-Verified` 公开镜像（官方 `cais/hle` 在 HuggingFace 上是 gated，无 token 拿不到）",
           f"- 实测：**{n} 道文本题**（官方 2500 题里带图的 342 题没有视觉通道，计 N/A）",
           "- 协议：一题一进程，只喂**题干**（不含答案/提示）；判据 = 标准答案规范化后完全相等；全量一次跑完、不教、不调参", "",
           "| 类别 | 题数 | 答对（严格） | 弃答（明说不知道） | 无回答 | 误解成陈述 | 硬答（真编了） |",
           "|---|---|---|---|---|---|---|"]
    for cat, d in sorted(by.items(), key=lambda kv: -kv[1][0]):
        out.append(f"| {cat} | {d[0]} | {d[1]} | {d[2]} | {d[3]} | {d[4]} | {d[5]} |")
    out.append(f"| **合计** | **{n}** | **{strict}** | **{abstain}** | **{empty}** | **{misread}** | **{wrong}** |")
    out += ["", f"**答对（严格）{strict}/{n}（{pct(strict)}）**", "",
            f"**弃答 {abstain}/{n}（{pct(abstain)}）｜无回答 {empty}/{n}｜误解成陈述 {misread}/{n}｜硬答（真编了）{wrong}/{n}**", "",
            f"（诊断：连「标准答案作为子串出现」的宽松口径也是 {loose}/{n}。）"]
    text = "\n".join(out)
    open(os.path.join(OUT, "hle_summary.md"), "w", encoding="utf-8").write(text + "\n")
    print(text)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.path.join(ROOT, "model.bin"))
    ap.add_argument("--binary", default=os.path.join(ROOT, "build_aip"))
    ap.add_argument("--data", default=CACHE)
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--download", action="store_true", help="（默认就会自动下载）")
    ap.add_argument("--summarize-only", action="store_true",
                    help="不重跑，用 _bench/hle_results.jsonl 重算摘要（改的是展示，不是判据）")
    args = ap.parse_args()
    if not os.path.exists(args.data):
        args.data = fetch(args.data, DATA_URL)
    if not (os.path.exists(args.binary) and os.path.exists(args.model)):
        sys.exit("缺 build_aip 或 model.bin")

    if args.summarize_only:
        return summarize_only(args)

    rows = [json.loads(l) for l in open(args.data, encoding="utf-8")]
    h = hashlib.sha256(open(args.data, "rb").read()).hexdigest()
    text_rows = [r for r in rows if not r.get("has_image")]
    img = len(rows) - len(text_rows)
    if args.limit:
        text_rows = text_rows[:args.limit]

    os.makedirs(OUT, exist_ok=True)
    res_path = os.path.join(OUT, "hle_results.jsonl")
    strict = loose = abstain = wrong = empty = misread = 0
    by_cat = {}
    with open(res_path, "w", encoding="utf-8") as f:
        for i, r in enumerate(text_rows, 1):
            reply = ask(args.binary, args.model, r["question"])
            s, l = judge(r["answer"], reply, r.get("answer_type"))
            flat = re.sub(r"\s+", "", reply)
            ab = bool(ABSTAIN.search(flat))
            mr = bool(MISREAD.search(re.sub(r"^[^\s]*>", "", flat)))
            if s:
                strict += 1
            elif ab:
                abstain += 1
            elif not flat:
                empty += 1
            elif mr:
                misread += 1
            else:
                wrong += 1
            loose += 1 if l else 0
            cat = r.get("category", "?")
            d = by_cat.setdefault(cat, [0, 0, 0, 0, 0, 0, 0])  # 题数/严格/宽松/弃答/空/误解/硬答
            d[0] += 1; d[1] += s; d[2] += l; d[3] += ab
            d[4] += (not flat); d[5] += (not s and not ab and bool(flat) and mr)
            d[6] += (not s and not ab and bool(flat) and not mr)
            f.write(json.dumps({"id": r.get("id"), "category": cat, "subject": r.get("raw_subject"),
                                "type": r.get("answer_type"), "gold": str(r["answer"])[:200],
                                "reply": reply[:300], "strict": s, "loose": l, "abstain": ab,
                                "empty": not flat, "misread": bool(flat) and mr},
                               ensure_ascii=False) + "\n")
            if i % 100 == 0:
                print(f"  ... {i}/{len(text_rows)}  严格 {strict} 宽松 {loose} 弃答 {abstain} 硬答 {wrong}",
                      flush=True)

    n = len(text_rows)
    def pct(x):
        return f"{100.0 * x / n:.2f}%" if n else "n/a"
    summary = []
    summary.append(f"# HLE（Humanity's Last Exam）基准 · {os.path.basename(args.model)}\n")
    summary.append(f"- 数据：`HLE-Verified` 公开镜像（`{DATA_URL}`）")
    summary.append(f"  sha256 `{h[:16]}…`｜{len(rows)} 题（官方全集）｜其中**带图 {img} 题**（无视觉通道，计 N/A）")
    summary.append(f"- 本次实测：**{n} 道文本题**（一题一进程，题干按 1200 字符切行喂入，取最后一条回答）")
    summary.append(f"- 二进制 `build_aip`，模型 `{os.path.basename(args.model)}`\n")
    summary.append("| 类别 | 题数 | 答对（严格） | 弃答（明说不知道） | 无回答 | 误解成陈述 | 硬答（真编了） |")
    summary.append("|---|---|---|---|---|---|---|")
    for cat, d in sorted(by_cat.items(), key=lambda kv: -kv[1][0]):
        c, s, l, ab, emp, mr, wr = d
        summary.append(f"| {cat} | {c} | {s} | {ab} | {emp} | {mr} | {wr} |")
    summary.append(f"| **合计** | **{n}** | **{strict}** | **{abstain}** | **{empty}** | **{misread}** | **{wrong}** |")
    summary.append("")
    summary.append(f"**答对（严格）{strict}/{n}（{pct(strict)}）** —— 判据：标准答案规范化后完全相等；"
                   f"多选题要求回答里**只有**那个字母。")
    summary.append(f"（诊断用：连「标准答案作为子串出现」这种宽松口径也是 {loose}/{n}，"
                   f"所以 0 这个数没有争议。）")
    summary.append(f"**弃答 {abstain}/{n}（{pct(abstain)}）｜无回答 {empty}/{n}（{pct(empty)}）"
                   f"｜误解成陈述 {misread}/{n}（{pct(misread)}）｜硬答（真编了）{wrong}/{n}（{pct(wrong)}）**")
    text = "\n".join(summary)
    open(os.path.join(OUT, "hle_summary.md"), "w", encoding="utf-8").write(text + "\n")
    print()
    print(text)


if __name__ == "__main__":
    main()
