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
        seq.append((name, k))
    return shape, seq


def candidates(seq):
    out = []
    for name, k in seq:
        if OPS[name]["p"] and not k:
            continue
        out.append([(name, k)])
    for a, ka in seq:
        for b, kb in seq:
            if a == b or OPS[a]["o"] != OPS[b]["i"]:
                continue
            if (OPS[a]["p"] and not ka) or (OPS[b]["p"] and not kb):
                continue
            out.append([(a, ka), (b, kb)])
    return out


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


def verify(chain, shape, rng, gold, tries=6):
    src = gen_program(shape, chain)
    if not src:
        return False, "形状不支持"
    for _ in range(tries):
        if shape == "arr":
            value = [rng.randint(-9, 9) for _ in range(rng.randint(1, 7))]
            inp = " ".join(str(x) for x in value) + "\n"
        else:
            value = "".join(rng.choice("abcXYZ") for _ in range(rng.randint(1, 6)))
            inp = value + "\n"
        got = run_case(src, inp.encode(), "cand")
        if got is None:
            return False, "编译/运行失败"
        exp = exp_str(gold(value))
        if got.strip() != exp.strip():
            return False, "不一致 want=%r got=%r" % (exp, got.strip())
    return True, "ok"


def search(desc, rng, gold, shape_hint=None):
    shape, seq = parse_desc(desc)
    if shape_hint:
        shape = shape_hint
    if not seq:
        return None, "没认出算子"
    for chain in candidates(seq):
        ok, _why = verify(chain, shape, rng, gold)
        if ok:
            return chain, "ok"
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


def main(argv):
    rng = random.Random(20261114)
    if "--desc" in argv:
        desc = argv[argv.index("--desc") + 1]
        chain, why = search(desc, rng, lambda x: x)
        print(("SEARCH ok: " + desc + " => " + " → ".join(n for n, _ in chain)) if chain
              else ("SEARCH fail: " + desc + "（%s）" % why))
        return 0 if chain else 1
    got, rows = 0, []
    for t in BLIND26:
        chain, why = search(t["desc"], rng, t["gold"], t["shape"])
        ok = chain is not None
        got += 1 if ok else 0
        rows.append(dict(name=t["name"], ok=ok, chain=[n for n, _ in chain] if chain else [], why=why))
        print(("  [PASS] " if ok else "  [FAIL] ") + t["name"] +
              (("  ⇒ " + " → ".join(n for n, _ in chain)) if ok else "  （%s）" % why))
    print("=> 首跑 %d / %d" % (got, len(BLIND26)))
    if "--json" in argv:
        print(json.dumps(dict(passed=got, total=len(BLIND26), rows=rows), ensure_ascii=False))
    return 0 if got else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
