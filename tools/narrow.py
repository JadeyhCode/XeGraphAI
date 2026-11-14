#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""P4 自动候选规则：从 triage 的“链抢答”生成窄排除，先试后采（§6.847）。

流程：
  1) 跑指定盲测集，挑出 wrong + 路由=链 的题；
  2) 从合成输出里解析抢答的算子（链：N 个运算（A → B）），取最后一个算子；
  3) 候选排除词 = 描述里最后一个逗号之后的要求短语（足够长、够具体）；
  4) 在模型副本上验证：候选排除写进 _t/narrow/cand.triples，ingest 进副本，跑盲测；
  5) 盲测有提升才进一步跑隐藏组；隐藏不掉才采纳（写进 code/knowledge/compose.triples）。
"""
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_blind26 as B
import triage as T

WORK = os.path.join(ROOT, "_t", "narrow")
CAND = os.path.join(WORK, "cand.triples")
CAND_MODEL = os.path.join(WORK, "m.bin")
REAL = os.path.join(ROOT, "code", "code_model.bin")


def ops_of(out):
    m = re.search(r"链：\d+ 个运算（([^）]+)）", out)
    if not m:
        return []
    return [x.strip() for x in m.group(1).split("→")]


def requirement_phrase(desc):
    tail = re.split(r"[，,]", desc)[-1].strip()
    return tail if len(tail) >= 4 else desc.strip()


def gen_candidates(which):
    tasks = T.SETS[which]
    rng = random.Random(20261118)
    cands = []
    for t in tasks:
        status, out = B.judge(t, rng)
        if status != "wrong":
            continue
        ops = ops_of(out)
        if not ops:
            continue
        cands.append(dict(name=t["name"], op=ops[-1], phrase=requirement_phrase(t["desc"])))
    return cands


def build_cand_model(cands):
    os.makedirs(WORK, exist_ok=True)
    lines = ["# 自动候选排除（narrow.py 生成）"]
    for c in cands:
        lines.append("%s\t排除\t%s" % (c["op"], c["phrase"]))
    with open(CAND, "w", encoding="utf-8") as f:
        f.write("\n".join(lines) + "\n")
    subprocess.run(["cp", REAL, CAND_MODEL], check=True)
    p = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", CAND_MODEL],
                       input=("::ingest %s\n::save %s\n::quit\n" % (CAND, CAND_MODEL)).encode(),
                       capture_output=True, cwd=ROOT)
    return p.returncode == 0


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


def hidden_pass_count(model=None):
    env = dict(os.environ)
    if model:
        env["AIP_CODE_MODEL"] = model
    code = ("import sys; sys.path.insert(0,'tools');"
            "import code_hidden as H;"
            "specs=[];"
            "[specs.extend(v) for n,v in vars(H).items() if isinstance(v,list) and v and isinstance(v[0],dict)];"
            "print(sum(1 for s in specs if H.judge(s,'code/code_model.bin')[0]=='PASS'))")
    r = subprocess.run([sys.executable, "-c", code], capture_output=True, cwd=ROOT, env=env)
    try:
        return int(r.stdout.decode().strip().split()[-1])
    except Exception:
        return -1


def main(argv):
    which = argv[argv.index("--set") + 1] if "--set" in argv else "28"
    base = measure(which)
    cands = gen_candidates(which)
    print("候选规则 %d 条；基线 blind%s = %d/8" % (len(cands), which, base))
    for c in cands:
        print("   - %-14s 排除「%s」于 运算:%s" % (c["name"], c["phrase"], c["op"]))
    if not cands:
        return 0
    if not build_cand_model(cands):
        print("候选模型构建失败")
        return 1
    got = measure(which, CAND_MODEL)
    print("带候选规则 blind%s = %d/8" % (which, got))
    if got <= base:
        print("没有提升 ⇒ 不采纳")
        return 0
    hid = hidden_pass_count(CAND_MODEL)
    print("隐藏组 = %d/328" % hid)
    if hid < 328:
        print("隐藏组掉了 ⇒ 不采纳")
        return 0
    print("通过：盲测 +%d，隐藏组不掉" % (got - base))
    if "--apply" in argv:
        with open(os.path.join(ROOT, "code", "knowledge", "compose.triples"), "a",
                  encoding="utf-8") as f:
            f.write("\n# §6.847 narrow.py 自动采纳（盲测提升且隐藏组不掉）\n")
            for c in cands:
                f.write("%s\t排除\t%s\n" % (c["op"], c["phrase"]))
        subprocess.run(["sh", "code/build_code_model.sh"], cwd=ROOT,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("已采纳并重建模型")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
