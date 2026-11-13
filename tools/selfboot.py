#!/usr/bin/env python3
"""tools/selfboot.py — **差分自举**：用 Python oracle 当判据，自己发现缺口、自己起草算子、自己验证入库。

为什么要有：blind 系列证明“人喂一条才长一条”。这里换机制：
  ① oracle：对一组**语义在 Python 里写得清**的数组题，Python 给出期望输出（这就是判据）；
  ② 试跑：让图按描述合成，跑 5 组随机输入，和 oracle 比对；
  ③ 起草：不合格就按“描述→算子三元组”起草一条知识（语句来自小型模板库），灌进**模型副本**；
  ④ 复测：同一批用例再跑，全过才算学会；仍不过就记入 rejects（不猜、不硬答）。

用法：python3 tools/selfboot.py [--model code/code_model.bin] [--keep]
输出：每道题的 before/after 通过数、起草条数、拒绝条数；退出码 0 = 至少 10/12 学会。
"""
import argparse, glob, os, random, re, shutil, subprocess, sys, tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "_t/selfboot")

# ---------- ① oracle：数组题的中文描述 + Python 参考实现 + 输出格式 ----------
def _sum(v): return sum(v)
def _max(v): return max(v)
def _min(v): return min(v)
def _prod(v):
    r = 1
    for x in v: r *= x
    return r
def _count_pos(v): return sum(1 for x in v if x > 0)
def _count_even(v): return sum(1 for x in v if x % 2 == 0)
def _count_neg(v): return sum(1 for x in v if x < 0)
def _rev(v): return list(reversed(v))
def _sort_asc(v): return sorted(v)
def _sort_desc(v): return sorted(v, reverse=True)
def _abs_sum(v): return sum(abs(x) for x in v)
def _sum_sq(v): return sum(x * x for x in v)

CASES = [
    dict(desc="读入一行整数（标准输入），输出所有元素之和。", fn=_sum, fmt="int"),
    dict(desc="读入一行整数（标准输入），输出最大值。", fn=_max, fmt="int"),
    dict(desc="读入一行整数（标准输入），输出最小值。", fn=_min, fmt="int"),
    dict(desc="读入一行整数（标准输入），输出所有元素的乘积。", fn=_prod, fmt="int"),
    dict(desc="读入一行整数（标准输入），输出正数的个数。", fn=_count_pos, fmt="int"),
    dict(desc="读入一行整数（标准输入），输出偶数的个数。", fn=_count_even, fmt="int"),
    dict(desc="读入一行整数（标准输入），输出负数的个数。", fn=_count_neg, fmt="int"),
    dict(desc="读入一行整数（标准输入），倒序输出这些数。", fn=_rev, fmt="arr"),
    dict(desc="读入一行整数（标准输入），从小到大排序后输出。", fn=_sort_asc, fmt="arr"),
    dict(desc="读入一行整数（标准输入），从大到小排序后输出。", fn=_sort_desc, fmt="arr"),
    dict(desc="读入一行整数（标准输入），输出所有元素绝对值之和。", fn=_abs_sum, fmt="int"),
    dict(desc="读入一行整数（标准输入），输出所有元素平方和。", fn=_sum_sq, fmt="int"),
]

# ---------- ③ 起草模板库：描述关键词 → 算子语句（小、明确、可审） ----------
DRAFTS = {
    "元素之和": ("运算:元素之和", "{ long t2 = 0; for (int i = 0; i < n; i++) t2 += v[i]; x = t2; }", None),
    "输出最大值": ("运算:数组最大值2", "{ long m = v[0]; for (int i = 1; i < n; i++) if (v[i] > m) m = v[i]; x = m; }", None),
    "输出最小值": ("运算:数组最小值2", "{ long m = v[0]; for (int i = 1; i < n; i++) if (v[i] < m) m = v[i]; x = m; }", None),
    "从大到小排序后输出": ("运算:数组降序输出", "for (int i = 0; i < n; i++) for (int j = i + 1; j < n; j++) if (v[j] > v[i]) { long t2 = v[i]; v[i] = v[j]; v[j] = t2; } for (int i = 0; i < n; i++) printf(i ? \" %ld\" : \"%ld\", v[i]); printf(\"\\n\");", "打印:无"),
    "正数的个数": ("运算:正数个数", "{ long c = 0; for (int i = 0; i < n; i++) if (v[i] > 0) c++; x = c; }", None),
    "负数的个数": ("运算:负数个数2", "{ long c = 0; for (int i = 0; i < n; i++) if (v[i] < 0) c++; x = c; }", None),
    "所有元素的乘积": ("运算:数组乘积", "{ long t2 = 1; for (int i = 0; i < n; i++) t2 *= v[i]; x = t2; }", None),
    "绝对值之和": ("运算:绝对值和", "{ long t2 = 0; for (int i = 0; i < n; i++) t2 += v[i] < 0 ? -v[i] : v[i]; x = t2; }", None),
    "元素平方和": ("运算:元素平方和", "{ long t2 = 0; for (int i = 0; i < n; i++) t2 += v[i] * v[i]; x = t2; }", None),
    "偶数的个数": ("运算:偶数个数2", "{ long c = 0; for (int i = 0; i < n; i++) if (v[i] % 2 == 0) c++; x = c; }", None),
    "倒序输出这些数": ("运算:数组倒序输出", 'for (int i = n - 1; i >= 0; i--) printf(i == n - 1 ? "%ld" : " %ld", v[i]); printf("\\n");', "打印:无"),
}

def rand_case(rng):
    n = rng.randint(1, 6)
    return [rng.randint(-9, 9) for _ in range(n)]

def oracle_out(case, v):
    r = case["fn"](v)
    if case["fmt"] == "arr":
        return " ".join(str(x) for x in r)
    return str(r)

def clean_out(s):
    keep = []
    for ln in s.replace("\r", "").split("\n"):
        if re.match(r"^(ok|FAIL)\b", ln):
            continue
        keep.append(ln.rstrip())
    while keep and not keep[-1]:
        keep.pop()
    return "\n".join(keep)

def synth(model, desc):
    for f in glob.glob(os.path.join(WORK, "synth_*.c")) + glob.glob(os.path.join(WORK, "synth_*.out")):
        os.remove(f)
    inp = f"::act on\n::code synth {desc}\n::quit\n"
    p = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=inp.encode(), cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=180)
    out = p.stdout.decode("utf-8", "replace")
    files = sorted(glob.glob(os.path.join(ROOT, "_t/work/synth_*.c")), key=os.path.getmtime)
    outs = sorted(glob.glob(os.path.join(ROOT, "_t/work/synth_*.out")), key=os.path.getmtime)
    return (files[-1] if files else (outs[-1] if outs else None)), out

def run_tests(model, case, rng, tries=5):
    """合成 → 编 → 5 组随机输入跑 → 与 oracle 比。返回 (通过数, 总组数, 合成消息)"""
    path, msg = synth(model, case["desc"])
    if not path:
        return 0, tries, msg
    exe = os.path.join(WORK, "prog_" + str(abs(hash(case["desc"])) % 100000))
    if path.endswith(".out"):
        shutil.copyfile(path, exe); os.chmod(exe, 0o755)
    else:
        cc = subprocess.run(["gcc", "-std=c11", "-O2", "-o", exe, path, "-lm"],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if cc.returncode != 0:
            return 0, tries, msg + " 编译失败"
    ok = 0
    for _ in range(tries):
        v = rand_case(rng)
        stdin = " ".join(str(x) for x in v) + "\n"
        p = subprocess.run([exe], input=stdin.encode(), stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=15)
        got = clean_out(p.stdout.decode("utf-8", "replace"))
        want = oracle_out(case, v)
        if got == want:
            ok += 1
    return ok, tries, msg

def hijacker(msg):
    """从合成消息里找抢戏的单元（任务/配方/骨架）"""
    for pat in (r"命中任务 (任务:[^（(\s]+)", r"配方 (配方:[^）)\s]+)"):
        m = re.search(pat, msg or "")
        if m:
            return m.group(1)
    return None


def draft_triples(case):
    """给这道题起草一条算子三元组（关键词 = 描述里的动作短语）"""
    for phrase, (name, stmt, outnode) in DRAFTS.items():
        if phrase in case["desc"]:
            kws = {phrase}
            for junk in ("的", "所有", "全部", "每个", "一共"):
                kws |= {v.replace(junk, "") for v in list(kws)}
            kws = {v for v in kws if len(v) >= 2}
            lines = [f"{name}\t关键词\t{v}" for v in sorted(kws)] + [
                f"{name}\t模板\t模板:标准输入整数数组",
                f"{name}\t语句\t{stmt}",
            ]
            if outnode:
                lines.append(f"{name}\t输出\t{outnode}")
            return "\n".join(lines) + "\n", name
    return None, None

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--model", default=os.path.join(ROOT, "code/code_model.bin"))
    ap.add_argument("--keep", action="store_true")
    args = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    rng = random.Random(20261113)

    model = args.model if os.path.isabs(args.model) else os.path.join(ROOT, args.model)
    if not os.path.exists(model):
        print(f"FAIL: 缺模型 {model}"); return 2
    shutil.copyfile(model, os.path.join(WORK, "m.bin"))
    model = os.path.join(WORK, "m.bin")

    before = 0; after = 0; drafted = 0; rejected = 0
    print("== 自举前 ==")
    results = []
    for case in CASES:
        ok, tot, msg = run_tests(model, case, rng)
        results.append([case, ok, tot, msg])
        before += (ok == tot)
        print(f"  {'PASS' if ok == tot else 'MISS'}  {case['desc'][:28]}…  {ok}/{tot}")

    print("== 起草 + 复测 ==")
    for r in results:
        case, ok, tot, msg = r
        if ok == tot:
            after += 1
            continue
        tri, name = draft_triples(case)
        if not tri:
            rejected += 1
            print(f"  REJECT（没有模板）: {case['desc'][:28]}…")
            continue
        tf = os.path.join(WORK, "draft.triples")
        open(tf, "w", encoding="utf-8").write(tri)
        rel = os.path.relpath(tf, ROOT)   # ::ingest 只收**相对仓库根**的路径（绝对路径会被拒）
        inp = f"::ingest {rel}\n::save {model}\n::quit\n"
        subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=inp.encode(), cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=300)
        drafted += 1
        ok2, tot2, msg2 = run_tests(model, case, rng)
        # 还不够好：看是不是被别的单元抢戏（合成消息里有“命中任务/配方 X”），给它起草排除词
        if ok2 < tot2:
            hj = hijacker(msg2)
            if hj:
                phrase = next((ph for ph in DRAFTS if ph in case["desc"]), None)
                if phrase:
                    # 描述会去掉虚词（的/了…）⇒ 排除词两种写法都写，免得匹配不上（实测踩过）
                    variants = {phrase}
                    for junk in ("的", "所有", "全部", "每个", "一共"):
                        variants |= {v.replace(junk, "") for v in list(variants)}
                    variants = {v for v in variants if len(v) >= 2}
                    open(tf, "w", encoding="utf-8").write(
                        "".join(f"{hj}\t排除\t{v}\n" for v in variants))
                    subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                                   input=f"::ingest {os.path.relpath(tf, ROOT)}\n::save {model}\n::quit\n".encode(),
                                   cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
                    drafted += 1
                    ok2, tot2, _ = run_tests(model, case, rng)
                    print(f"    给 {hj} 起草排除词「{phrase}」后 {ok2}/{tot2}")
        r[1], r[2] = ok2, tot2
        if ok2 == tot2:
            after += 1
            print(f"  起草 {name} ⇒ {ok2}/{tot2} 学会")
        else:
            print(f"  起草 {name} ⇒ 仍 {ok2}/{tot2}（留作反例）")

    total = len(CASES)
    print(f"\n== 汇总 ==\n  自举前 {before}/{total}；自举后 {after}/{total}；起草 {drafted}；无模板拒绝 {rejected}")
    if not args.keep:
        pass
    return 0 if after >= 10 else 1

if __name__ == "__main__":
    sys.exit(main())
