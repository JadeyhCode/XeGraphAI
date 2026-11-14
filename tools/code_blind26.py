#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""blind26：8 道「没教过但可拼」的题，量 C 管线（::code synth）的首跑。

与 tools/code_search.py 用同一套 gold 语义。
"""
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_search as S

TASKS = S.BLIND26


def synth(desc):
    os.makedirs(os.path.join(ROOT, "_t", "work"), exist_ok=True)
    for f in os.listdir(os.path.join(ROOT, "_t", "work")):
        if f.startswith("synth_"):
            os.remove(os.path.join(ROOT, "_t", "work", f))
    inp = "::act on\n::code synth %s\n::quit\n" % desc
    r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model",
                        os.path.join(ROOT, "code", "code_model.bin")],
                       input=inp.encode(), capture_output=True, timeout=180, cwd=ROOT)
    out = (r.stdout + r.stderr).decode("utf-8", "replace")
    # 不靠文件名（中文可能有 Unicode 规范化差异）：直接取工作目录里最新的 .c
    wdir = os.path.join(ROOT, "_t", "work")
    cands = [os.path.join(wdir, f) for f in os.listdir(wdir)
             if f.endswith(".c") and not f.endswith(".out")]
    cands = [p for p in cands if os.path.isfile(p)]
    if not cands:
        return None, out
    cands.sort(key=lambda p: os.path.getmtime(p))
    return cands[-1], out


def run_prog(src_path, data):
    exe = src_path[:-2]
    r = subprocess.run(["gcc", "-std=c11", "-O1", "-w", "-o", exe, src_path, "-lm"],
                       capture_output=True)
    if r.returncode != 0:
        return None
    try:
        run = subprocess.run([exe], input=data, capture_output=True, timeout=5)
    except subprocess.TimeoutExpired:
        return None
    return run.stdout.decode("utf-8", "replace")


def judge(task, rng, tries=4):
    path, out = synth(task["desc"])
    if not path:
        return "refuse", out
    ok_n = 0
    for _ in range(tries):
        if task["shape"] == "arr":
            v = [rng.randint(-9, 9) for _ in range(rng.randint(1, 7))]
            data = (" ".join(map(str, v)) + "\n").encode()
        else:
            v = "".join(rng.choice("abcXYZ") for _ in range(rng.randint(1, 6)))
            data = (v + "\n").encode()
        want = task["gold"](v)
        if want is None:      # 这种输入上任务无定义 ⇒ 跳过（不能算错）
            continue
        got = run_prog(path, data)
        if got is None:
            return "refuse", out
        if got.strip() != S.exp_str(want).strip():
            return "wrong", out
        ok_n += 1
    if ok_n == 0:
        return "refuse", out
    return "pass", out


def main():
    rng = random.Random(20261114)
    cnt = dict(pass_=0, refuse=0, wrong=0)
    for t in TASKS:
        got, out = judge(t, rng)
        cnt["pass_" if got == "pass" else got] += 1
        print("  [%s] %s" % (got.upper(), t["name"]))
    print("=> pass %d / refuse %d / wrong %d" % (cnt["pass_"], cnt["refuse"], cnt["wrong"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
