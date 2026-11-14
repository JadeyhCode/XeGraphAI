#!/usr/bin/env python3
"""tools/selfmanager.py — **自管理编排器**（§6.828）。

把“学什么 → 学 → 门禁 → 入库/回滚 → 日报”这条日常循环交给它自己跑，人只看报告。

纪律（每条都是踩过的坑）：
  ① **串行锁** `_t/manager.lock`：长跑共用 `_t/work`，并行必然读出假数（324/328 事件）；
  ② 每一步之后跑能力门；FAIL ⇒ 回滚到步骤前（模型 sha256 + 知识文件快照）；
  ③ 只改 `_t/` 下的模型副本，仓库模型不动；
  ④ 报告写 `_t/manager/report.jsonl`（机读）+ 标准输出（人读）。

用法：
  python3 tools/selfmanager.py --steps generalize            # 只做反统一+门禁
  python3 tools/selfmanager.py --steps selfserve:22 --gate   # 用 blind22 当需求学
  python3 tools/selfmanager.py --steps all --gate
"""
import argparse, json, os, shutil, subprocess, sys, time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "_t/manager")
LOCK = os.path.join(ROOT, "_t/manager.lock")

def log(msg):
    print(msg, flush=True)

def acquire_lock():
    os.makedirs(os.path.dirname(LOCK), exist_ok=True)
    if os.path.exists(LOCK):
        try:
            pid = int(open(LOCK).read().strip() or 0)
            os.kill(pid, 0)
            log(f"REFUSE: 已有编排在跑（pid={pid}，锁 {LOCK}）")
            return False
        except Exception:
            log("  发现陈旧锁，接管")
    open(LOCK, "w").write(str(os.getpid()))
    return True

def release():
    try:
        os.remove(LOCK)
    except OSError:
        pass

def sha(path):
    if not os.path.exists(path):
        return ""
    import hashlib
    h = hashlib.sha256()
    h.update(open(path, "rb").read())
    return h.hexdigest()

def snapshot():
    files = [os.path.join(ROOT, "code/code_model.bin"),
             os.path.join(ROOT, "code/knowledge/generalized.triples"),
             os.path.join(ROOT, "code/knowledge/compose.triples")]
    return {f: sha(f) for f in files}

def restore(snap):
    backup = os.path.join(WORK, "backup")
    os.makedirs(backup, exist_ok=True)
    for path in snap:
        dst = os.path.join(backup, os.path.basename(path))
        if os.path.exists(dst):
            shutil.copyfile(dst, path)

def do_backup(snap):
    backup = os.path.join(WORK, "backup")
    os.makedirs(backup, exist_ok=True)
    for path in snap:
        if os.path.exists(path):
            shutil.copyfile(path, os.path.join(backup, os.path.basename(path)))

def run(cmd, env=None, timeout=3600):
    e = dict(os.environ)
    if env:
        e.update(env)
    r = subprocess.run(cmd, cwd=ROOT, env=e, stdout=subprocess.PIPE,
                       stderr=subprocess.STDOUT, timeout=timeout)
    return r.returncode, r.stdout.decode("utf-8", "replace")

def gate():
    log("  [门禁] capbench …")
    rc, out = run(["sh", "tests/capbench.sh"], env={"AIP_CAPBENCH": "1"})
    ok = "PASS capbench" in out
    tail = [l for l in out.split("\n") if "FAIL" in l or "PASS capbench" in l]
    for l in tail[-4:]:
        log("    " + l.strip())
    return ok

def step_generalize(report):
    log("  [步骤] 反统一：扫“只差一个常数”的实例对并归纳（合并写）")
    rc, out = run(["python3", "tools/generalize.py", "--commit"])
    made = out.count("归纳：")
    report["generalize"] = {"rc": rc, "declared": made}
    log(f"    归纳出 {made} 个概念（rc={rc}）")
    if made == 0:
        return False
    rc2, out2 = run(["sh", "code/build_code_model.sh"], timeout=1800)
    log(f"    重建模型 rc={rc2}")
    return rc2 == 0

def step_selfserve(report, blind_no):
    log(f"  [步骤] 自服务训练：把 blind{blind_no} 当需求（期望输出当 oracle）")
    rc, out = run(["python3", "tools/selfserve.py", "--from-blind", str(blind_no)], timeout=3600)
    import re
    m = re.search(r"训练前 (\d+)/(\d+)；训练后 (\d+)/(\d+)", out)
    if m:
        report["selfserve"] = {"before": f"{m.group(1)}/{m.group(2)}",
                               "after": f"{m.group(3)}/{m.group(4)}"}
        log(f"    训练前 {m.group(1)}/{m.group(2)} ⇒ 训练后 {m.group(3)}/{m.group(4)}")
    else:
        report["selfserve"] = {"rc": rc}
    return rc == 0


def step_regress(report):
    """自动回归：清 _t/work → 串行跑整包 → 记 sha → 与基线比。"""
    log("  [步骤] 自动回归（清 _t/work、串行、记 sha）")
    for d in ("_t/work", "_t/par"):
        path = os.path.join(ROOT, d)
        if os.path.isdir(path):
            shutil.rmtree(path, ignore_errors=True)
    os.makedirs(os.path.join(ROOT, "_t/work"), exist_ok=True)
    sha_before = sha(os.path.join(ROOT, "code/code_model.bin"))
    rc, out = run(["sh", "tests/all.sh"], timeout=5400)
    ok = "全部回归通过" in out
    n = out.count("[OK]")
    report["regress"] = {"ok": ok, "suites": n, "model_sha": sha_before}
    log("    整包：%d 套（%s）；模型 sha=%s" % (n, "通过" if ok else "失败", sha_before[:12]))
    return ok


def step_blind(report, nos):
    """盲测失败清单 → 需求 → selfserve → 门禁 → 入库（失败如实记 NOGAIN）。"""
    import re
    for n in nos:
        log("  [步骤] 盲测 blind%d：先量失败清单" % n)
        rc, out = run(["python3", "tools/code_blind%d.py" % n], timeout=1800)
        fails = [l.strip() for l in out.split(chr(10)) if "[WRONG]" in l or "[REFUSE]" in l]
        before = re.search(r"=> pass (\d+) / refuse (\d+) / wrong (\d+)", out)
        log("    失败 %d 题；当前 %s" % (len(fails), before.group(0) if before else "?"))
        ok = step_selfserve(report, n)
        if ok:
            rc2, out2 = run(["python3", "tools/code_blind%d.py" % n], timeout=1800)
            after = re.search(r"=> pass (\d+) / refuse (\d+) / wrong (\d+)", out2)
            if after:
                report.setdefault("blind", {})["blind%d" % n] = {
                    "before": before.group(0) if before else "?", "after": after.group(0)}
                log("    复测：%s" % after.group(0))
    return True


def daily_log(report):
    """自动日报：今天学了什么、退了什么、盲测分数。"""
    day = time.strftime("%Y-%m-%d")
    path = os.path.join(WORK, "daily-%s.md" % day)
    lines = ["# 自管理日报 %s" % day, ""]
    for r in report.get("results", []):
        st = "OK" if r.get("ok") else ("回滚" if r.get("rolled_back") else "失败")
        lines.append("- 步骤 %s：%s" % (r.get("step"), st))
    for key, val in report.items():
        if key in ("results", "steps", "started", "finished"):
            continue
        lines.append("- %s：%s" % (key, json.dumps(val, ensure_ascii=False)))
    open(path, "w", encoding="utf-8").write(chr(10).join(lines) + chr(10))
    log("  日报：%s" % path)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--steps", default="generalize",
                    help="逗号分隔：generalize / selfserve:N / all")
    ap.add_argument("--gate", action="store_true", help="每步后跑能力门，FAIL 就回滚")
    ap.add_argument("--no-build", action="store_true")
    args = ap.parse_args()
    os.makedirs(WORK, exist_ok=True)
    if not acquire_lock():
        return 2
    try:
        if not os.path.exists(os.path.join(ROOT, "build_aip")):
            rc, _ = run(["gcc", "-std=c11", "-O2", "-o", "build_aip", "main.c", "-lm"], timeout=600)
            if rc != 0:
                log("FAIL: build_aip 编不出来")
                return 1
        if not os.path.exists(os.path.join(ROOT, "code/code_model.bin")):
            run(["sh", "code/build_code_model.sh"], timeout=1800)
        steps = []
        for s in args.steps.split(","):
            s = s.strip()
            if not s:
                continue
            if s == "all":
                steps += [("generalize", None), ("blind", [21, 22]), ("regress", None)]
            elif s.startswith("selfserve"):
                steps.append(("selfserve", int(s.split(":")[1]) if ":" in s else 21))
            else:
                steps.append((s, None))
        report = {"steps": args.steps, "started": time.time(), "results": []}
        for name, arg in steps:
            snap = snapshot()
            do_backup(snap)
            ok = False
            if name == "generalize":
                ok = step_generalize(report)
            elif name == "selfserve":
                ok = step_selfserve(report, arg)
            elif name == "regress":
                ok = step_regress(report)
            elif name == "blind":
                ok = step_blind(report, arg or [21, 22])
            else:
                log(f"  未知步骤 {name}")
                continue
            if not ok:
                log(f"  [回滚] 步骤 {name} 没产出，恢复快照")
                restore(snap)
                report["results"].append({"step": name, "ok": False, "rolled_back": True})
                continue
            if args.gate:
                if not gate():
                    log(f"  [回滚] 能力门 FAIL ⇒ 恢复 {name} 之前的模型与知识")
                    restore(snap)
                    report["results"].append({"step": name, "ok": False, "rolled_back": True})
                    continue
            report["results"].append({"step": name, "ok": True})
        report["finished"] = time.time()
        out = os.path.join(WORK, "report.jsonl")
        with open(out, "a", encoding="utf-8") as f:
            f.write(json.dumps(report, ensure_ascii=False) + "\n")
        daily_log(report)
        log(f"\n== 编排完成：{len([r for r in report['results'] if r.get('ok')])} 步成功 / "
            f"{len(report['results'])} 步（报告 {out}）==")
        return 0
    finally:
        release()

if __name__ == "__main__":
    sys.exit(main())
