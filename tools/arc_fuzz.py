#!/usr/bin/env python3
# tools/arc_fuzz.py — 给 `::arc solve` 生成**随机任务**，专门撞边界（§6.789）
#
# 为什么要有：`arc.h` 里到处是定长数组（30×30、8 个训练对、4 个测试），
# 又有一堆"按面积/颜色/连通块"的直觉逻辑。真实任务没覆盖到 1×1、全同色、30×30、非方形……
# 这套生成器把那些形状都造一遍，断言**不崩、不卡死、有结论**（解出 / 解不出）。
#
# 用法：python3 tools/arc_fuzz.py [--n 20] [--out 目录] [--seed 7]
#   生成 <out>/f<k>.json，供 tests/arcfuzz.sh 逐个喂给 ::arc solve。
import argparse, json, os, random


def grid(rng, h, w, colors, bg=0):
    return [[rng.choice(colors) if rng.random() < 0.35 else bg for _ in range(w)] for _ in range(h)]


def mutate(rng, g, op):
    h, w = len(g), len(g[0])
    if op == "same":
        return [row[:] for row in g]
    if op == "rot180":
        return [row[::-1] for row in g[::-1]]
    if op == "flip_h":
        return [row[::-1] for row in g]
    if op == "flip_v":
        return g[::-1]
    if op == "colormap":
        m = {1: 2, 2: 3, 3: 1, 4: 5, 5: 4}
        return [[m.get(v, v) for v in row] for row in g]
    if op == "up2":
        return [[v for v in row for _ in range(2)] for row in g for _ in range(2)]
    if op == "crop":
        return [[g[0][0]]]
    if op == "transpose" and h == w:
        return [list(r) for r in zip(*g)]
    return [row[:] for row in g]


OPS = ["same", "rot180", "flip_h", "flip_v", "colormap", "up2", "crop", "transpose"]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--n", type=int, default=20)
    ap.add_argument("--out", default="_t/arcfuzz")
    ap.add_argument("--seed", type=int, default=7)
    args = ap.parse_args()
    rng = random.Random(args.seed)
    os.makedirs(args.out, exist_ok=True)
    shapes = [(1, 1), (1, 5), (5, 1), (2, 2), (3, 3), (5, 7), (10, 10), (30, 30), (2, 30), (30, 2)]
    for k in range(args.n):
        h, w = shapes[k % len(shapes)]
        bg = 0
        colors = [0, 1, 2, 3, 4]
        op = OPS[k % len(OPS)]
        train = []
        for _ in range(rng.randint(1, 4)):
            gi = grid(rng, h, w, colors, bg)
            train.append({"input": gi, "output": mutate(rng, gi, op)})
        ti = grid(rng, h, w, colors, bg)
        task = {"train": train, "test": [{"input": ti, "output": mutate(rng, ti, op)}]}
        open(os.path.join(args.out, f"f{k}.json"), "w").write(json.dumps(task, separators=(",", ":")))
    print(f"{args.n} 个随机任务 → {args.out}")


if __name__ == "__main__":
    main()
