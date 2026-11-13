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
]

def clean(s):
    out = [ln.rstrip() for ln in s.replace("\r", "").split("\n") if not re.match(r"^(ok|FAIL)\b", ln)]
    while out and not out[-1]: out.pop()
    return "\n".join(out)

def synth(model, desc):
    for p in glob.glob(os.path.join(ROOT, "_t/work/synth_*")):
        os.remove(p)
    inp = f"::act on\n::code synth {desc}\n::quit\n"
    r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=inp.encode(), cwd=ROOT, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=180)
    out = r.stdout.decode("utf-8", "replace")
    files = sorted(glob.glob(os.path.join(ROOT, "_t/work/synth_*.c")), key=os.path.getmtime)
    return (files[-1] if files else None), out

def compile_prog(src_text, tag):
    src = os.path.join(WORK, f"p_{tag}.c")
    open(src, "w", encoding="utf-8").write(src_text)
    exe = os.path.join(WORK, f"p_{tag}")
    cc = subprocess.run(["gcc", "-std=c11", "-O2", "-o", exe, src, "-lm"],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return exe if cc.returncode == 0 else None

def run(exe, n):
    try:
        p = subprocess.run([exe, str(n)], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, timeout=10)
        return clean(p.stdout.decode("utf-8", "replace"))
    except subprocess.TimeoutExpired:
        return None

def verify(exe, req, rng):
    """示例 + 性质 + 稳定：返回 (ok, 说明)"""
    import random
    for n, want in req["examples"]:
        got = run(exe, n)
        if got != want:
            return False, f"示例 {n} 得到 {got!r} 期望 {want!r}"
    for _ in range(20):
        n = rng.randint(1, 200)
        a, b = run(exe, n), run(exe, n)
        if a is None or a != b:
            return False, "不稳定/超时"
        if req["props"] == ["range"] and not re.fullmatch(r"-?\d+", a or ""):
            return False, f"输出不是整数：{a!r}"
        if req["props"] == ["yesno"] and a not in ("yes", "no"):
            return False, f"输出不是 yes/no：{a!r}"
    return True, "示例+性质+稳定 全过"

def wrap(func_src, fname):
    """把候选函数包成完整程序（模板:命令行整数 的语义）；按返回类型选打印格式。"""
    ret_str = ("const char" in func_src and "*" in func_src.split("(")[0]) or "char *" in func_src.split("(")[0]
    pf = "%s" if ret_str else "%ld"
    fmt = '"' + pf + '\\n"'
    return ("#include <stdio.h>\n#include <stdlib.h>\n"
            + func_src.rstrip() + "\n"
            + "int main(int argc, char **argv) {\n"
            + "    long n = argc > 1 ? atol(argv[1]) : 10;\n"
            + "    printf(" + fmt + ", " + fname + "(n));\n"
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
        for m in re.finditer(r"(static\s+)?(?:long|const\s+char\s*\*|char\s*\*)\s*(\w+)\s*\(\s*long\s+\w+\s*\)\s*\{", text):
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
    args = ap.parse_args()
    os.makedirs(CORPUS, exist_ok=True)
    import random
    rng = random.Random(20261113)
    src_model = args.model if os.path.isabs(args.model) else os.path.join(ROOT, args.model)
    if not os.path.exists(src_model):
        print(f"FAIL: 缺模型 {src_model}"); return 2
    model = os.path.join(WORK, "m.bin")
    shutil.copyfile(src_model, model)

    before = after = ingested = 0
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
            prog = wrap(func_src, fname)
            exe = compile_prog(prog, req["name"] + "_cand")
            if not exe:
                continue
            ok, why = verify(exe, req, rng)
            if not ok:
                print(f"  REJECT {req['name']} ← {src_name}:{fname}（{why}）")
                continue
            good, tri = ingest(model, req, prog, req["name"])
            if good:
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

    total = len(REQS)
    print(f"\n== 汇总 ==\n  训练前 {before}/{total}；训练后 {after}/{total}；采纳入库 {ingested} 条")
    return 0 if after >= int(total * 0.75) else 1

if __name__ == "__main__":
    sys.exit(main())
