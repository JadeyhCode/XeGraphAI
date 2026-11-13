#!/usr/bin/env python3
# tools/arc2_bench.py — 在 **ARC-AGI-2** 上量这个白箱 AI（**只量它自己，不许挂外挂**）
#
# ARC-AGI-2 是什么：每题给 2~6 对「输入网格 → 输出网格」示例（≤30×30、10 色整数矩阵），
# 要归纳出变换规则再套到测试输入上。它考的是**从极少例子里现场归纳新规则**，
# 而且专门设计来打败"枚举对称/镜像/平铺这类小变换"的做法 —— 正好是本项目
# 「现场推理、不靠预训练」这个主张最硬的反面考卷。
#
# ⚠ 测试协议（**不能作弊**，四条硬规矩）：
#   1. **只喂它该看见的东西**：训练对的输入+输出、测试对的**输入**。测试对的答案（output）
#      只在**最后判分**时用一次，绝不参与"选规则/挑答案"。
#   2. **不挂外挂**：不允许把外部求解器（枚举变换、搜索程序、调用别的模型）的答案算成它的分。
#      所以这个工具**只有一段**：把题按现有接口喂给 build_aip，看它答什么。
#      （曾经量过一版"外部枚举求解器"（ARC-AGI-1 训练集 5%、ARC-AGI-2 eval 0%）——
#        那**不是这个系统的分**，已从工具里删掉，只在 docs 里留一句备注。）
#   3. **不调参、不挑题**：全量 120 题一次跑完，不看结果再改判据；判据只有一条「网格精确相等」。
#   4. **不训练/不教**：不往图里灌这两套题的任何东西（每题一个干净进程）。
#
# 数据：`arcprize/ARC-AGI-2` 公开仓库 —— evaluation **120 题**（公开 eval，官方允许本地测）、
#      training 1000 题（公开训练集，本工具不喂给系统，只列个数作对照）。
#
# 用法：
#   python3 tools/arc2_bench.py                  # 全量 120 题
#   python3 tools/arc2_bench.py --limit 10       # 先跑 10 题（表头会标）
#   python3 tools/arc2_bench.py --model model_post3.bin
#
# 产物：_bench/arc2_results.jsonl（逐题：对/弃答/无回答/硬答 + 原回答）、_bench/arc2_summary.md
import argparse
import json
import os
import re
import subprocess
import sys
import tarfile
import urllib.request

ROOT = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
DATA_DIR = os.path.join(ROOT, "_bench", "data", "ARC-AGI-2")
TARBALL = "https://codeload.github.com/arcprize/ARC-AGI-2/tar.gz/refs/heads/main"
OUT = os.path.join(ROOT, "_bench")
COMMIT = "f3283f727488"      # 取数时的 main（2025-05-15）
ABSTAIN = re.compile(r"(不太懂|不知道|没学过|不认识|没记过|不编|不会|真不会|不太会|没把握|拿不准|想想|好像|答不上|答不了|不确定|不太确定)")


def ensure_data():
    ev = os.path.join(DATA_DIR, "data", "evaluation")
    if os.path.isdir(ev) and len(os.listdir(ev)) > 100:
        return
    os.makedirs(DATA_DIR, exist_ok=True)
    path = os.path.join(OUT, "data", "arc2.tgz")
    print(f"[下载] {TARBALL}", flush=True)
    req = urllib.request.Request(TARBALL, headers={"User-Agent": "aip-arc2-bench"})
    try:
        data = urllib.request.urlopen(req, timeout=120).read()
    except Exception as e:
        print(f"[警告] 校验 TLS 失败（{e}），改用不校验下载", flush=True)
        import ssl
        data = urllib.request.urlopen(req, timeout=120, context=ssl._create_unverified_context()).read()
    open(path, "wb").write(data)
    with tarfile.open(path) as t:
        for m in t.getmembers():
            parts = m.name.split("/", 1)
            if len(parts) == 2 and parts[1].startswith("data/"):
                m.name = parts[1]
                t.extract(m, DATA_DIR)


def load(split):
    d = os.path.join(DATA_DIR, "data", split)
    return [(fn[:-5], json.load(open(os.path.join(d, fn))))
            for fn in sorted(os.listdir(d)) if fn.endswith(".json")]


def grid_text(g):
    return "[" + "/".join("".join(str(v) for v in row) for row in g) + "]"


def serialize(task, max_pairs=6):
    """只写训练对的输入+输出，和测试对的**输入** —— 测试答案一个字都不给。"""
    parts = ["找出输入到输出的变换规律，然后给出测试输入的输出。"]
    for k, p in enumerate(task["train"][:max_pairs], 1):
        parts.append(f"例{k} 输入{grid_text(p['input'])} 输出{grid_text(p['output'])}")
    for k, p in enumerate(task["test"], 1):
        parts.append(f"测试{k} 输入{grid_text(p['input'])} 输出？")
    return " ".join(parts)


def ask(binary, model, text, max_line=1200):
    """题干按 1200 字符切行（引擎输入缓冲 4096 字节，中文按字节算，留余量），取最后一条 AI 回答。"""
    text = re.sub(r"\s+", " ", text).strip()
    lines = [text[i:i + max_line] for i in range(0, len(text), max_line)]
    stdin = "\n".join(lines) + "\n::quit\n"
    try:
        p = subprocess.run([binary, "--model", model], input=stdin, text=True,
                           capture_output=True, timeout=180)
        out = p.stdout
    except subprocess.TimeoutExpired:
        return ""
    reps = [l[l.find("AI > ") + 5:].strip() for l in out.splitlines() if "AI > " in l]
    return reps[-1] if reps else ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.path.join(ROOT, "model.bin"))
    ap.add_argument("--binary", default=os.path.join(ROOT, "build_aip"))
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    ensure_data()
    ev = load("evaluation")
    tr = load("training")
    if args.limit:
        ev = ev[:args.limit]
    if not (os.path.exists(args.binary) and os.path.exists(args.model)):
        sys.exit("缺 build_aip 或 model.bin")
    os.makedirs(OUT, exist_ok=True)

    rows = []
    with open(os.path.join(OUT, "arc2_results.jsonl"), "w", encoding="utf-8") as f:
        for i, (name, t) in enumerate(ev, 1):
            reply = ask(args.binary, args.model, serialize(t))
            flat = re.sub(r"\s+", "", reply)
            # 判分才用答案：测试输出必须逐格相等（紧凑形式，两边去空白比对）
            want = "".join(grid_text(p["output"]) for p in t["test"])
            correct = bool(flat) and re.sub(r"\s+", "", want) in flat
            ab = bool(ABSTAIN.search(flat))
            r = {"id": name, "correct": correct, "abstain": ab, "empty": not flat, "reply": reply[:200]}
            rows.append(r)
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
            if i % 20 == 0:
                print(f"  ... {i}/{len(ev)} 答对 {sum(x['correct'] for x in rows)}", flush=True)

    n = len(rows)
    ca = sum(r["correct"] for r in rows)
    ab = sum(r["abstain"] for r in rows)
    emp = sum(r["empty"] for r in rows)
    wr = n - ca - ab - emp
    lines = [f"# ARC-AGI-2 基准 · {os.path.basename(args.model)}", "",
             f"- 数据：`arcprize/ARC-AGI-2` @ `{COMMIT}`（公开仓库）｜evaluation **{n}** 题"
             + (f"（`--limit {args.limit}` 截断，**不是全量数**）" if args.limit else "")
             + f"｜training {len(tr)} 题（**未喂给系统**，只作对照）",
             "- 协议：训练对给输入+输出、测试对**只给输入**；判据 = 网格逐格精确相等；全量一次跑完、不挑题、不调参",
             "- 外挂：**没有**（不挂枚举求解器、不调外部模型）；所有答案都来自 `build_aip` 自己", "",
             "| 项 | 数 |", "|---|---|",
             f"| 答对 | **{ca}/{n}**（{100.0 * ca / n:.1f}%）|",
             f"| 弃答（明说不知道） | {ab}/{n} |",
             f"| 无回答（没输出） | {emp}/{n} |",
             f"| 硬答（答了但错） | {wr}/{n} |", ""]
    if n and ca == 0:
        lines += ["> 0 分就是 0 分：它没有网格/视觉通道，也不会从例子里归纳变换。",
                  "> 这一段的用途是**把这个 0 钉在案上**，而不是用外挂把它填成非 0。", ""]
    text = "\n".join(lines)
    open(os.path.join(OUT, "arc2_summary.md"), "w", encoding="utf-8").write(text + "\n")
    print()
    print(text)


if __name__ == "__main__":
    main()
