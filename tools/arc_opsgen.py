#!/usr/bin/env python3
# tools/arc_opsgen.py — 每个 ARC 原语造一道"只有它能解"的小题（§6.790）
#
# 目的：`tests/arcops.sh` 用它对每个原语做**功能级**回归 —— 某个原语悄悄坏掉（改错方向、
# 变成 no-op、被误停用），这里立刻红。题都是现造的（不依赖数据集），所以套件可以离线跑。
import json, os, sys


def g(rows):
    return [list(map(int, r)) for r in rows]


def op_rot90(x):
    return [list(r) for r in zip(*x[::-1])]


def op_rot180(x):
    return [r[::-1] for r in x[::-1]]


def op_fliplr(x):
    return [r[::-1] for r in x]


def op_flipud(x):
    return x[::-1]


def op_transpose(x):
    return [list(r) for r in zip(*x)]


def op_up2(x):
    return [[v for v in row for _ in range(2)] for row in x for _ in range(2)]


def op_down2(x):
    # 输入是 2× 放大过的图，只有块内同色时才成立
    return [[x[i * 2][j * 2] for j in range(len(x[0]) // 2)] for i in range(len(x) // 2)]


def op_tile2(x):
    return [row * 2 for _ in range(2) for row in x]


def op_crop0(x):
    pts = [(i, j) for i, r in enumerate(x) for j, v in enumerate(r) if v != 0]
    i0 = min(i for i, _ in pts); i1 = max(i for i, _ in pts)
    j0 = min(j for _, j in pts); j1 = max(j for _, j in pts)
    return [row[j0:j1 + 1] for row in x[i0:i1 + 1]]


def op_symh(x):
    out = [r[:] for r in x]
    for i, r in enumerate(x):
        for j, v in enumerate(r):
            if v and out[i][len(r) - 1 - j] == 0:
                out[i][len(r) - 1 - j] = v
    return out


def op_connecth(x):
    out = [r[:] for r in x]
    for r in range(len(x)):
        cols = [c for c in range(len(x[0])) if x[r][c]]
        for a, b in zip(cols, cols[1:]):
            if x[r][a] == x[r][b]:
                for c in range(a + 1, b):
                    out[r][c] = x[r][a]
    return out


def op_gravdown(x):
    out = [[0] * len(x[0]) for _ in x]
    for j in range(len(x[0])):
        col = [x[i][j] for i in range(len(x)) if x[i][j]]
        for k, v in enumerate(col):
            out[len(x) - len(col) + k][j] = v
    return out


def op_outline(x):
    from collections import Counter
    col = Counter(v for r in x for v in r if v).most_common(1)[0][0]
    pts = [(i, j) for i, r in enumerate(x) for j, v in enumerate(r) if v]
    i0 = min(i for i, _ in pts); i1 = max(i for i, _ in pts)
    j0 = min(j for _, j in pts); j1 = max(j for _, j in pts)
    out = [[0] * len(x[0]) for _ in x]
    for j in range(j0, j1 + 1):
        out[i0][j] = col; out[i1][j] = col
    for i in range(i0, i1 + 1):
        out[i][j0] = col; out[i][j1] = col
    return out


CASES = {
    # 名字: (输入, 变换, 期望 how 里出现的原语名)
    "rot90": (g(["120", "003", "400"]), op_rot90, "rot90"),
    "rot180": (g(["123", "456", "789"]), op_rot180, "rot180"),
    "fliplr": (g(["123", "456", "789"]), op_fliplr, "fliplr"),
    "flipud": (g(["123", "456", "789"]), op_flipud, "flipud"),
    "transpose": (g(["120", "340", "567"]), op_transpose, "transpose"),
    "up2": (g(["120", "003"]), op_up2, "up2"),
    "tile2x2": (g(["12", "30"]), op_tile2, "tile2x2"),
    "crop0": (g(["00000", "01200", "00300", "00000"]), op_crop0, "crop0|子块"),
    "symh": (g(["100", "020", "003"]), op_symh, "symh"),
    "connecth": (g(["20002"]), op_connecth, "connecth"),
    "gravdown": (g(["010", "000", "100"]), op_gravdown, "gravdown"),
    "outline": (g(["00000", "01000", "01000", "01110", "00000"]), op_outline, "outline"),
}


def _outline_base2():
    # L 形：包围盒是 2×2/3×3，边框 ≠ 原图形（这样"整块刷色"和"常量输出"都抢不走）
    return g(["00000", "00100", "00100", "00110", "00000"])


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else "_t/arcops"
    os.makedirs(out, exist_ok=True)
    rows = []
    for name, (base, fn, want) in CASES.items():
        train = []
        for k in range(2):
            gi = [r[:] for r in base]
            if k == 1:                       # 第二个训练对换个内容，避免"只对上一条"
                gi = [[(v + 1) if (v and v < 8) else v for v in r] for r in gi]
            if name == "outline" and k == 1:
                gi = _outline_base2()        # 换形状，别让"常量输出"抢答
            train.append({"input": gi, "output": fn(gi)})
        ti = [[(v + 2) if (v and v < 8) else v for v in r] for r in base]
        task = {"train": train, "test": [{"input": ti, "output": fn(ti)}]}
        path = os.path.join(out, name + ".json")
        json.dump(task, open(path, "w"), separators=(",", ":"))
        rows.append(f"{name}\t{path}\t{want}")
    open(os.path.join(out, "index.tsv"), "w").write("\n".join(rows) + "\n")
    print(f"{len(rows)} 个原语小题 → {out}")


if __name__ == "__main__":
    main()
