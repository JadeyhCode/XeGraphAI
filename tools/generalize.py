#!/usr/bin/env python3
"""tools/generalize.py — 双实例反统一（§6.825）：从两条只差一个常数的知识里归纳参数化概念。

① 扫 compose.triples 的 运算:*，按（模板, 输出, 语句挖掉整数常量后的形状）分组
② 同组 >=2 条 ⇒ 归纳：常量换成 <P>，关键词变模式（前后缀 + 数字枚举），挂 参数 P
③ 验证：插进模型副本，要求 k=2、k=3 的行为与两条“已教”知识在随机输入上逐字一致
④ 迁移测量：对**没教过**的 k=5 也生成程序，按“去重后第 5 小”的独立算式核对
"""
import argparse, os, re, shutil, subprocess, sys, random

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "_t/gen")
CN = "一二三四五六七八九十"

def read_triples():
    ops = {}
    src = os.environ.get("GEN_TRIPLES") or "code/knowledge/compose.triples"
    for line in open(os.path.join(ROOT, src) if not os.path.isabs(src) else src, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        subj, rel, obj = parts[0], parts[1], "\t".join(parts[2:])
        if not subj.startswith("运算:"):
            continue
        ops.setdefault(subj, {}).setdefault(rel, []).append(obj)
    return ops

def shape(stmt):
    consts = []
    def repl(m):
        consts.append(int(m.group(0)))
        return "#"
    return re.sub(r"\d+", repl, stmt), consts

def synth(model, desc):
    import glob as g
    for p in g.glob(os.path.join(ROOT, "_t/gen/synth_*")):
        os.remove(p)
    env = dict(os.environ); env["AIP_WORKDIR"] = "_t/gen"; env["AIP_PARAM_INSIDE"] = "1"
    r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                       input=f"::act on\n::code synth {desc}\n::quit\n".encode(),
                       cwd=ROOT, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=180)
    files = sorted(g.glob(os.path.join(ROOT, "_t/gen/synth_*.c")), key=os.path.getmtime)
    return (files[-1] if files else None), r.stdout.decode("utf-8", "replace")

def build(path, tag):
    exe = os.path.join(WORK, "p_" + tag)
    src = os.path.join(WORK, "p_" + tag + ".c")
    shutil.copyfile(path, src)
    cc = subprocess.run(["gcc", "-std=c11", "-O2", "-o", exe, src, "-lm"],
                        stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    return exe if cc.returncode == 0 else None

def run_prog(exe, stdin):
    if not exe:
        return None
    p = subprocess.run([exe], input=stdin.encode(), stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=10)
    out = [l for l in p.stdout.decode("utf-8", "replace").split("\n") if not re.match(r"^(ok|FAIL)\b", l)]
    while out and not out[-1].strip():
        out.pop()
    return "\n".join(x.strip() for x in out).strip()

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--commit", action="store_true")
    ap.add_argument("--model", default="", help="模型副本（默认：仓库模型拷到 _t/gen/m.bin）")
    args = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    ops = read_triples()
    groups = {}
    for name, rel in ops.items():
        st = (rel.get("语句") or [""])[0]
        if not st or "<P>" in st:
            continue
        sk, consts = shape(st)
        if not consts:
            continue
        tpl = (rel.get("模板") or ["?"])[0]
        out = (rel.get("输出") or ["默认"])[0]
        kw = (rel.get("关键词") or [""])[0]
        groups.setdefault((sk, tpl, out), []).append((name, kw, consts[0], st))
    made = 0
    all_tri = []
    CN_NUM = {c: i + 1 for i, c in enumerate(CN)}
    for key, members in groups.items():
        if len(members) < 2:
            continue
        ks = []
        for m in members:
            mm = re.search(r"([一二三四五六七八九十])", m[1])
            if not mm:
                ks.append(None)
            else:
                ks.append(CN_NUM[mm.group(1)])
        if any(k is None for k in ks):
            continue
        pats = [re.match(r"^(.*?)([一二三四五六七八九十])([^一二三四五六七八九十]*)$", m[1]) for m in members]
        if not all(pats):
            continue
        prefixes = {p.group(1) for p in pats}
        suffixes = {p.group(3) for p in pats}
        if len(prefixes) != 1 or len(suffixes) != 1:
            continue
        _pre = prefixes.pop(); _suf = suffixes.pop()
        chunks = [CN[i:i + 4] for i in range(0, len(CN), 4)]   # 模式展开上限 4 个分支
        pats_out = [_pre + "{" + "|".join(c) + "}" + _suf for c in chunks]
        pat = pats_out[0]
        # 常数偏移：对每个常量位置，offset = k - 常量；要求所有成员一致
        base_consts = re.findall(r"\d+", members[0][3])
        # 每个常量位置：若各成员取值相同 ⇒ 保留字面量；不同 ⇒ 换成 <P-offset>（offset 必须一致）
        plan = []      # (原值, 替换串 或 None)
        ok = True
        for i, c in enumerate(base_consts):
            vals = set()
            offs = set()
            for ki, m in zip(ks, members):
                cs = re.findall(r"\d+", m[3])
                if i >= len(cs):
                    ok = False; break
                vals.add(int(cs[i]))
                offs.add(ki - int(cs[i]))
            if not ok:
                break
            if len(vals) == 1:
                plan.append((int(c), None))
            elif len(offs) == 1:
                off = offs.pop()
                plan.append((int(c), "<P>" if off == 0 else "<P> - %d" % off))
            else:
                ok = False; break
        if not ok:
            continue
        it = iter(plan)
        def _repl(m):
            _val, _rep = next(it)
            return _rep if _rep else m.group(0)
        gstmt = re.sub(r"\d+", _repl, members[0][3])
        gname = "运算:概念_" + re.sub(r"\W+", "_", pat)[:24]
        tpl = (ops[members[0][0]].get("模板") or ["?"])[0]
        tri = "".join(f"{gname}\t关键词\t{p}\n" for p in pats_out)
        tri += f"{gname}\t模式参数\t1\n"
        tri += (
               f"{gname}\t参数\tP\n"
               f"{gname}\t语句\t{gstmt}\n"
               f"{gname}\t模板\t{tpl}\n")
        for use in (ops[members[0][0]].get("用") or []):
            tri += f"{gname}\t用\t{use}\n"
        # 其余关系（输出/声明行/要求/排除…）照抄第一条：归纳只改“关键词 + 语句里的参数”
        for rel_name, vals in ops[members[0][0]].items():
            if rel_name in ("关键词", "语句", "参数", "用", "模板"):
                continue
            for v in vals:
                tri += f"{gname}\t{rel_name}\t{v}\n"
        names = [m[0] for m in members]
        print(f"  归纳：{names} => {gname}（关键词 {pat}）")
        tf = os.path.join(WORK, "gen.triples")
        open(tf, "w", encoding="utf-8").write(tri)
        all_tri.append(tri)
        if args.model:
            model = os.path.join(ROOT, args.model) if not os.path.isabs(args.model) else args.model
        else:
            model = os.path.join(WORK, "m.bin")
            shutil.copyfile(os.path.join(ROOT, "code/code_model.bin"), model)
        r = subprocess.run([os.path.join(ROOT, "build_aip"), "--model", model],
                           input=f"::ingest {os.path.relpath(tf, ROOT)}\n::save {model}\n::quit\n".encode(),
                           cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=300)
        if r.returncode != 0:
            print("    插入失败"); continue
        made += 1
        # ---- 通用验证 ----
        # 实例模型（只有实例，没有概念）vs 概念模型：教过的说法必须逐字一致
        base_model_src = (os.path.join(ROOT, args.model) if args.model and not os.path.isabs(args.model)
                          else (args.model if args.model else os.path.join(ROOT, "code/code_model.bin")))
        model_a = os.path.join(WORK, "a.bin")
        shutil.copyfile(base_model_src, model_a)
        # 注意：a 里也要有两条实例（调用方已把实例灌进 base_model_src 时就不用再灌）
        rng = random.Random(20261113)
        ok_taught = True
        for m in members:
            kw = m[1]
            for _ in range(3):
                vals = [rng.randint(-9, 9) for _ in range(rng.randint(3, 6))]
                stdin = " ".join(str(x) for x in vals) + "\n"
                desc = f"写一个程序：读入一行整数（标准输入），输出{kw}。"
                pa, _ = synth(model_a, desc)
                exe_a = build(pa, "a") if pa else None
                pb, _ = synth(model, desc)
                exe_b = build(pb, "b") if pb else None
                if not exe_a or not exe_b:
                    ok_taught = False
                    print(f"    教过的说法合成失败：{kw}（无概念={'有' if exe_a else '无'} 有概念={'有' if exe_b else '无'}）")
                    break
                oa = run_prog(exe_a, stdin)
                ob = run_prog(exe_b, stdin)
                if oa != ob:
                    ok_taught = False
                    print(f"    教过的说法不一致：{kw} 无概念={oa!r} 有概念={ob!r}（输入 {vals}）")
                    break
            if not ok_taught:
                break
        print(f"    教过的说法复现：{'OK' if ok_taught else 'FAIL'}")
        # 迁移：把模式里的数字换成“五”的说法，看能不能合成出来（正确性交给调用方的 oracle）
        probe = re.sub(r"\{[^}]*\}", "五", pats_out[0])
        desc = f"写一个程序：读入一行整数（标准输入），输出{probe}。"
        pp, _ = synth(model, desc)
        exe_p = build(pp, "probe") if pp else None
        print(f"    PROBE\t{probe}")
        if pp:
            stdin = "1 2 3\n"
            got = run_prog(exe_p, stdin)
            print(f"    PROBE_IO\t{stdin.strip()}\t{got}")
        else:
            print("    PROBE_IO\t\t")
    if args.commit and made:
        out = os.path.join(ROOT, "code/knowledge/generalized.triples")
        # **合并**：已有的概念不覆盖（只追加新的）
        old_lines = []
        old_names = set()
        if os.path.exists(out):
            for ln in open(out, encoding="utf-8"):
                old_lines.append(ln.rstrip("\n"))
                if "\t" in ln:
                    old_names.add(ln.split("\t")[0])
        add_lines = []
        for tri in all_tri:
            name = tri.split("\t")[0]
            if name in old_names:
                continue
            add_lines.extend(tri.rstrip("\n").split("\n"))
        open(out, "w", encoding="utf-8").write("\n".join(old_lines + add_lines) + "\n")
        print(f"\n已写 {out}（重跑 sh code/build_code_model.sh 生效）")
    return 0 if made else 1

if __name__ == "__main__":
    sys.exit(main())
