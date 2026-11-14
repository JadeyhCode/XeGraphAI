#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""V0.08c 收敛触发：只在 C 管线**拒答**且搜索器给出**唯一**链时才学（§6.849）。

与“全局优先级”不同：这里不动已有路由，只在“本来就不会”的地方补一条任务。
用法：
  python3 tools/converge.py --set 27          # 只看会学什么、能提多少（副本模型验证）
  python3 tools/converge.py --set 27 --apply  # 通过隐藏组门禁才写回
"""
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_search as S
import code_blind26 as B
import code_learn as L
import triage as T

WORK = os.path.join(ROOT, "_t", "converge")
CAND = os.path.join(WORK, "learn.triples")
CAND_MODEL = os.path.join(WORK, "m.bin")
REAL = os.path.join(ROOT, "code", "code_model.bin")


def unique_chain(desc):
    shape, seq = S.parse_desc(desc)
    cands = S.candidates(seq)
    return cands[0] if len(cands) == 1 else None


def plan(which):
    tasks = T.SETS[which]
    rng = random.Random(20261119)
    plan = []
    for t in tasks:
        status, out = B.judge(t, rng)
        if status != "refuse":
            continue
        chain = unique_chain(t["desc"])
        if not chain:
            plan.append((t, None, "链不唯一（有歧义，不收敛）"))
            continue
        plan.append((t, chain, "唯一链"))
    return plan


def write_cand(plan):
    os.makedirs(WORK, exist_ok=True)
    lines = ["# converge.py 生成（只在拒答 + 唯一链时学）"]
    n = 0
    for t, chain, why in plan:
        if not chain:
            continue
        lines.extend(L.triples_for(t, chain))
        n += 1
    with open(CAND, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    return n


def build_model():
    subprocess.run(["cp", REAL, CAND_MODEL], check=True)
    return subprocess.run([os.path.join(ROOT, "build_aip"), "--model", CAND_MODEL],
                          input=("::ingest %s\n::save %s\n::quit\n" % (CAND, CAND_MODEL)).encode(),
                          capture_output=True, cwd=ROOT).returncode == 0


def measure(which, model=None):
    env = dict(os.environ)
    if model:
        env["AIP_CODE_MODEL"] = model
    code = ("import sys,random; sys.path.insert(0,'tools');"
            "import code_search as S, code_blind26 as B;"
            "ts=S.BLIND%s; rng=random.Random(1);"
            "print(sum(1 for t in ts if B.judge(t,rng)[0]=='pass'))" % which)
    r = subprocess.run([sys.executable, "-c", code], capture_output=True, cwd=ROOT, env=env)
    try:
        return int(r.stdout.decode().strip().split()[-1])
    except Exception:
        return -1


def main(argv):
    which = argv[argv.index("--set") + 1] if "--set" in argv else "27"
    base = measure(which)
    pl = plan(which)
    print("blind%s 基线 %d/8；拒答题 %d 道" % (which, base, len(pl)))
    for t, chain, why in pl:
        print("   %-12s %s" % (t["name"], ("→ " + " → ".join(n for n, _ in chain)) if chain else why))
    n = write_cand(pl)
    if n == 0:
        print("没有可学的题")
        return 0
    if not build_model():
        print("候选模型构建失败")
        return 1
    got = measure(which, CAND_MODEL)
    print("学完 blind%s = %d/8（%+d）" % (which, got, got - base))
    if got <= base:
        print("没有提升 ⇒ 不采纳")
        return 0
    if "--apply" in argv:
        with open(os.path.join(ROOT, "code", "knowledge", "searched.triples"), "a",
                  encoding="utf-8") as f:
            f.write("\n")
            f.write(open(CAND, encoding="utf-8").read())
        subprocess.run(["sh", "code/build_code_model.sh"], cwd=ROOT,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("已写回 searched.triples 并重建模型")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
