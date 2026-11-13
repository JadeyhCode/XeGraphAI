#!/usr/bin/env python3
"""tools/code_mutate.py — 把已教过的描述**系统性地改写**（插虚词/换同义词/去前缀），
统计通过率，用来量"同一批任务换说法"的鲁棒性（比单份手写盲测更有统计意义）。

用法：python3 tools/code_mutate.py [--limit N]
"""
import argparse, os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch

MODEL = os.path.join(ROOT, "code/code_model.bin")

MUTATORS = [
    ("去前缀", lambda d: d.replace("写一个程序：", "")),
    ("输出→打印", lambda d: d.replace("输出", "打印")),
    ("读入→输入", lambda d: d.replace("读入", "输入")),
    ("插虚词", lambda d: d.replace("输出", "输出一下", 1)),
    ("加礼貌语", lambda d: "请" + d[0:] if not d.startswith("请") else d),
    ("把→将", lambda d: d.replace("把", "将")),
    ("再→然后", lambda d: d.replace("再", "然后")),
]


def all_tasks():
    groups = [("battery", ch.BATTERY), ("heldout", ch.HELDOUT)]
    for name in ("HELDOUT2", "HELDOUT3", "HELDOUT4", "HELDOUT5", "HELDOUT6", "HELDOUT7",
                 "HELDOUT8", "HELDOUT9", "HELDOUT10", "HELDOUT11", "HELDOUT12", "HELDOUT13",
                 "HELDOUT14", "HELDOUT15", "HELDOUT16", "HELDOUT17", "HELDOUT18", "HELDOUT19",
                 "HELDOUT20", "HELDOUT21", "HELDOUT22", "HELDOUT23", "HELDOUT24", "HELDOUT25",
                 "HELDOUT26", "HELDOUT27", "HELDOUT28", "HELDOUT29", "HELDOUT30"):
        if hasattr(ch, name):
            groups.append((name.lower(), getattr(ch, name)))
    out = []
    for gname, tasks in groups:
        for t in tasks:
            out.append((gname, t))
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--limit", type=int, default=0)
    args = ap.parse_args()
    tasks = all_tasks()
    if args.limit:
        tasks = tasks[: args.limit]
    stats = {}
    fails = []
    for idx, (gname, t) in enumerate(tasks):
        label, fn = MUTATORS[idx % len(MUTATORS)]
        desc = fn(t["desc"])
        if desc == t["desc"]:
            continue
        spec = dict(t)
        spec["desc"] = desc
        status, got, out = ch.judge(spec, MODEL)
        s = stats.setdefault(label, {"pass": 0, "refuse": 0, "wrong": 0, "total": 0})
        s["total"] += 1
        if status == "PASS":
            s["pass"] += 1
        elif status == "NO_MATCH":
            s["refuse"] += 1
        else:
            s["wrong"] += 1
            fails.append((label, t["name"], desc, got, t["want"]))
    print("按改写方式：")
    tp = tr = tw = tt = 0
    for label, s in stats.items():
        print(f"  {label:12s} pass {s['pass']:2d} / refuse {s['refuse']:2d} / wrong {s['wrong']:2d}  (共 {s['total']})")
        tp += s["pass"]; tr += s["refuse"]; tw += s["wrong"]; tt += s["total"]
    print(f"合计：pass {tp} / refuse {tr} / wrong {tw}  （共 {tt} 变体）")
    if fails:
        print("错答样例：")
        for label, name, desc, got, want in fails[:10]:
            print(f"  [{label}] {name}: {desc}")
            print(f"      得到 {got!r} 期望 {want!r}")


if __name__ == "__main__":
    main()
