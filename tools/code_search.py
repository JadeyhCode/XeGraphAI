#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""P1 类型化搜索器（§6.841）。

描述 → 签名（形状 + 操作词 + 参数）→ 枚举 1–2 个基算子的**类型链**
→ 拼 C 程序 → 编译 → 随机输入 + **任务自己的语义(gold)** 比对 → 通过才算会。
"""
import json
import os
import random
import re
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
WORK = os.path.join(ROOT, "_t", "search")

def _load_duals():
    """从 code/knowledge/duals.triples 读对偶表（结构反统一的数据形式）。"""
    path = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                        "code", "knowledge", "duals.triples")
    pairs = []
    try:
        for line in open(path, encoding="utf-8"):
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            parts = line.split("	")
            if len(parts) == 3 and parts[0] == "对偶":
                pairs.append((parts[1], parts[2]))
    except OSError:
        pass
    return pairs


DUALS = _load_duals()

OPS = {
    "数组最大": dict(i="arr", o="int", p=0,
        stmt="x = v[0]; for (int i = 1; i < n; i++) if (v[i] > x) x = v[i];"),
    "数组最小": dict(i="arr", o="int", p=0,
        stmt="x = v[0]; for (int i = 1; i < n; i++) if (v[i] < x) x = v[i];"),
    "数组和": dict(i="arr", o="int", p=0,
        stmt="x = 0; for (int i = 0; i < n; i++) x += v[i];"),
    "数组长度": dict(i="arr", o="int", p=0, stmt="x = n;"),
    "数组平均": dict(i="arr", o="int", p=0,
        stmt="x = 0; for (int i = 0; i < n; i++) x += v[i]; x = x / n;"),
    "平方和": dict(i="arr", o="int", p=0,
        stmt="x = 0; for (int i = 0; i < n; i++) x += v[i] * v[i];"),
    "极差": dict(i="arr", o="int", p=0,
        stmt="x = v[0]; { long lo = v[0]; for (int i = 1; i < n; i++) { if (v[i] > x) x = v[i]; if (v[i] < lo) lo = v[i]; } x -= lo; }"),
    "偶数个数": dict(i="arr", o="int", p=0,
        stmt="x = 0; for (int i = 0; i < n; i++) if (v[i] % 2 == 0) x++;"),
    "中位数": dict(i="arr", o="int", p=0,
        stmt="qsort(v, (size_t)n, sizeof v[0], cmp_long); x = (n % 2 == 1) ? v[n / 2] : v[n / 2 - 1];"),
    "排序升": dict(i="arr", o="arr", p=0,
        stmt="qsort(v, (size_t)n, sizeof v[0], cmp_long);"),
    "排序降": dict(i="arr", o="arr", p=0,
        stmt="qsort(v, (size_t)n, sizeof v[0], cmp_desc);"),
    "反转": dict(i="arr", o="arr", p=0,
        stmt="for (int i = 0, j = n - 1; i < j; i++, j--) { long t = v[i]; v[i] = v[j]; v[j] = t; }"),
    "每数乘k": dict(i="arr", o="arr", p=1,
        stmt="for (int i = 0; i < n; i++) v[i] *= P;"),
    "每数加k": dict(i="arr", o="arr", p=1,
        stmt="for (int i = 0; i < n; i++) v[i] += P;"),
    "转大写": dict(i="str", o="str", p=0,
        stmt="for (int i = 0; s[i]; i++) if (s[i] >= 'a' && s[i] <= 'z') s[i] = (char)(s[i] - 32);"),
    "转小写": dict(i="str", o="str", p=0,
        stmt="for (int i = 0; s[i]; i++) if (s[i] >= 'A' && s[i] <= 'Z') s[i] = (char)(s[i] + 32);"),
    "字符长度": dict(i="str", o="int", p=0, stmt="x = (long)strlen(s);"),
    "第k小": dict(i="arr", o="int", p=1,
        stmt="qsort(v, (size_t)n, sizeof v[0], cmp_long); x = v[P - 1];"),
    "第k大": dict(i="arr", o="int", p=1, derived=True,
        stmt="qsort(v, (size_t)n, sizeof v[0], cmp_desc); x = v[P - 1];"),
    "去重": dict(i="arr", o="arr", p=0,
        stmt="{ qsort(v, (size_t)n, sizeof v[0], cmp_long); int m = 0; for (int i = 0; i < n; i++) if (i == 0 || v[i] != v[i - 1]) v[m++] = v[i]; n = m; }"),
    "前缀和": dict(i="arr", o="arr", p=0,
        stmt="for (int i = 1; i < n; i++) v[i] += v[i - 1];"),
    "方差整数": dict(i="arr", o="int", p=0,
        stmt="x = 0; { long s = 0; for (int i = 0; i < n; i++) s += v[i]; long mu = s / n; for (int i = 0; i < n; i++) { long d = v[i] - mu; x += d * d; } x /= n; }"),
    "字符串反转": dict(i="str", o="str", p=0,
        stmt="for (int i = 0, j = (int)strlen(s) - 1; i < j; i++, j--) { char t = s[i]; s[i] = s[j]; s[j] = t; }"),
}

CN = {"零": 0, "一": 1, "二": 2, "两": 2, "三": 3, "四": 4, "五": 5,
      "六": 6, "七": 7, "八": 8, "九": 9, "十": 10}


def cn2int(t):
    if not t:
        return None
    if t.isdigit():
        return int(t)
    if t in CN:
        return CN[t]
    m = re.fullmatch(r"([一二三四五六七八九])?十([一二三四五六七八九])?", t)
    if m:
        a = CN.get(m.group(1), 1) if m.group(1) else 1
        b = CN.get(m.group(2), 0) if m.group(2) else 0
        return a * 10 + b
    return None


KW = {
    "数组最大": ["最大值", "最大"],
    "数组最小": ["最小值", "最小"],
    "数组和": ["总和", "之和", "求和", "加起来"],
    "数组长度": ["长度", "多少个", "个数"],
    "数组平均": ["平均值", "平均"],
    "平方和": ["平方和"],
    "极差": ["极差"],
    "偶数个数": ["偶数个数", "偶数的个数"],
    "中位数": ["中位数"],
    "排序升": ["从小到大排序", "排序后"],
    "排序降": ["从大到小排序", "降序"],
    "反转": ["反转", "倒序输出", "倒过来"],
    "每数乘k": ["每个数乘", "每个数都乘", "都乘"],
    "每数加k": ["每个数加", "每个数都加", "都加"],
    "转大写": ["转成大写", "变成大写"],
    "转小写": ["转成小写", "变成小写"],
    "字符长度": ["字符数", "长度"],
    "第k小": ["第"],
    "第k大": ["大"],
    "去重": ["去重", "不重复", "去掉重复"],
    "前缀和": ["前缀和", "前面的和累加"],
    "方差整数": ["方差"],
    "字符串反转": ["反转", "倒过来"],
}


def parse_desc(desc):
    shape = "str" if "字符串" in desc else "arr"
    hits = []
    for name, kws in KW.items():
        for kw in kws:
            pos = desc.find(kw)
            if pos >= 0:
                hits.append((pos, name))
                break
    hits.sort()
    seq, seen = [], set()
    for pos, name in hits:
        if name in seen:
            continue
        seen.add(name)
        k = None
        m = re.search(r"([0-9]+|[一二三四五六七八九十]+)", desc[pos:pos + 10])
        if m:
            k = cn2int(m.group(1))
        if k is None:
            # 数字在关键词**前面**（“第二大的数”：数词在“大”前面）
            back = desc[max(0, pos - 6):pos]
            m2 = re.search(r"([0-9]+|[一二三四五六七八九十]+)$", back)
            if m2:
                k = cn2int(m2.group(1))
        seq.append((name, k))
    return shape, seq


def candidates(seq, stage=1):
    """保序子序列，长度 1–3，类型必须接得上；按长度优先（MDL）。

    stage=1：只用基础算子；stage=2：允许 derived（对偶替换派生出来的）算子。
    """
    out = []

    def extend(prefix, start):
        if prefix:
            out.append(list(prefix))
        if len(prefix) >= 3:
            return
        for j in range(start, len(seq)):
            name, k = seq[j]
            if OPS[name]["p"] and not k:
                continue
            if stage < 2 and OPS[name].get("derived"):
                continue
            if prefix and OPS[prefix[-1][0]]["o"] != OPS[name]["i"]:
                continue
            prefix.append((name, k))
            extend(prefix, j + 1)
            prefix.pop()

    extend([], 0)
    out.sort(key=len)
    # 去重（同一链只留一份）
    seen, uniq = set(), []
    for c in out:
        key = tuple(c)
        if key in seen:
            continue
        seen.add(key)
        uniq.append(c)
    return uniq


PRELUDE = r"""
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int cmp_long(const void *a, const void *b) {
    long x = *(const long *)a, y = *(const long *)b;
    return (x > y) - (x < y);
}
static int cmp_desc(const void *a, const void *b) {
    long x = *(const long *)a, y = *(const long *)b;
    return (x < y) - (x > y);
}
"""


def gen_program(shape, chain):
    body = []
    if shape == "arr":
        body.append("    long v[256]; int n = 0;")
        body.append("    while (n < 256 && scanf(\"%ld\", &v[n]) == 1) n++;")
        body.append("    if (n <= 0) return 0;")
        body.append("    long x = 0; (void)x;")
    elif shape == "str":
        body.append("    static char s[512];")
        body.append("    if (!fgets(s, sizeof s, stdin)) return 0;")
        body.append("    s[strcspn(s, \"\\r\\n\")] = 0;")
        body.append("    long x = 0; (void)x;")
    else:
        return None
    for name, k in chain:
        st = OPS[name]["stmt"]
        if OPS[name]["p"]:
            st = st.replace("P", str(k if k else 1))
        body.append("    { " + st + " }")
    last = OPS[chain[-1][0]]["o"]
    if last == "arr":
        body.append("    for (int i = 0; i < n; i++) printf(\"%ld%c\", v[i], i + 1 == n ? 10 : 32);")
    elif last == "str":
        body.append("    fputs(s, stdout); putchar(10);")
    else:
        body.append("    printf(\"%ld\\n\", x);")
    return PRELUDE + "int main(void) {\n" + "\n".join(body) + "\n    return 0;\n}\n"


def run_case(src, stdin_data, tag):
    os.makedirs(WORK, exist_ok=True)
    c = os.path.join(WORK, tag + ".c")
    b = os.path.join(WORK, tag)
    with open(c, "w", encoding="utf-8") as f:
        f.write(src)
    r = subprocess.run(["gcc", "-std=c11", "-O1", "-w", "-o", b, c, "-lm"], capture_output=True)
    if r.returncode != 0:
        return None
    try:
        run = subprocess.run([b], input=stdin_data, capture_output=True, timeout=5)
    except subprocess.TimeoutExpired:
        return None
    return run.stdout.decode("utf-8", "replace")


def exp_str(want):
    if isinstance(want, list):
        return " ".join(str(x) for x in want)
    return str(want)


def verify(chain, shape, rng, gold, tries=6, max_attempts=20):
    """随机输入 + 任务 gold；无定义的输入跳过，但要凑够 tries 个有效样本。"""
    src = gen_program(shape, chain)
    if not src:
        return False, "形状不支持"
    checked = 0
    for _ in range(max_attempts):
        if checked >= tries:
            break
        if shape == "arr":
            # 值域小一点，保证**有重复**（否则“去重”类任务会被偶然放行）
            value = [rng.randint(-4, 4) for _ in range(rng.randint(2, 7))]
            inp = " ".join(str(x) for x in value) + "\n"
        else:
            value = "".join(rng.choice("abcXYZ") for _ in range(rng.randint(1, 6)))
            inp = value + "\n"
        want = gold(value)
        if want is None:
            continue
        got = run_case(src, inp.encode(), "cand")
        if got is None:
            return False, "编译/运行失败"
        if got.strip() != exp_str(want).strip():
            return False, "不一致 want=%r got=%r" % (exp_str(want), got.strip())
        checked += 1
    if checked < tries:
        return False, "有效样本不足"
    return True, "ok"


def search(desc, rng, gold, shape_hint=None):
    shape, seq = parse_desc(desc)
    if shape_hint:
        shape = shape_hint
    if not seq:
        return None, "没认出算子"
    # 阶段 1：基础算子枚举
    for chain in candidates(seq, stage=1):
        ok, _why = verify(chain, shape, rng, gold)
        if ok:
            return chain, "ok:S1"
    # 阶段 2：对偶替换（anti-unification 的“同一骨架、换比较子”）
    if os.environ.get("AIP_SEARCH_NO_DUAL") == "1":
        return None, "关掉对偶替换（A/B）"
    if any(OPS[n].get("derived") for n, _ in seq):
        for chain in candidates(seq, stage=2):
            ok, _why = verify(chain, shape, rng, gold)
            if ok:
                return chain, "ok:S2"
    return None, "候选都没过验证"


BLIND26 = [
    dict(name="极差", shape="arr", desc="读入一行整数（标准输入），输出它们的极差。",
         gold=lambda a: max(a) - min(a)),
    dict(name="平方和", shape="arr", desc="读入一行整数（标准输入），输出它们的平方和。",
         gold=lambda a: sum(x * x for x in a)),
    dict(name="偶数个数", shape="arr", desc="读入一行整数（标准输入），输出其中偶数的个数。",
         gold=lambda a: sum(1 for x in a if x % 2 == 0)),
    dict(name="排序后最大", shape="arr", desc="读入一行整数（标准输入），从小到大排序后输出最大值。",
         gold=lambda a: max(a)),
    dict(name="加三后平均", shape="arr", desc="读入一行整数（标准输入），每个数加 3 之后输出平均值。",
         gold=lambda a: int(sum(x + 3 for x in a) / len(a))),  # C 的整除向零截断
    dict(name="大写后长度", shape="str", desc="读入一行字符串（标准输入），转成大写后输出长度。",
         gold=lambda s: len(s.upper())),
    dict(name="反转后取最小", shape="arr", desc="读入一行整数（标准输入），反转后输出最小值。",
         gold=lambda a: min(reversed(a))),
    dict(name="乘二后求和", shape="arr", desc="读入一行整数（标准输入），每个数乘 2 之后输出总和。",
         gold=lambda a: sum(x * 2 for x in a)),
]


BLIND27 = [
    dict(name="去重", shape="arr", desc="读入一行整数（标准输入），去掉重复的数后输出。",
         gold=lambda a: sorted(set(a))),
    dict(name="去重后长度", shape="arr", desc="读入一行整数（标准输入），去重后输出一共有多少个数。",
         gold=lambda a: len(set(a))),
    dict(name="去重后第二小", shape="arr", desc="读入一行整数（标准输入），去重后输出第二小的数。",
         gold=lambda a: (sorted(set(a))[1] if len(set(a)) > 1 else None)),
    dict(name="前缀和最大", shape="arr", desc="读入一行整数（标准输入），输出前缀和里的最大值。",
         gold=lambda a: max(_pref(a))),
    dict(name="前缀和最小", shape="arr", desc="读入一行整数（标准输入），输出前缀和里的最小值。",
         gold=lambda a: min(_pref(a))),
    dict(name="乘二后极差", shape="arr", desc="读入一行整数（标准输入），每个数乘 2 之后输出极差。",
         gold=lambda a: max(a) * 2 - min(a) * 2),
    dict(name="加五后方差", shape="arr", desc="读入一行整数（标准输入），每个数加 5 之后输出方差（取整）。",
         gold=lambda a: _var([x + 5 for x in a])),
    dict(name="反转后前缀和最大", shape="arr", desc="读入一行整数（标准输入），反转后再输出前缀和里的最大值。",
         gold=lambda a: max(_pref(list(reversed(a))))),
]


def _pref(a):
    out, s = [], 0
    for x in a:
        s += x
        out.append(s)
    return out


def _var(a):
    mu = int(sum(a) / len(a))
    return int(sum((x - mu) * (x - mu) for x in a) / len(a))


def _set_of(argv):
    if "--blind28" in argv:
        return BLIND28
    return BLIND27 if "--blind27" in argv else BLIND26


BLIND28 = [
    dict(name="第二大", shape="arr", desc="读入一行整数（标准输入），输出其中第二大的数。",
         gold=lambda a: sorted(a, reverse=True)[1] if len(a) > 1 else None),
    dict(name="第三大", shape="arr", desc="读入一行整数（标准输入），输出其中第三大的数。",
         gold=lambda a: sorted(a, reverse=True)[2] if len(a) > 2 else None),
    dict(name="去重后第二大", shape="arr", desc="读入一行整数（标准输入），去重后输出第二大的数。",
         gold=lambda a: sorted(set(a), reverse=True)[1] if len(set(a)) > 1 else None),
    dict(name="去重后第三小", shape="arr", desc="读入一行整数（标准输入），去重后输出第三小的数。",
         gold=lambda a: sorted(set(a))[2] if len(set(a)) > 2 else None),
    dict(name="从大到小排序", shape="arr", desc="读入一行整数（标准输入），从大到小排序后输出。",
         gold=lambda a: sorted(a, reverse=True)),
    dict(name="降序后第二个", shape="arr", desc="读入一行整数（标准输入），从大到小排序后输出第二个数。",
         gold=lambda a: sorted(a, reverse=True)[1] if len(a) > 1 else None),
    dict(name="反转后第二大", shape="arr", desc="读入一行整数（标准输入），反转后输出第二大的数。",
         gold=lambda a: sorted(a, reverse=True)[1] if len(a) > 1 else None),
    dict(name="前缀和第二大", shape="arr", desc="读入一行整数（标准输入），输出前缀和里的第二大的数。",
         gold=lambda a: sorted(_pref(a), reverse=True)[1] if len(a) > 1 else None),
]


def main(argv):
    rng = random.Random(20261114)
    if "--desc" in argv:
        desc = argv[argv.index("--desc") + 1]
        chain, why = search(desc, rng, lambda x: x)
        print(("SEARCH ok: " + desc + " => " + " → ".join(n for n, _ in chain)) if chain
              else ("SEARCH fail: " + desc + "（%s）" % why))
        return 0 if chain else 1
    got, rows = 0, []
    TASKS = _set_of(argv)
    for t in TASKS:
        chain, why = search(t["desc"], rng, t["gold"], t["shape"])
        ok = chain is not None
        got += 1 if ok else 0
        rows.append(dict(name=t["name"], ok=ok, chain=[n for n, _ in chain] if chain else [], why=why))
        print(("  [PASS] " if ok else "  [FAIL] ") + t["name"] +
              (("  ⇒ " + " → ".join(n for n, _ in chain)) if ok else "  （%s）" % why))
    print("=> 首跑 %d / %d" % (got, len(TASKS)))
    if "--json" in argv:
        print(json.dumps(dict(passed=got, total=len(TASKS), rows=rows), ensure_ascii=False))
    return 0 if got else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
