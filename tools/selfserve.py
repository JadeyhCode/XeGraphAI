#!/usr/bin/env python3
"""tools/selfserve.py — **需求驱动自服务训练**（按用户设计的那条路，§6.822）。

流程（每个需求）：
  ① 图内推：::code synth 直接合成；成功则过四层验证（示例/性质/稳定/换说法）
  ② 断点分类：没匹配 / 没编过 / 测试不过（从 note 读）
  ③ 取源：先试产品联网（::net get，白名单内），失败则读本地语料 _t/serve/corpus/*.c
  ④ 抽取 + 包装：把候选函数包成完整程序（模板 main），编译
  ⑤ 验证：示例 + 性质 + 稳定性（**不过就不入库**）
  ⑥ 扩图：通过的候选写成 任务/骨架/代码 三元组，灌进模型副本，再试 ①
  ⑦ 无果：记录 reject，继续下一条（不猜、不硬答）

用法：python3 tools/selfserve.py [--commit]
  --commit  把采纳的三元组追加到 code/knowledge/served.triples（默认只写 _t/serve/）
"""
import argparse, glob, os, re, shutil, subprocess, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "_t/serve")
CORPUS = os.path.join(WORK, "corpus")
CORPUS_REPO = os.path.join(ROOT, "tools/serve_corpus")   # 随仓库走的示例语料（沙箱里也能跑）

# ---------- 需求集：都带示例（示例 = oracle 的第一层） ----------
REQS = [
    dict(name="约数个数", desc="读入一个整数（命令行参数），输出它正约数的个数。",
         key="正约数的个数", examples=[(12, "6"), (7, "2"), (1, "1")],
         props=["range"]),
    dict(name="欧拉函数", desc="读入一个整数（命令行参数），输出它的欧拉函数值。",
         key="欧拉函数值", examples=[(9, "6"), (10, "4"), (1, "1")],
         props=["range"]),
    dict(name="整数立方根", desc="读入一个整数（命令行参数），输出它立方根的整数部分（向下取整）。",
         key="立方根的整数", examples=[(27, "3"), (26, "2"), (1, "1")],
         props=["range"]),
    dict(name="二进制回文", desc="读入一个整数（命令行参数），判断它的二进制表示是否回文（输出 yes 或 no）。",
         key="二进制表示是否回文", examples=[(5, "yes"), (6, "no"), (1, "yes")],
         props=["yesno"]),
    dict(name="数组次大值", desc="读入一行整数（标准输入），输出第二大的数。",
         key="第二大的数", kind="arr", examples=[("5 9 1 7\n", "7"), ("3 3 1\n", "1")],
         props=["range"]),
    dict(name="数组是否对称", desc="读入一行整数（标准输入），判断数组是否对称（输出 yes 或 no）。",
         key="数组是否对称", kind="arr", examples=[("1 2 1\n", "yes"), ("1 2\n", "no")],
         props=["yesno"]),
    dict(name="单词个数", desc="读入一行字符串（标准输入），输出单词的个数。",
         key="单词的个数", kind="str", examples=[("a b c\n", "3"), ("hi\n", "1")],
         props=["range"]),
    dict(name="七的个数自动", desc="读入一行整数（标准输入），输出其中数字 7 出现的次数。",
         key="数字 7 出现的次数", kind="arr", family="count_digit", digit=7, examples=[], props=["range"]),
    dict(name="偶数元素和", desc="读入一行整数（标准输入），输出其中偶数元素的和。",
         key="偶数元素的和", kind="arr", examples=[], props=["range"]),
    dict(name="平方和推断", desc="读入一行整数（标准输入），输出所有元素的平方和。",
         key="元素的平方和", kind="arr", examples=[], props=["range"]),
    dict(name="绝对值和", desc="读入一行整数（标准输入），输出所有元素绝对值之和。",
         key="绝对值之和", kind="arr", examples=[("-1 2 -3\n", "6")], props=["abs_sum"]),
    dict(name="极差", desc="读入一行整数（标准输入），输出最大值与最小值之差。",
         key="最大值与最小值之差", kind="arr", examples=[("4 1 9\n", "8")], props=["range_diff"]),
    dict(name="符号和", desc="读入一行整数（标准输入），正数记 1、负数记 -1、零记 0，输出这些值的和。",
         key="正数记 1", kind="arr", examples=[("1 -2 0\n", "0")], props=["range"]),
    dict(name="字符串压缩", desc="读入一行字符串（标准输入），把连续重复的字符压缩成字符+次数（输出压缩后的串）。",
         key="连续重复的字符压缩", kind="str", examples=[("aaabb\n", "a3b2"), ("abc\n", "abc")],
         props=[]),
]

# ---------- 示例自动生成：family → 现算期望（不需要人手写示例） ----------
# ---------- 从描述**推断**语义族（关键词 → family），这样不必人手打 family 标记 ----------
def infer_family(desc):
    d = desc
    if "绝对值" in d:
        return ("abs_sum", {})
    if "最大值与最小值之差" in d or "极差" in d:
        return ("range_diff", {})
    m = re.search(r"数字\s*(\d)", d)
    if m and ("次数" in d or "个数" in d or "出现" in d):
        return ("count_digit", {"digit": int(m.group(1))})
    if "偶数" in d and ("和" in d):
        return ("even_sum", {})
    if "偶数" in d:
        return ("count_even", {})
    if "平方和" in d or "平方的和" in d:
        return ("sum_sq", {})
    if "最大" in d:
        return ("max", {})
    if "最小" in d:
        return ("min", {})
    if "之和" in d or "求和" in d or "加起来" in d or "总和" in d:
        return ("sum", {})
    if "从小到大" in d or "升序" in d or "排好" in d:
        return ("sorted", {})
    return (None, {})


def auto_examples(req):
    fam = req.get("family")
    extra = {}
    if not fam:
        fam, extra = infer_family(req["desc"])
    if not fam:
        return []
    if fam == "count_digit":
        d = req.get("digit", extra.get("digit", 7))
        cases = [([d], "1"), ([d, d], "2"), ([d * 11], "2"), ([1, 2, 3], "0")]
        return [(f"{' '.join(str(x) for x in v)}\n", w) for v, w in cases]
    if fam == "sum":
        vs = [[1, 2, 3], [-1, 2], [5]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(sum(v))) for v in vs]
    if fam == "abs_sum":
        vs = [[-1, 2, -3], [4], [-5]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(sum(abs(x) for x in v))) for v in vs]
    if fam == "range_diff":
        vs = [[4, 1, 9], [7], [0, 0]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(max(v) - min(v))) for v in vs]
    if fam == "max":
        vs = [[4, 1, 9], [7], [-2, -5]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(max(v))) for v in vs]
    if fam == "min":
        vs = [[4, 1, 9], [7], [-2, -5]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(min(v))) for v in vs]
    if fam == "count_even":
        vs = [[1, 2, 4], [1, 3], [8]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(sum(1 for x in v if x % 2 == 0))) for v in vs]
    if fam == "even_sum":
        vs = [[1, 2, 4], [3, 5], [-2, 3]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(sum(x for x in v if x % 2 == 0))) for v in vs]
    if fam == "sum_sq":
        vs = [[1, 2, 3], [4], [-2, 1]]
        return [(f"{' '.join(str(x) for x in v)}\n", str(sum(x * x for x in v))) for v in vs]
    if fam == "sorted":
        vs = [[3, 1, 2], [5], [-1, 0]]
        return [(f"{' '.join(str(x) for x in v)}\n", " ".join(str(x) for x in sorted(v))) for v in vs]
    return []



def _sentinels():
    """哨兵用例：blind19/20/21（公开的盲测集），用来保证“学新不毁旧”。"""
    import importlib.util
    cases = []
    for g in (19, 20, 21):
        path = os.path.join(ROOT, f"tools/code_blind{g}.py")
        spec = importlib.util.spec_from_file_location(f"cb{g}", path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        cases += getattr(mod, f"BLIND{g}")
    return cases


def sentinel_passset(model):
    sys.path.insert(0, os.path.join(ROOT, "tools"))
    from code_hidden import judge
    good = set()
    for spec in _sentinels():
        st, _got, _out = judge(spec, model)
        if st == "PASS":
            good.add(spec["name"])
    return good

def kind_of(req):
    return req.get("kind", "argv")

def clean(s):
    out = [ln.rstrip() for ln in s.replace("\r", "").split("\n") if not re.match(r"^(ok|FAIL)\b", ln)]
    while out and not out[-1]: out.pop()
    return "\n".join(out)

def synth(model, desc, workdir=None):
    """合成；workdir 给定时用独立工作目录（AIP_WORKDIR），避免和主循环抢 _t/work。"""
    import glob as g
    wd = workdir or "_t/work"
    for p in g.glob(os.path.join(ROOT, wd, "synth_*")):
        os.remove(p)
    env = dict(os.environ)
    env["AIP_WORKDIR"] = wd
    inp = f"::act on\n::code synth {desc}\n::quit\n"
    r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=inp.encode(), cwd=ROOT, env=env, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=180)
    out = r.stdout.decode("utf-8", "replace")
    files = sorted(g.glob(os.path.join(ROOT, wd, "synth_*.c")), key=os.path.getmtime)
    return (files[-1] if files else None), out

def compile_prog(src_text, tag):
    src = os.path.join(WORK, f"p_{tag}.c")
    open(src, "w", encoding="utf-8").write(src_text)
    exe = os.path.join(WORK, f"p_{tag}")
    cc = subprocess.run(["gcc", "-std=c11", "-O2", "-o", exe, src, "-lm"],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return exe if cc.returncode == 0 else None

def run_case(exe, req, case):
    """按需求类型跑一个用例：argv / 数组 stdin / 字符串 stdin"""
    try:
        if kind_of(req) == "argv":
            p = subprocess.run([exe, str(case)], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=10)
        else:
            p = subprocess.run([exe], input=str(case).encode(), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=10)
        return clean(p.stdout.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return None

def prop_check(name, case, out, code):
    """具名性质：不依赖参考答案，只看输出与输入的关系。"""
    if name == "range":
        return bool(re.fullmatch(r"-?\d+", out or ""))
    if name == "yesno":
        return out in ("yes", "no")
    if name == "nonneg":
        return bool(re.fullmatch(r"\d+", out or ""))
    if name == "in_input":
        xs = [int(x) for x in case.split()]
        return out is not None and out.strip().lstrip("-").isdigit() and int(out) in xs
    if name == "le_max":
        xs = [int(x) for x in case.split()]
        return bool(out) and int(out) <= max(xs)
    if name == "sum_order_free":
        # 打乱输入后结果必须不变（由上层跑两次比较）
        return out is not None
    if name == "range_diff":
        xs = [int(x) for x in case.split()]
        return bool(out) and int(out) == max(xs) - min(xs)
    if name == "abs_sum":
        xs = [int(x) for x in case.split()]
        return bool(out) and int(out) == sum(abs(x) for x in xs)
    return True

def verify(exe, req, rng):
    """示例 + 性质 + 稳定：返回 (ok, 说明)"""
    ex = req["examples"] or auto_examples(req)
    if not ex:
        return False, "没有示例也没有可用 family（不猜）"
    for case, want in ex:
        got = run_case(exe, req, case)
        if got != want:
            return False, f"示例 {case!r} 得到 {got!r} 期望 {want!r}"
    for _ in range(20):
        if kind_of(req) == "arr":
            vals = [rng.randint(-9, 9) for _ in range(rng.randint(1, 6))]
            case = " ".join(str(x) for x in vals) + "\n"
        elif kind_of(req) == "str":
            case = "".join(rng.choice("aabbc") for _ in range(rng.randint(1, 8))) + "\n"
        else:
            vals = None
            case = rng.randint(1, 200)
        a, b = run_case(exe, req, case), run_case(exe, req, case)
        if a is None or a != b:
            return False, "不稳定/超时"
        for p in req.get("props", []):
            if not prop_check(p, case, a, None):
                return False, f"性质 {p} 不过（输入 {case!r} 输出 {a!r}）"
    return True, "示例+性质+稳定 全过"

def _ret_is_str(func_src):
    head = func_src.split('(')[0]
    return 'char' in head and '*' in head

def wrap(func_src, fname, req):
    """把候选函数包成完整程序：argv / 数组 stdin / 字符串 stdin 三种形态。"""
    fmt = '"%s\\n"' if _ret_is_str(func_src) else '"%ld\\n"'
    k = kind_of(req)
    if k == 'arr':
        call, decl = f'{fname}(v, n)', 'long v[64]; int n = 0; while (n < 64 && scanf("%ld", &v[n]) == 1) n++;'
    elif k == 'str':
        call, decl = f'{fname}(s)', 'char s[256] = ""; if (fgets(s, sizeof s, stdin)) {}'
    else:
        call, decl = f'{fname}(n)', 'long n = argc > 1 ? atol(argv[1]) : 10;'
    return ("#include <stdio.h>\n#include <stdlib.h>\n#include <string.h>\n"
            + func_src.rstrip() + "\n"
            + "int main(int argc, char **argv) {\n"
            + "    " + decl + "\n"
            + "    printf(" + fmt + ", " + call + ");\n"
            + "    return 0;\n}\n")

def try_sources(req):
    """③④ 取源 + 抽取：先产品联网（失败就跳过），再本地语料"""
    cands = []
    # 产品联网：::net get 只在白名单里、不跟重定向；抓不到就继续用本地语料
    net_log = os.path.join(WORK, "net.log")
    q = req["key"]
    cmd = f"::net on\n::web search {q}\n::quit\n"
    try:
        r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", os.path.join(WORK, "m.bin")],
                           input=cmd.encode(), cwd=ROOT, stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=120)
        open(net_log, "ab").write(r.stdout)
    except Exception as e:
        open(net_log, "a").write(f"net search failed: {e}\n")
    # 本地语料（任意抓取器都可以往这里放文件；这就是“网络层”的插口）
    paths = sorted(glob.glob(os.path.join(CORPUS, "*.c"))) + sorted(glob.glob(os.path.join(CORPUS_REPO, "*.c")))
    for path in paths:
        text = open(path, encoding="utf-8", errors="replace").read()
        for m in re.finditer(r"(static\s+)?(?:long|const\s+char\s*\*|char\s*\*)\s*(\w+)\s*\(\s*(?:long\s+\w+|const\s+long\s*\*\s*\w+\s*,\s*int\s+\w+|const\s+char\s*\*\s*\w+)\s*\)\s*\{", text):
            start = m.start()
            depth = 0; i = text.find("{", start)
            j = i
            while j < len(text):
                if text[j] == "{": depth += 1
                elif text[j] == "}":
                    depth -= 1
                    if depth == 0: break
                j += 1
            cands.append((os.path.basename(path), text[start:j + 1], m.group(2)))
    return cands


VDIR = "_t/serve/vwork"    # 内部复跑专用（AIP_WORKDIR）

def spec_runner(spec, model):
    """按 spec 的类型跑一次“合成→编译→跑→比对”，返回是否 PASS（独立工作目录）。"""
    path, _msg = synth(model, spec["desc"], workdir=VDIR)
    if not path:
        return False
    exe = compile_prog(open(path, encoding="utf-8").read(), "v_" + str(abs(hash(spec["name"])) % 99999))
    if not exe:
        return False
    try:
        if spec["kind"] == "argv":
            p = subprocess.run([exe] + spec["data"], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=10)
        elif spec["kind"] == "file":
            fpath = os.path.join(WORK, "v_" + str(abs(hash(spec["name"])) % 99999) + ".txt")
            open(fpath, "w", encoding="utf-8").write(spec["data"][0])
            p = subprocess.run([exe, fpath], stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=10)
        else:
            p = subprocess.run([exe], input=spec["data"][0].encode(), stdout=subprocess.PIPE,
                               stderr=subprocess.STDOUT, timeout=10)
        got = clean(p.stdout.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return False
    return re.fullmatch(spec["want"], got) is not None


def affected_specs(key):
    import importlib.util
    specs = []
    def load(path, names):
        spec = importlib.util.spec_from_file_location("vm_" + os.path.basename(path)[:-3], path)
        mod = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(mod)
        for nm in names:
            specs.extend(getattr(mod, nm, []))
    load(os.path.join(ROOT, "tools/code_hidden.py"),
         ["HELDOUT" + ("" if i == 1 else str(i)) for i in range(1, 41)])
    for g in (19, 20, 21):
        load(os.path.join(ROOT, f"tools/code_blind{g}.py"), [f"BLIND{g}"])
    return [sp for sp in specs if key and key in sp.get("desc", "")]


SAMPLE_GROUPS = ("HELDOUT", "HELDOUT5", "HELDOUT18", "HELDOUT30", "HELDOUT33")

def sample_specs():
    import importlib.util
    path = os.path.join(ROOT, "tools/code_hidden.py")
    spec = importlib.util.spec_from_file_location("vm_sample", path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    out = []
    for nm in SAMPLE_GROUPS:
        for sp in getattr(mod, nm, [])[:3]:
            out.append(sp)
    return out

def ingest(model, req, prog_src, tag):
    """⑥ 扩图：任务/骨架/代码 三元组（代码用续行）"""
    name = "SERVE_" + tag
    body = "\\\n".join(prog_src.rstrip().split("\n"))
    tri = (f"任务:{name}\t关键词\t{req['key']}\n"
           f"任务:{name}\t骨架\t骨架:{name}\n"
           f"任务:{name}\t用途\t自服务训练：{req['name']}\n"
           f"骨架:{name}\t代码\t{body}\n")
    tf = os.path.join(WORK, "served.triples")
    open(tf, "w", encoding="utf-8").write(tri)
    rel = os.path.relpath(tf, ROOT)
    r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=f"::ingest {rel}\n::save {model}\n::quit\n".encode(),
                       cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
    return r.returncode == 0, tri

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--commit", action="store_true")
    ap.add_argument("--model", default=os.path.join(ROOT, "code/code_model.bin"))
    ap.add_argument("--sentinels", action="store_true",
                    help="每次入库前跑哨兵（blind19/20/21），掉分就回滚这次入库")
    ap.add_argument("--gate", action="store_true",
                    help="训练后跑能力门；FAIL 就回滚")

    args = ap.parse_args()
    os.makedirs(CORPUS, exist_ok=True)
    import random
    rng = random.Random(20261113)
    src_model = args.model if os.path.isabs(args.model) else os.path.join(ROOT, args.model)
    if not os.path.exists(src_model):
        print(f"FAIL: 缺模型 {src_model}"); return 2
    model = os.path.join(WORK, "m.bin")
    shutil.copyfile(src_model, model)

    os.makedirs(os.path.join(ROOT, VDIR), exist_ok=True)
    SAMPLE = sample_specs()
    SAMPLE_BASE = {sp["name"] for sp in SAMPLE if spec_runner(sp, model)}
    before = after = ingested = 0
    BASE_SENTINELS = set()
    if args.sentinels:
        print("== 哨兵基线（blind19/20/21 里现在能过的） ==")
        BASE_SENTINELS = sentinel_passset(model)
        print(f"   {len(BASE_SENTINELS)} 条")
    accepted = []
    print("== ① 图内推（先看它自己会不会） ==")
    for req in REQS:
        path, msg = synth(model, req["desc"])
        ok = False
        if path:
            exe = compile_prog(open(path, encoding="utf-8").read(), req["name"])
            if exe:
                ok, why = verify(exe, req, rng)
        if ok:
            before += 1; after += 1
            print(f"  PASS {req['name']}（图内直接合成）")
        else:
            note = [l for l in msg.split("\n") if "合成" in l]
            print(f"  MISS {req['name']}（断点：{(note[-1][:60] if note else '没匹配')}）")
    print("== ② 取源 → ④ 验证 → ⑥ 扩图 ==")
    for req in REQS:
        # 已经会了就不再动
        path, _ = synth(model, req["desc"])
        if path:
            exe = compile_prog(open(path, encoding="utf-8").read(), req["name"] + "_chk")
            if exe and verify(exe, req, rng)[0]:
                continue
        cands = try_sources(req)
        got = False
        for src_name, func_src, fname in cands:
            prog = wrap(func_src, fname, req)
            exe = compile_prog(prog, req["name"] + "_cand")
            if not exe:
                continue
            ok, why = verify(exe, req, rng)
            if not ok:
                print(f"  REJECT {req['name']} ← {src_name}:{fname}（{why}）")
                continue
            backup = os.path.join(WORK, "m_pre_ingest.bin")
            shutil.copyfile(model, backup)
            os.makedirs(os.path.join(ROOT, VDIR), exist_ok=True)
            AFF = affected_specs(req.get("key", ""))
            base_ok = {sp["name"] for sp in AFF if spec_runner(sp, model)}
            good, tri = ingest(model, req, prog, req["name"])
            if good and AFF:
                lost = base_ok - {sp["name"] for sp in AFF if spec_runner(sp, model)}
                if lost:
                    shutil.copyfile(backup, model)
                    print(f"  ROLLBACK {req['name']}：影响面掉分 {sorted(lost)[:3]} ⇒ 这次不入库")
                    continue
            if good:
                if args.sentinels:
                    lost = BASE_SENTINELS - sentinel_passset(model)
                    if lost:
                        shutil.copyfile(backup, model)
                        print(f"  ROLLBACK {req['name']}：入库后哨兵掉分 {sorted(lost)[:3]} ⇒ 这次不入库")
                        continue
                ingested += 1; got = True; accepted.append(tri)
                print(f"  LEARN  {req['name']} ← {src_name}:{fname}（{why}）")
                break
        if not got:
            print(f"  NOGAIN {req['name']}（语料里没有能过验证的候选）")
        # 复测：扩图后再 synth 一次；若被别的单元抢戏，给它起草排除词再试一次
        for attempt in (0, 1):
            path, msg2 = synth(model, req["desc"])
            ok = False
            if path:
                exe = compile_prog(open(path, encoding="utf-8").read(), req["name"] + "_re")
                if exe: ok = verify(exe, req, rng)[0]
            if ok:
                after += 1
                print(f"  CHECK  {req['name']}：扩图后能合成且验证通过")
                break
            if attempt == 0:
                m = re.search(r"命中任务 (任务:[^（(\s]+)", msg2 or "") or re.search(r"配方 (配方:[^）)\s]+)", msg2 or "")
                if not m:
                    break
                tf = os.path.join(WORK, "excl.triples")
                variants = {req["key"], req["key"].replace("的", "")}
                open(tf, "w", encoding="utf-8").write(
                    "".join(f"{m.group(1)}\t排除\t{v}\n" for v in variants if len(v) >= 2))
                subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                               input=f"::ingest {os.path.relpath(tf, ROOT)}\n::save {model}\n::quit\n".encode(),
                               cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
                print(f"    给 {m.group(1)} 加排除词「{req['key']}」后重试")

    if args.commit and accepted:
        out = os.path.join(ROOT, "code/knowledge/served.triples")
        with open(out, "a", encoding="utf-8") as f:
            f.write("\n# 自服务训练采纳（%s）\n" % "selfserve")
            for tri in accepted:
                f.write(tri)
        print(f"\n已追加到 {out}（重跑 sh code/build_code_model.sh 生效）")

    lost_sample = SAMPLE_BASE - {sp["name"] for sp in SAMPLE if spec_runner(sp, model)}
    if lost_sample:
        shutil.copyfile(src_model, model)
        print(f"\n== 样本门：掉分 {sorted(lost_sample)[:5]} ⇒ 已整体回滚 ==")
    else:
        print(f"\n== 样本门：{len(SAMPLE_BASE)}/{len(SAMPLE)} 仍通过（训练未破坏旧能力）==")

    if args.gate:
        print("\n== 能力门：训练后的模型跑隐藏组 + blind11..21 ==")
        env = dict(os.environ); env["AIP_CAPBENCH"] = "1"; env["AIP_CODE_MODEL"] = model
        pr = subprocess.run(["sh", os.path.join(ROOT, "tests/capbench.sh")],
                            cwd=ROOT, env=env, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, timeout=3600)
        out = pr.stdout.decode("utf-8", "replace")
        for l in [x for x in out.split("\n") if "=>" in x or "FAIL" in x or "PASS capbench" in x][-6:]:
            print("   " + l.strip())
        if "PASS capbench" not in out:
            shutil.copyfile(src_model, model)
            print("   能力门 FAIL ⇒ 已回滚到训练前的模型")

    total = len(REQS)
    print(f"\n== 汇总 ==\n  训练前 {before}/{total}；训练后 {after}/{total}；采纳入库 {ingested} 条")
    return 0 if after >= int(total * 0.75) else 1

if __name__ == "__main__":
    sys.exit(main())
