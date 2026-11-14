#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""P4 采集分类：把盲测失败按“缺什么”分类，给出下一步动作（§6.845）。

用法：
  python3 tools/triage.py --set 28          # 跑 blind28，写 _t/triage/blind28.md
  python3 tools/triage.py --set 26 --json
"""
import json
import os
import random
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_search as S
import code_blind26 as B

SETS = {"26": S.BLIND26, "27": S.BLIND27, "28": S.BLIND28}


def route_of(out):
    if "链：" in out:
        return "链"
    if "命中任务" in out:
        return "任务"
    if "配方" in out:
        return "配方"
    return "无"


def classify(status, out):
    route = route_of(out)
    if status == "pass":
        return "已会", "—"
    if status == "refuse":
        if route == "无":
            return "缺知识（关键词/骨架都未命中）", "补关键词或骨架（selfserve/搜索器写回）"
        return "有路由但编/跑失败", "看 _t/work 产物：模板或语句不匹配"
    # wrong
    if route == "链":
        return "链抢答（语义串味）", "给该链加窄排除，或让位给任务/搜索器结果"
    if route == "任务":
        return "任务语义不符（旧任务太泛）", "给旧任务加排除，或补更具体的任务"
    return "配方语义不符", "收窄配方关键词/排除"


def main(argv):
    which = "28"
    if "--set" in argv:
        which = argv[argv.index("--set") + 1]
    tasks = SETS[which]
    rng = random.Random(20261117)
    rows = []
    cnt = {}
    for t in tasks:
        status, out = B.judge(t, rng)
        cause, action = classify(status, out)
        rows.append(dict(name=t["name"], status=status, route=route_of(out),
                         cause=cause, action=action))
        cnt[cause] = cnt.get(cause, 0) + 1
    if "--json" in argv:
        print(json.dumps(dict(set="blind" + which, rows=rows), ensure_ascii=False))
        return 0
    outdir = os.path.join(ROOT, "_t", "triage")
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "blind%s.md" % which)
    with open(path, "w", encoding="utf-8") as f:
        f.write("# blind%s 失败分类（P4）\n\n" % which)
        f.write("| 题 | 结果 | 路由 | 原因 | 建议动作 |\n|---|---|---|---|---|\n")
        for r in rows:
            f.write("| %s | %s | %s | %s | %s |\n" %
                    (r["name"], r["status"], r["route"], r["cause"], r["action"]))
        f.write("\n" + "、".join("%s×%d" % (k, v) for k, v in cnt.items()) + "\n")
    print("分类完成：", "、".join("%s×%d" % (k, v) for k, v in cnt.items()))
    print("报告：", os.path.relpath(path, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
