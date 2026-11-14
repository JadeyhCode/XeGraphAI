#!/usr/bin/env python3
"""tools/datadeps.py — 给进链的算子补 **依赖/提供** 关系（§6.831）。

为什么：链的算子按描述文本位置排序，不是数据流顺序；“每行的和的最大值”里，
声明 v 的算子位置靠后，依赖 v 的算子就被误剔除（§6.829/§6.830 两次踩到）。

做法：扫 compose.triples 的 运算:*，从语句/声明行推出：
  提供：声明行里的变量（long v[...]、int n、int rr/cc、char s[...]）；语句里给 x 赋值的也算提供 x
  依赖：语句里出现、但没在本算子内声明/赋值的 v/m/s/n/rr/cc
输出 `code/knowledge/datadeps.triples`（构建脚本会一起灌）。
"""
import os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "code/knowledge/compose.triples")
OUT = os.path.join(ROOT, "code/knowledge/datadeps.triples")

VARS = ("v", "m", "s", "n", "rr", "cc")

def main():
    ops = {}
    for line in open(SRC, encoding="utf-8"):
        line = line.rstrip("\n")
        if not line or line.startswith("#"):
            continue
        parts = line.split("\t")
        if len(parts) < 3 or not parts[0].startswith("运算:"):
            continue
        ops.setdefault(parts[0], {}).setdefault(parts[1], []).append("\t".join(parts[2:]))
    out = []
    for name, rel in sorted(ops.items()):
        stmt = (rel.get("语句") or [""])[0]
        decl = (rel.get("声明行") or [""])[0]
        if not stmt and not decl:
            continue
        provided = set()
        for m in re.finditer(r"\b(long|int|char)\s+([a-zA-Z_]\w*)\s*(\[|;|=|,)", decl):
            if m.group(2) in VARS:
                provided.add(m.group(2))
        for v in VARS:
            if re.search(r"\b%s\s*=" % v, decl):
                provided.add(v)
        # 语句里**局部声明**的变量算“提供”，不是依赖（如 int m = 0 里的 m）
        for lm in re.finditer(r"\b(long|int|char)\s+([a-zA-Z_]\w*)\s*(\[|;|=|,)", stmt):
            if lm.group(2) in VARS:
                provided.add(lm.group(2))
        if re.search(r"\bx\s*=", stmt):
            provided.add("x")
        needed = set()
        for v in VARS:
            if re.search(r"\b%s\s*\[" % v, stmt) or re.search(r"\b%s\b" % v, stmt):
                if v not in provided:
                    needed.add(v)
        # 只给“有说法”的算子打标记：要么声明了东西，要么确实依赖外部变量
        if not provided and not needed:
            continue
        for v in sorted(provided):
            out.append("%s\t提供\t%s" % (name, v))
        for v in sorted(needed):
            out.append("%s\t依赖\t%s" % (name, v))
    open(OUT, "w", encoding="utf-8").write("\n".join(out) + "\n")
    print("datadeps: %d 条（%d 个算子）" % (len(out), len({l.split(chr(9))[0] for l in out})))
    return 0

if __name__ == "__main__":
    sys.exit(main())
