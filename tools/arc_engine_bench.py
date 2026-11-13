#!/usr/bin/env python3
# tools/arc_engine_bench.py — **引擎内 ARC**（`::arc solve`）的分数：ARC-AGI-1 / ARC-AGI-2
#
# 和 tools/arc2_bench.py 的区别（**必须分清，否则又变成"外挂分数"**）：
#   · arc2_bench.py 测的是"把题文本化喂给对话接口"（原样、无网格通道）；
#   · 本工具测的是**引擎自己的 ARC 子系统**（arc.h + `::arc solve`）：变换表在图里、搜索在 C 里。
#   · 两者都是"系统自己"，都**不挂外挂**；区别只是用哪个入口。
#
# 协议（不作弊）：
#   1. 只把**输入**交给引擎（训练对的输入+输出，测试对的输入）；测试对的答案由本脚本在**判分**时比对。
#   2. 一题一进程，不教、不调参；判据 = 网格逐格相等。
#   3. 全量一次跑完（ARC-AGI-1: 400+400；ARC-AGI-2: 120）。
#
# 用法：
#   python3 tools/arc_engine_bench.py --set arc1-eval     # 400
#   python3 tools/arc_engine_bench.py --set arc1-train    # 400
#   python3 tools/arc_engine_bench.py --set arc2-eval     # 120
#   python3 tools/arc_engine_bench.py --set arc1-eval --limit 50
#
# 产物：_bench/arc_engine_<set>.jsonl、_bench/arc_engine_<set>.md
import argparse, json, os, re, subprocess, sys, tarfile, urllib.request

ROOT = os.path.dirname(os.path.abspath(os.path.dirname(__file__)))
OUT = os.path.join(ROOT, "_bench")
DATA = os.path.join(OUT, "data")
SETS = {
    "arc1-train": ("https://codeload.github.com/fchollet/ARC-AGI/tar.gz/refs/heads/master",
                   "ARC-AGI-master/data/training", "ARC-AGI-1-training"),
    "arc1-eval":  ("https://codeload.github.com/fchollet/ARC-AGI/tar.gz/refs/heads/master",
                   "ARC-AGI-master/data/evaluation", "ARC-AGI-1-evaluation"),
    "arc2-eval":  ("https://codeload.github.com/arcprize/ARC-AGI-2/tar.gz/refs/heads/main",
                   "ARC-AGI-2-main/data/evaluation", "ARC-AGI-2-evaluation"),
}


def ensure(url, member_prefix, name):
    d = os.path.join(DATA, name)
    if os.path.isdir(d) and len(os.listdir(d)) > 100:
        return d
    os.makedirs(DATA, exist_ok=True)
    tgz = os.path.join(DATA, name + ".tgz")
    print(f"[下载] {url}", flush=True)
    req = urllib.request.Request(url, headers={"User-Agent": "aip-arc-bench"})
    try:
        raw = urllib.request.urlopen(req, timeout=180).read()
    except Exception as e:
        print(f"[警告] TLS 校验失败（{e}），改用不校验下载", flush=True)
        import ssl
        raw = urllib.request.urlopen(req, timeout=180, context=ssl._create_unverified_context()).read()
    open(tgz, "wb").write(raw)
    os.makedirs(d, exist_ok=True)
    with tarfile.open(tgz) as t:
        for m in t.getmembers():
            if m.isfile() and m.name.startswith(member_prefix) and m.name.endswith(".json"):
                m.name = os.path.basename(m.name)
                t.extract(m, d)
    return d


def run_task(binary, model, path):
    """只把任务文件交给引擎；引擎自己看不到测试输出（它只读 input）。"""
    p = subprocess.run([binary, "--model", model], input=f"::arc solve {path}\n::quit\n",
                       text=True, capture_output=True, timeout=300)
    out = p.stdout
    solved = "解出：" in out
    how = ""
    m = re.search(r"解出：(.+?)（深度", out)
    if m:
        how = m.group(1)
    preds = re.findall(r"预测\d+: (\[[0-9/]+\])", out)
    return solved, how, preds, out


def parse_grid(s):
    s = s.strip("[]")
    return [list(map(int, row)) for row in s.split("/")]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--set", required=True, choices=sorted(SETS))
    ap.add_argument("--model", default=os.path.join(ROOT, "model.bin"))
    ap.add_argument("--binary", default=os.path.join(ROOT, "build_aip"))
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    url, prefix, name = SETS[args.set]
    d = ensure(url, prefix, name)
    files = sorted(f for f in os.listdir(d) if f.endswith(".json"))
    if args.limit:
        files = files[:args.limit]

    rows = []
    for i, fn in enumerate(files, 1):
        path = os.path.join(d, fn)
        task = json.load(open(path))
        solved, how, preds, out = run_task(args.binary, args.model, path)
        correct = False
        if solved and len(preds) == len(task["test"]):
            correct = all(parse_grid(pr) == te["output"] for pr, te in zip(preds, task["test"]))
        rows.append({"id": fn[:-5], "solved": solved, "correct": correct, "how": how,
                     "preds": preds, "tail": out.strip().splitlines()[-1][:120]})
        if i % 50 == 0:
            print(f"  ... {i}/{len(files)} 解出 {sum(r['solved'] for r in rows)} 正确 {sum(r['correct'] for r in rows)}", flush=True)

    n = len(rows); ok = sum(r["correct"] for r in rows); sv = sum(r["solved"] for r in rows)
    by = {}
    for r in rows:
        if r["correct"]:
            key = r["how"].split("∘")[0] if r["how"] else "?"
            by[key] = by.get(key, 0) + 1
    with open(os.path.join(OUT, f"arc_engine_{args.set}.jsonl"), "w", encoding="utf-8") as f:
        for r in rows:
            f.write(json.dumps(r, ensure_ascii=False) + "\n")
    lines = [f"# 引擎内 ARC · {args.set} · {os.path.basename(args.model)}", "",
             f"- 题数 **{n}**｜网格逐格相等 **{ok}**（{100.0*ok/n:.1f}%）｜引擎说「解出」 **{sv}**",
             f"- 解出的题按首个变换归类：`{json.dumps(by, ensure_ascii=False)}`",
             "- 协议：只喂训练对+测试输入；一题一进程；不教、不调参；判据=逐格相等", ""]
    text = "\n".join(lines)
    open(os.path.join(OUT, f"arc_engine_{args.set}.md"), "w", encoding="utf-8").write(text + "\n")
    print(); print(text)


if __name__ == "__main__":
    main()
