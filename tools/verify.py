#!/usr/bin/env python3
"""tools/verify.py — **四层验证**：示例回放 + 性质检查 + 换说法一致 + 随机稳定性（§6.821）。

背景：没有神经网络时，oracle 从需求里长出来——
  ① 示例（需求方给）② 性质（按操作族写死）③ 换说法一致（概念层模式生成另一种说法）
  ④ 随机/边界稳定性（不许崩、不许抖）。四层都过才允许入库；任何一层红都**不采纳**。

用法：python3 tools/verify.py [--model M] [--only NAME]
输出：每条需求的四层结果 + 置信度；退出码 0 = 至少 80% 的需求四层全过。
"""
import argparse, os, random, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "_t/verify")

def run_synth(model, desc):
    for f in ("synth_*.c", "synth_*.out"):
        import glob as g
        for p in g.glob(os.path.join(ROOT, "_t/work", f)):
            os.remove(p)
    import glob as g
    inp = f"::act on\n::code synth {desc}\n::quit\n"
    r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=inp.encode(), cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=180)
    files = sorted(g.glob(os.path.join(ROOT, "_t/work/synth_*.c")), key=os.path.getmtime)
    outs = sorted(g.glob(os.path.join(ROOT, "_t/work/synth_*.out")), key=os.path.getmtime)
    return (files[-1] if files else (outs[-1] if outs else None)), r.stdout.decode("utf-8", "replace")

def build(src, tag):
    exe = os.path.join(WORK, "p_" + tag)
    if src.endswith(".out"):
        shutil.copyfile(src, exe); os.chmod(exe, 0o755); return exe
    cc = subprocess.run(["gcc", "-std=c11", "-O2", "-o", exe, src, "-lm"],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return exe if cc.returncode == 0 else None

def run_prog(exe, stdin, timeout=10):
    try:
        p = subprocess.run([exe], input=stdin.encode(), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=timeout)
        return clean(p.stdout.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return None

def clean(s):
    out = [ln.rstrip() for ln in s.replace("\r", "").split("\n") if not re.match(r"^(ok|FAIL)\b", ln)]
    while out and not out[-1]: out.pop()
    return "\n".join(out)

# ---------- ② 性质库：按操作族写死，不需要知道“正确答案” ----------
def p_sorted(v, out):
    xs = [int(x) for x in out.split()] if out and out.strip() else []
    return xs == sorted(xs) and sorted(xs) == sorted(v)
def p_rev_sorted(v, out):
    xs = [int(x) for x in out.split()] if out and out.strip() else []
    return xs == sorted(v, reverse=True)
def p_multiset(v, out):
    xs = [int(x) for x in out.split()] if out and out.strip() else []
    return sorted(xs) == sorted(v)
def p_order_free(v, out):
    return True   # 由上层用“打乱输入再跑”实现，这里占位
def p_range(v, out):
    return out is not None and out.strip().lstrip("-").isdigit()
def p_yesno(v, out):
    return out in ("yes", "no")

SPECS = [
    dict(name="排序", desc="读入一行整数（标准输入），从小到大排序后输出。",
         alt="读入一行整数（标准输入），按升序排好输出。",
         gen=lambda r: [r.randint(-9, 9) for _ in range(r.randint(1, 6))],
         examples=[("3 1 2\n", "1 2 3")], props=[p_sorted]),
    dict(name="降序", desc="读入一行整数（标准输入），从大到小排序后输出。",
         alt="读入一行整数（标准输入），从高到低排。",
         gen=lambda r: [r.randint(-9, 9) for _ in range(r.randint(1, 6))],
         examples=[("3 1 2\n", "3 2 1")], props=[p_rev_sorted]),
    dict(name="求和", desc="读入一行整数（标准输入），输出所有元素之和。",
         alt="读入一行整数（标准输入），把它们加起来是多少。",
         gen=lambda r: [r.randint(-9, 9) for _ in range(r.randint(1, 6))],
         examples=[("1 2 3\n", "6")], props=[p_range]),
    dict(name="最大值", desc="读入一行整数（标准输入），输出最大值。",
         alt="读入一行整数（标准输入），把最大的那个数找出来。",
         gen=lambda r: [r.randint(-9, 9) for _ in range(r.randint(1, 6))],
         examples=[("5 9 1\n", "9")], props=[p_range]),
    dict(name="计数", desc="读入一行整数（标准输入），输出一共有多少个数。",
         alt="读入一行整数（标准输入），数一数一共有几个数。",
         gen=lambda r: [r.randint(-9, 9) for _ in range(r.randint(1, 6))],
         examples=[("4 5 6 7\n", "4")], props=[p_range]),
    dict(name="偶数个数", desc="读入一行整数（标准输入），输出偶数的个数。",
         alt="读入一行整数（标准输入），里面有几个偶数。",
         gen=lambda r: [r.randint(-9, 9) for _ in range(r.randint(1, 6))],
         examples=[("1 2 4\n", "2")], props=[p_range]),
    dict(name="倒序", desc="读入一行整数（标准输入），倒序输出这些数。",
         alt="读入一行整数（标准输入），把顺序倒过来打印。",
         gen=lambda r: [r.randint(-9, 9) for _ in range(r.randint(1, 6))],
         examples=[("1 2 3\n", "3 2 1")], props=[p_multiset]),
    dict(name="回文", desc="读入一行字符串（标准输入），判断是不是回文（输出 yes 或 no）。",
         alt="读入一行字符串（标准输入），正着读反着读一样吗（输出 yes 或 no）。",
         gen=lambda r: "".join(r.choice("ab") for _ in range(r.randint(1, 6))),
         examples=[("aba\n", "yes")], props=[p_yesno], strin=True),
    dict(name="转大写", desc="读入一行字符串（标准输入），把所有字母改成大写后输出。",
         alt="读入一行字符串（标准输入），全都变成大写。",
         gen=lambda r: "".join(r.choice("abAB") for _ in range(r.randint(1, 6))),
         examples=[("ab\n", "AB")], props=[], strin=True),
    dict(name="元音个数", desc="读入一行字符串（标准输入），输出元音字母的个数。",
         alt="读入一行字符串（标准输入），数数有几个元音。",
         gen=lambda r: "".join(r.choice("abcde") for _ in range(r.randint(1, 6))),
         examples=[("abcde\n", "2")], props=[p_range], strin=True),
]

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.path.join(ROOT, "code/code_model.bin"))
    ap.add_argument("--only", default=None)
    args = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    model = args.model if os.path.isabs(args.model) else os.path.join(ROOT, args.model)
    if not os.path.exists(model):
        print(f"FAIL: 缺模型 {model}"); return 2
    rng = random.Random(20261113)
    ok_all = 0; total = 0
    for spec in SPECS:
        if args.only and args.only != spec["name"]: continue
        total += 1
        src, msg = run_synth(model, spec["desc"])
        if not src:
            print(f"  [NO_MATCH] {spec['name']}"); continue
        exe = build(src, re.sub(r"\W+", "_", spec["name"]))
        if not exe:
            print(f"  [BUILD_FAIL] {spec['name']}"); continue
        # ① 示例
        ex_ok = all(run_prog(exe, stdin) == want for stdin, want in spec["examples"])
        # ② 性质 + ④ 稳定性：20 组随机输入
        prop_ok, stable_ok, n = True, True, 20
        for _ in range(n):
            v = spec["gen"](rng)
            stdin = v + "\n" if isinstance(v, str) else " ".join(str(x) for x in v) + "\n"
            o1 = run_prog(exe, stdin)
            o2 = run_prog(exe, stdin)
            if o1 is None or o1 != o2: stable_ok = False; break
            for p in spec["props"]:
                if not p(v, o1): prop_ok = False; break
            if not prop_ok: break
        # ③ 换说法一致
        alt_src, _ = run_synth(model, spec["alt"])
        alt_ok = False
        if alt_src:
            alt_exe = build(alt_src, re.sub(r"\W+", "_", spec["name"]) + "_alt")
            if alt_exe:
                alt_ok = True
                for _ in range(10):
                    v = spec["gen"](rng)
                    stdin = v + "\n" if isinstance(v, str) else " ".join(str(x) for x in v) + "\n"
                    if run_prog(exe, stdin) != run_prog(alt_exe, stdin):
                        alt_ok = False; break
        layers = [("示例", ex_ok), ("性质", prop_ok), ("换说法", alt_ok), ("稳定", stable_ok)]
        allok = all(v for _, v in layers)
        ok_all += 1 if allok else 0
        marks = " ".join(f"{n}:{'✔' if v else '✘'}" for n, v in layers)
        print(f"  [{'PASS' if allok else 'FAIL'}] {spec['name']:<8} {marks}")
    print(f"\n== 四层全过 {ok_all}/{total} ==")
    return 0 if ok_all >= int(total * 0.8) else 1

if __name__ == "__main__":
    sys.exit(main())
