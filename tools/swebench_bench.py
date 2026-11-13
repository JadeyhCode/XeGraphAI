#!/usr/bin/env python3
# tools/swebench_bench.py — 在 **SWE-bench**（Lite，300 题）上量这个白箱 AI（**不作弊**）
#
# SWE-bench 是什么：给一个真实 GitHub 仓库的 issue（英文），要求产出**能通过仓库测试的补丁**；
# 官方判分 = 把补丁打进 base_commit、跑 FAIL_TO_PASS 与 PASS_TO_PASS 测试，全绿才算 resolved。
# 它考的是**在真实代码库里改代码**：读代码、定位、写补丁、跑测试、按报错再改。
#
# ⚠ 测试协议（**不能作弊**，比 ARC/HLE 更严，因为这份数据里带"金补丁"）：
#   1. **只读三列**：`instance_id / repo / problem_statement`。金补丁（patch）、测试补丁（test_patch）、
#      FAIL_TO_PASS 列**根本不读进内存**（`--prepare` 落缓存时也只写这三列）。
#   2. **不挂外挂**：不用别的模型/别的 agent 生成补丁；所有输出都来自 `build_aip` 自己。
#   3. **不训练不教**：一题一个干净进程，不往图里灌任务或测试。
#   4. **判据写死**：①有没有产出**可应用的补丁**（unified diff：`@@` 或 `--- `/`+++ `）
#      ②有没有**假装改好了**（"已修复/完成"）③认输（"不太懂/不会"）。
#   5. **不跑假评测**：官方 harness 需要 Docker + 每仓库环境；本系统产出 0 个补丁 ⇒ 没有东西可评测。
#      工具里**不伪造** resolved 分，只在结果里写明"官方评测未运行（无补丁）"。
#
# 数据：`princeton-nlp/SWE-bench_Lite`（公开，test split 300 题，parquet 1.1 MB）。
#
# 用法：
#   python3 tools/swebench_bench.py --prepare     # parquet → _bench/data/swebench_lite.jsonl（只留三列）
#   python3 tools/swebench_bench.py              # 全量 300 题（约 1~2 min）
#   python3 tools/swebench_bench.py --limit 20
#   python3 tools/swebench_bench.py --model model_post3.bin
#
# 产物：_bench/swebench_results.jsonl、_bench/swebench_summary.md
import argparse
import json
import os
import re
import subprocess
import sys
import urllib.request

ROOT = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
OUT = os.path.join(ROOT, "_bench")
DATA = os.path.join(OUT, "data")
PARQUET = os.path.join(DATA, "swebench_lite.parquet")
JSONL = os.path.join(DATA, "swebench_lite.jsonl")
URL = "https://huggingface.co/datasets/princeton-nlp/SWE-bench_Lite/resolve/main/data/test-00000-of-00001.parquet"
SAFE_COLS = ["instance_id", "repo", "problem_statement"]   # ← 金补丁/测试/判据列**不读**

ABSTAIN = re.compile(r"(不太懂|不知道|没学过|不认识|没记过|不编|不会|真不会|不太会|没把握|拿不准|"
                     r"想想|好像|答不上|答不了|不确定|不太确定)")
FAKE_DONE = re.compile(r"(已修复|修好了|改好了|已经改|完成了|搞定了|fixed|done)", re.I)
PATCH_LIKE = re.compile(r"(^@@ |^--- |^\+\+\+ |diff --git )", re.M)
TAUGHT = re.compile(r"(记下了|教会了|学到)")


def fetch(url, path):
    """下载 parquet：直连失败换镜像（同 hle_bench：huggingface.co 常常 timeout，hf-mirror 通）。"""
    if os.path.exists(path) and os.path.getsize(path) > 1000:
        return path
    urls = [url]
    if "huggingface.co" in url:
        urls.append(url.replace("huggingface.co", "hf-mirror.com"))
    last = None
    for u in urls:
        try:
            req = urllib.request.Request(u, headers={"User-Agent": "aip-swebench"})
            with urllib.request.urlopen(req, timeout=120) as r, open(path, "wb") as f:
                f.write(r.read())
            return path
        except Exception as e:
            last = e
            print(f"[警告] {u} 失败（{e}）", flush=True)
    raise SystemExit(f"FAIL: 数据下载不了：{last}")

def prepare():
    """parquet → jsonl：**只写三列**。金补丁不落盘、不读入。"""
    if os.path.exists(JSONL) and os.path.getsize(JSONL) > 1000:
        return JSONL
    fetch(URL, PARQUET)
    rows = None
    try:                                   # duckdb 更小（21 MB），优先；读的列写死在 SQL 里
        import duckdb
        con = duckdb.connect()
        rows = [dict(zip(SAFE_COLS, r)) for r in con.execute(
            "SELECT instance_id, repo, problem_statement FROM read_parquet(?)", [PARQUET]
        ).fetchall()]
    except ImportError:
        try:
            import pyarrow.parquet as pq
            t = pq.read_table(PARQUET, columns=SAFE_COLS)   # ← 只读这三列
            rows = [(t.column(c)[i].as_py() for c in SAFE_COLS) for i in range(t.num_rows)]
        except ImportError:
            sys.exit("需要 duckdb 或 pyarrow 才能读 parquet（本机是 duckdb）：\n"
                     "  python3 -m pip install --user duckdb\n"
                     "（装不了就把 parquet 转成 JSONL 放到 _bench/data/swebench_lite.jsonl）")
    with open(JSONL, "w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
    print(f"[准备] {len(rows)} 题 → {JSONL}（只有 {'/'.join(SAFE_COLS)}）", flush=True)
    return JSONL


def ask(binary, model, text, max_line=1200):
    """issue 正文按 1200 字符切行喂入（引擎输入缓冲 4096 字节，中文按字节算，留余量），取最后一条回答。"""
    text = re.sub(r"\s+", " ", text).strip()
    lines = [text[i:i + max_line] for i in range(0, len(text), max_line)]
    stdin = "\n".join(lines) + "\n::quit\n"
    try:
        p = subprocess.run([binary, "--model", model], input=stdin, text=True,
                           capture_output=True, timeout=300)
        out = p.stdout
    except subprocess.TimeoutExpired:
        return ""
    reps = [l[l.find("AI > ") + 5:] for l in out.splitlines() if "AI > " in l]
    return (reps[-1] if reps else "").strip()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.path.join(ROOT, "model.bin"))
    ap.add_argument("--binary", default=os.path.join(ROOT, "build_aip"))
    ap.add_argument("--limit", type=int, default=0)
    ap.add_argument("--prepare", action="store_true")
    args = ap.parse_args()
    if args.prepare:
        prepare()
        return
    if not os.path.exists(JSONL):
        prepare()
    if not (os.path.exists(args.binary) and os.path.exists(args.model)):
        sys.exit("缺 build_aip 或 model.bin")
    tasks = [json.loads(l) for l in open(JSONL, encoding="utf-8")]
    if args.limit:
        tasks = tasks[:args.limit]
    os.makedirs(OUT, exist_ok=True)

    rows = []
    with open(os.path.join(OUT, "swebench_results.jsonl"), "w", encoding="utf-8") as f:
        for i, t in enumerate(tasks, 1):
            reply = ask(args.binary, args.model, t["problem_statement"])
            flat = re.sub(r"\s+", "", reply)
            r = {
                "instance_id": t["instance_id"], "repo": t["repo"],
                "patch_like": bool(PATCH_LIKE.search(reply)),
                "fake_done": bool(FAKE_DONE.search(reply)),
                "abstain": bool(ABSTAIN.search(flat)),
                "taught": bool(TAUGHT.search(flat)),
                "empty": not flat,
                "reply": reply[:300],
            }
            rows.append(r)
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
            if i % 25 == 0:
                print(f"  ... {i}/{len(tasks)} 补丁形 {sum(x['patch_like'] for x in rows)}", flush=True)

    n = len(rows)
    pl = sum(r["patch_like"] for r in rows)
    fd = sum(r["fake_done"] for r in rows)
    ab = sum(r["abstain"] for r in rows)
    em = sum(r["empty"] for r in rows)
    tg = sum(r["taught"] for r in rows)
    # 官方判分需要"补丁 + Docker 环境跑测试"；补丁为 0 ⇒ 没有任何东西可提交评测
    resolved = 0
    lines = [f"# SWE-bench Lite 基准 · {os.path.basename(args.model)}", "",
             f"- 数据：`princeton-nlp/SWE-bench_Lite`（公开 test split，共 300 题；本次跑 **{n}** 题"
             + (f"，`--limit {args.limit}` 截断，**不是全量数**" if args.limit else "") + "）",
             "- **只读三列**：`instance_id / repo / problem_statement`；金补丁、test_patch、FAIL_TO_PASS **没有读进内存**",
             "- 协议：一题一进程；不挂外挂、不教、不调参；判据写死（见下）", "",
             "| 项 | 数 |", "|---|---|",
             f"| 产出可应用补丁（unified diff） | **{pl}/{n}** |",
             f"| 假装改好了（\"已修复/完成\"） | {fd}/{n} |",
             f"| 认输（明说不会/不懂） | {ab}/{n} |",
             f"| 无回答 | {em}/{n} |",
             f"| 把 issue 当知识记下了 | {tg}/{n} |",
             f"| **resolved（官方定义）** | **{resolved}/{n}**（没有补丁就没有可评测对象）|", ""]
    lines += ["> 官方评测（`swebench.harness.run_evaluation`，Docker + 每仓库环境 + pytest）**没有运行**：",
              "> 本系统一个补丁都没产出，跑评测管道只会对空集合打分。这里**不伪造** resolved 分。", ""]
    text = "\n".join(lines)
    open(os.path.join(OUT, "swebench_summary.md"), "w", encoding="utf-8").write(text + "\n")
    print()
    print(text)


if __name__ == "__main__":
    main()
