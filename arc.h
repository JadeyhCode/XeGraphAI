/* arc.h — ARC 网格归纳：**图里存变换表，C 只当执行器**（§6.779）
 *
 * 为什么放在这里、而不是写个外部 Python 求解器：
 *   本项目的硬规矩是"新增机制能下沉到图的就下沉"。ARC 要的东西正好可以拆成两半：
 *     · **知识**（有哪些变换、谁先谁后、哪个被停用）—— 数据，进图；
 *     · **执行**（把 30×30 的整数矩阵按某个变换搬一遍）—— 代码，留在 C。
 *   于是 `::arc sync` 把变换表写进图（`变换:rot90 属于 变换类:几何`、`有 序号 1`、`有 启用 是`），
 *   `::arc off rot90` 只是往图里写 `启用 否` —— **停用一个变换不需要重编译**。
 *   这和 frame.h（帧数据进图、解释器在 C）、code.h（骨架进图、编译器在 C）是同一个形状。
 *
 * 层：grid / json / ops / search。搜索是**深度 1~2 的组合**，用训练对筛，测试对只当输入。
 *
 * 判据（和 tools/arc2_bench.py 一致）：网格**逐格相等**才算解出。
 */
#ifndef ARC_H
#define ARC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define ARC_MAX 30        /* ARC 的网格上界（题目自己保证 ≤30×30） */
#define ARC_MAX_TRAIN 8
#define ARC_MAX_TEST 4

typedef struct { int rows, cols; uint8_t v[ARC_MAX][ARC_MAX]; } ArcGrid;

static inline void arc_grid_zero(ArcGrid *g) { memset(g, 0, sizeof *g); }
static inline int arc_grid_eq(const ArcGrid *a, const ArcGrid *b) {
    if (a->rows != b->rows || a->cols != b->cols) return 0;
    for (int i = 0; i < a->rows; ++i)
        for (int j = 0; j < a->cols; ++j)
            if (a->v[i][j] != b->v[i][j]) return 0;   /* 逐格比：不碰未用到的格子（那里是未初始化） */
    return 1;
}

/* ---------------- 极简 JSON：只认 ARC 的任务文件形状 ----------------
 * {"train":[{"input":[[..]],"output":[[..]]},...],"test":[{"input":[[..]],"output":[[..]]}]}
 * 没有字符串转义要处理（全是数字和结构符），所以不用引 JSON 库。*/

typedef struct {
    int n_train, n_test;
    ArcGrid tr_in[ARC_MAX_TRAIN], tr_out[ARC_MAX_TRAIN];
    ArcGrid te_in[ARC_MAX_TEST], te_out[ARC_MAX_TEST];
    int te_has_out[ARC_MAX_TEST];
} ArcTask;

static inline const char *arc_j_skip(const char *p) {
    while (*p && (*p == ' ' || *p == '\t' || *p == '\r' || *p == '\n' || *p == ',')) p++;
    return p;
}

/* 读一个 [[r,c..],[..]] 网格；失败返回 NULL */
static inline const char *arc_j_grid(const char *p, ArcGrid *g) {
    p = arc_j_skip(p);
    if (*p != '[') return NULL;
    p++;
    arc_grid_zero(g);
    g->rows = 0; g->cols = 0;
    for (;;) {
        p = arc_j_skip(p);
        if (*p == ']') { p++; break; }
        if (*p != '[') return NULL;
        p++;
        int cols = 0;
        for (;;) {
            p = arc_j_skip(p);
            if (*p == ']') { p++; break; }
            if (*p < '0' || *p > '9') return NULL;
            int v = 0;
            while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); p++; }
            if (g->rows >= ARC_MAX || cols >= ARC_MAX) return NULL;
            g->v[g->rows][cols++] = (uint8_t)v;
        }
        if (g->rows == 0) g->cols = cols;
        else if (cols != g->cols) return NULL;       /* 行不等长：坏文件 */
        g->rows++;
    }
    return (g->rows > 0 && g->cols > 0) ? p : NULL;
}

/* 在 ptr 之后找 key，并读它后面的网格（找不到返回 NULL） */
static inline const char *arc_j_key(const char *p, const char *key) {
    size_t kl = strlen(key);
    for (; *p; ++p)
        if (*p == '"' && strncmp(p + 1, key, kl) == 0 && p[1 + kl] == '"') {
            const char *q = p + 2 + kl;
            while (*q && *q != ':') q++;
            if (*q == ':') return q + 1;
            return NULL;
        }
    return NULL;
}

static inline int arc_task_load(const char *path, ArcTask *t) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (n <= 0 || n > (1 << 22)) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)n + 1);
    if (!buf) { fclose(f); return -1; }
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);

    /* ⚠ `test` 可能排在 `train` **前面**（ARC-AGI-1 里就有这种文件），所以不能靠
     * "从 train 扫到 test" 划段：按文档顺序扫四个 key，用最后见到的段键决定往哪存。 */
    memset(t, 0, sizeof *t);
    const char *p = buf;
    int section = 0;                 /* 1=train 2=test */
    while ((p = strchr(p, '"')) != NULL) {
        p++;
        if (!strncmp(p, "train\"", 6))       { section = 1; p += 6; continue; }
        if (!strncmp(p, "test\"", 5))        { section = 2; p += 5; continue; }
        if (!strncmp(p, "input\"", 6)) {
            const char *q = p + 6;
            while (*q && *q != ':') q++;
            ArcGrid g;
            if (*q != ':' || !arc_j_grid(q + 1, &g)) break;
            if (section == 1) {
                if (t->n_train >= ARC_MAX_TRAIN) break;
                t->tr_in[t->n_train] = g;
            } else if (section == 2) {
                if (t->n_test >= ARC_MAX_TEST) break;
                t->te_in[t->n_test] = g;
            }
            p = q + 1;
            continue;
        }
        if (!strncmp(p, "output\"", 7)) {
            const char *q = p + 7;
            while (*q && *q != ':') q++;
            ArcGrid g;
            if (*q != ':' || !arc_j_grid(q + 1, &g)) break;
            if (section == 1) {
                if (t->n_train < ARC_MAX_TRAIN) {      /* 一个训练对以 output 收尾 */
                    t->tr_out[t->n_train] = g;
                    t->n_train++;
                }
            } else if (section == 2) {
                if (t->n_test < ARC_MAX_TEST) {
                    t->te_out[t->n_test] = g;
                    t->te_has_out[t->n_test] = 1;
                    t->n_test++;               /* 一个 test 对象以 output 收尾 */
                }
            }
            p = q + 1;
            continue;
        }
        p++;
    }
    free(buf);
    return (t->n_train > 0 && t->n_test > 0) ? 0 : -1;
}

/* ---------------- 变换原语 ----------------
 * 每个都是 `in → out`；装不下/形状不合法返回 -1（不算候选，不报错）。*/

typedef int (*ArcOpFn)(const ArcGrid *in, ArcGrid *out);

static inline int arc_op_identity(const ArcGrid *in, ArcGrid *out) { *out = *in; return 0; }

static inline int arc_op_rot(const ArcGrid *in, ArcGrid *out, int k) {
    const ArcGrid *cur = in;
    ArcGrid a = *in, b;
    for (int r = 0; r < (k % 4); ++r) {
        memset(&b, 0, sizeof b);
        b.rows = cur->cols; b.cols = cur->rows;
        for (int i = 0; i < cur->rows; ++i)
            for (int j = 0; j < cur->cols; ++j)
                b.v[j][cur->rows - 1 - i] = cur->v[i][j];
        a = b; cur = &a;
    }
    if (a.rows > ARC_MAX || a.cols > ARC_MAX) return -1;
    *out = a;
    return 0;
}
static inline int arc_op_rot90(const ArcGrid *i, ArcGrid *o)  { return arc_op_rot(i, o, 1); }
static inline int arc_op_rot180(const ArcGrid *i, ArcGrid *o) { return arc_op_rot(i, o, 2); }
static inline int arc_op_rot270(const ArcGrid *i, ArcGrid *o) { return arc_op_rot(i, o, 3); }

static inline int arc_op_fliplr(const ArcGrid *in, ArcGrid *out) {
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            out->v[i][j] = in->v[i][in->cols - 1 - j];
    return 0;
}
static inline int arc_op_flipud(const ArcGrid *in, ArcGrid *out) {
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            out->v[i][j] = in->v[in->rows - 1 - i][j];
    return 0;
}
static inline int arc_op_transpose(const ArcGrid *in, ArcGrid *out) {
    out->rows = in->cols; out->cols = in->rows;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            out->v[j][i] = in->v[i][j];
    return 0;
}
static inline int arc_op_antitranspose(const ArcGrid *in, ArcGrid *out) {
    ArcGrid t; if (arc_op_transpose(in, &t)) return -1;
    return arc_op_rot(&t, out, 2);
}

static inline int arc_op_upscale(const ArcGrid *in, ArcGrid *out, int k) {
    if (in->rows * k > ARC_MAX || in->cols * k > ARC_MAX) return -1;
    out->rows = in->rows * k; out->cols = in->cols * k;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j)
            out->v[i][j] = in->v[i / k][j / k];
    return 0;
}
static inline int arc_op_up2(const ArcGrid *i, ArcGrid *o) { return arc_op_upscale(i, o, 2); }
static inline int arc_op_up3(const ArcGrid *i, ArcGrid *o) { return arc_op_upscale(i, o, 3); }

static inline int arc_op_downscale(const ArcGrid *in, ArcGrid *out, int k) {
    if (in->rows % k || in->cols % k) return -1;
    out->rows = in->rows / k; out->cols = in->cols / k;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j) {
            uint8_t v = in->v[i * k][j * k];
            for (int a = 0; a < k; ++a)
                for (int b = 0; b < k; ++b)
                    if (in->v[i * k + a][j * k + b] != v) return -1;   /* 块内不一致 ⇒ 不是缩小 */
            out->v[i][j] = v;
        }
    return 0;
}
static inline int arc_op_down2(const ArcGrid *i, ArcGrid *o) { return arc_op_downscale(i, o, 2); }
static inline int arc_op_down3(const ArcGrid *i, ArcGrid *o) { return arc_op_downscale(i, o, 3); }

static inline int arc_op_tile(const ArcGrid *in, ArcGrid *out, int ky, int kx) {
    if (in->rows * ky > ARC_MAX || in->cols * kx > ARC_MAX) return -1;
    out->rows = in->rows * ky; out->cols = in->cols * kx;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j)
            out->v[i][j] = in->v[i % in->rows][j % in->cols];
    return 0;
}
static inline int arc_op_tile2x2(const ArcGrid *i, ArcGrid *o) { return arc_op_tile(i, o, 2, 2); }
static inline int arc_op_tile1x2(const ArcGrid *i, ArcGrid *o) { return arc_op_tile(i, o, 1, 2); }
static inline int arc_op_tile2x1(const ArcGrid *i, ArcGrid *o) { return arc_op_tile(i, o, 2, 1); }

static inline int arc_op_mirrortile(const ArcGrid *in, ArcGrid *out) {
    if (in->rows * 2 > ARC_MAX || in->cols * 2 > ARC_MAX) return -1;
    out->rows = in->rows * 2; out->cols = in->cols * 2;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j) {
            int r = i % in->rows, c = j % in->cols;
            if (i >= in->rows) r = in->rows - 1 - r;      /* 下半个上下翻 */
            if (j >= in->cols) c = in->cols - 1 - c;      /* 右半个左右翻 */
            out->v[i][j] = in->v[r][c];
        }
    return 0;
}

static inline int arc_common_color(const ArcGrid *g) {
    int cnt[256]; memset(cnt, 0, sizeof cnt);
    for (int i = 0; i < g->rows; ++i)
        for (int j = 0; j < g->cols; ++j) cnt[g->v[i][j]]++;
    int best = 0, bc = -1;
    for (int c = 0; c < 256; ++c) if (cnt[c] > best) { best = cnt[c]; bc = c; }
    return bc;
}
static inline int arc_op_crop_bg(const ArcGrid *in, ArcGrid *out, int bg) {
    int r0 = in->rows, r1 = -1, c0 = in->cols, c1 = -1;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if (in->v[i][j] != bg) {
                if (i < r0) r0 = i;
                if (i > r1) r1 = i;
                if (j < c0) c0 = j;
                if (j > c1) c1 = j;
            }
    if (r1 < 0) return -1;
    out->rows = r1 - r0 + 1; out->cols = c1 - c0 + 1;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j)
            out->v[i][j] = in->v[r0 + i][c0 + j];
    return 0;
}
static inline int arc_op_crop0(const ArcGrid *in, ArcGrid *out) { return arc_op_crop_bg(in, out, 0); }
static inline int arc_op_crop_common(const ArcGrid *in, ArcGrid *out) {
    return arc_op_crop_bg(in, out, arc_common_color(in));
}

/* 对称补全：把背景格按镜像补上非背景色（只补"对面已经有颜色"的格） */
static inline int arc_symfill(const ArcGrid *in, ArcGrid *out, int h, int v) {
    *out = *in;
    int bg = arc_common_color(in);
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) {
            if (out->v[i][j] == bg) continue;
            uint8_t val = out->v[i][j];
            int ij[4][2]; int n = 0;
            if (h) { ij[n][0] = i; ij[n][1] = in->cols - 1 - j; n++; }
            if (v) { ij[n][0] = in->rows - 1 - i; ij[n][1] = j; n++; }
            if (h && v) { ij[n][0] = in->rows - 1 - i; ij[n][1] = in->cols - 1 - j; n++; }
            for (int k = 0; k < n; ++k)
                if (out->v[ij[k][0]][ij[k][1]] == bg) out->v[ij[k][0]][ij[k][1]] = val;
        }
    return 0;
}
static inline int arc_op_symh(const ArcGrid *i, ArcGrid *o)  { return arc_symfill(i, o, 1, 0); }
static inline int arc_op_symv(const ArcGrid *i, ArcGrid *o)  { return arc_symfill(i, o, 0, 1); }
static inline int arc_op_symhv(const ArcGrid *i, ArcGrid *o) { return arc_symfill(i, o, 1, 1); }

/* 重力：非背景色朝一个方向落到底（每列/每行独立堆叠） */
static inline int arc_op_gravity(const ArcGrid *in, ArcGrid *out, int dir) {
    /* dir: 0=下 1=上 2=左 3=右 */
    int bg = arc_common_color(in);
    memset(out, 0, sizeof *out);
    out->rows = in->rows; out->cols = in->cols;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) out->v[i][j] = (uint8_t)bg;
    if (dir == 0 || dir == 1) {
        for (int j = 0; j < in->cols; ++j) {
            uint8_t col[ARC_MAX]; int n = 0;
            for (int i = 0; i < in->rows; ++i) if (in->v[i][j] != bg) col[n++] = in->v[i][j];
            for (int i = 0; i < n; ++i) {
                int row = (dir == 0) ? (in->rows - n + i) : i;
                out->v[row][j] = col[i];
            }
        }
    } else {
        for (int i = 0; i < in->rows; ++i) {
            uint8_t row[ARC_MAX]; int n = 0;
            for (int j = 0; j < in->cols; ++j) if (in->v[i][j] != bg) row[n++] = in->v[i][j];
            for (int j = 0; j < n; ++j) {
                int at = (dir == 3) ? (in->cols - n + j) : j;
                out->v[i][at] = row[j];
            }
        }
    }
    return 0;
}
static inline int arc_op_grav_down(const ArcGrid *i, ArcGrid *o) { return arc_op_gravity(i, o, 0); }
static inline int arc_op_grav_up(const ArcGrid *i, ArcGrid *o)   { return arc_op_gravity(i, o, 1); }
static inline int arc_op_grav_left(const ArcGrid *i, ArcGrid *o) { return arc_op_gravity(i, o, 2); }
static inline int arc_op_grav_right(const ArcGrid *i, ArcGrid *o){ return arc_op_gravity(i, o, 3); }

/* 最大连通块（4 邻接）的包围盒 */
static inline int arc_op_largest_object(const ArcGrid *in, ArcGrid *out) {
    int bg = arc_common_color(in);
    static int lab[ARC_MAX][ARC_MAX];
    memset(lab, 0, sizeof lab);
    int best = 0, bestsz = 0, id = 0, sz[ARC_MAX * ARC_MAX / 2 + 2];
    int stack[ARC_MAX * ARC_MAX][2];
    memset(sz, 0, sizeof sz);
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) {
            if (lab[i][j] || in->v[i][j] == bg) continue;
            id++;
            int sp = 0; stack[sp][0] = i; stack[sp][1] = j; sp++;
            lab[i][j] = id;
            int n = 0;
            while (sp) {
                int a = stack[--sp][0], b = stack[sp][1];
                n++;
                int d[4][2] = { {a-1,b},{a+1,b},{a,b-1},{a,b+1} };
                for (int k = 0; k < 4; ++k) {
                    int x = d[k][0], y = d[k][1];
                    if (x < 0 || y < 0 || x >= in->rows || y >= in->cols) continue;
                    if (lab[x][y] || in->v[x][y] == bg) continue;
                    lab[x][y] = id; stack[sp][0] = x; stack[sp][1] = y; sp++;
                }
            }
            sz[id] = n;
            if (n > bestsz) { bestsz = n; best = id; }
        }
    if (!best) return -1;
    int r0 = in->rows, r1 = -1, c0 = in->cols, c1 = -1;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if (lab[i][j] == best) {
                if (i < r0) r0 = i;
                if (i > r1) r1 = i;
                if (j < c0) c0 = j;
                if (j > c1) c1 = j;
            }
    out->rows = r1 - r0 + 1; out->cols = c1 - c0 + 1;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j)
            out->v[i][j] = in->v[r0 + i][c0 + j];
    return 0;
}


/* ---------------- 第二批原语（§6.779）：裁剪到某色 / 取象限 / 去噪 / 补洞 / 叠加 ----------------
 * 加它们的理由：ARC-AGI-1 eval 里"输出是输入的某个子块或某个颜色的包围盒"占了一大类，
 * 光靠几何+缩放的组合碰不到。每加一个都必须能在训练对上筛，所以宁多勿精。 */
static inline int arc_op_crop_color(const ArcGrid *in, ArcGrid *out, int c) {
    int any = 0;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) if (in->v[i][j] == c) any = 1;
    if (!any) return -1;
    return arc_op_crop_bg(in, out, c);
}
static inline int arc_op_cropc1(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 1); }
static inline int arc_op_cropc2(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 2); }
static inline int arc_op_cropc3(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 3); }
static inline int arc_op_cropc4(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 4); }
static inline int arc_op_cropc5(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 5); }
static inline int arc_op_cropc6(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 6); }
static inline int arc_op_cropc7(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 7); }
static inline int arc_op_cropc8(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 8); }
static inline int arc_op_cropc9(const ArcGrid *i, ArcGrid *o) { return arc_op_crop_color(i, o, 9); }

static inline int arc_op_quad(const ArcGrid *in, ArcGrid *out, int which) {
    if (in->rows % 2 || in->cols % 2) return -1;
    int rh = in->rows / 2, cw = in->cols / 2;
    int r0 = (which >= 2) ? rh : 0, c0 = (which % 2) ? cw : 0;
    out->rows = rh; out->cols = cw;
    for (int i = 0; i < rh; ++i)
        for (int j = 0; j < cw; ++j) out->v[i][j] = in->v[r0 + i][c0 + j];
    return 0;
}
static inline int arc_op_quad_tl(const ArcGrid *i, ArcGrid *o) { return arc_op_quad(i, o, 0); }
static inline int arc_op_quad_tr(const ArcGrid *i, ArcGrid *o) { return arc_op_quad(i, o, 1); }
static inline int arc_op_quad_bl(const ArcGrid *i, ArcGrid *o) { return arc_op_quad(i, o, 2); }
static inline int arc_op_quad_br(const ArcGrid *i, ArcGrid *o) { return arc_op_quad(i, o, 3); }

/* 去噪：孤立点（四邻都是背景）抹成背景 */
static inline int arc_op_denoise(const ArcGrid *in, ArcGrid *out) {
    int bg = arc_common_color(in);
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) {
            if (in->v[i][j] == bg) continue;
            int nb = 0;
            if (i > 0 && in->v[i-1][j] != bg) nb++;
            if (i + 1 < in->rows && in->v[i+1][j] != bg) nb++;
            if (j > 0 && in->v[i][j-1] != bg) nb++;
            if (j + 1 < in->cols && in->v[i][j+1] != bg) nb++;
            if (nb == 0) out->v[i][j] = (uint8_t)bg;
        }
    return 0;
}

/* 补洞：从边框灌不进背景的区域（被围住的洞）用"最多的前景色"填掉 */
static inline int arc_op_fillholes(const ArcGrid *in, ArcGrid *out) {
    int bg = arc_common_color(in);
    int cnt[256]; memset(cnt, 0, sizeof cnt);
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) if (in->v[i][j] != bg) cnt[in->v[i][j]]++;
    int fg = bg, best = 0;
    for (int c = 0; c < 256; ++c) if (cnt[c] > best) { best = cnt[c]; fg = c; }
    static int seen[ARC_MAX][ARC_MAX];
    memset(seen, 0, sizeof seen);
    int stack[ARC_MAX * ARC_MAX][2]; int sp = 0;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if ((i == 0 || j == 0 || i == in->rows - 1 || j == in->cols - 1) && in->v[i][j] == bg && !seen[i][j]) {
                seen[i][j] = 1; stack[sp][0] = i; stack[sp][1] = j; sp++;
            }
    while (sp) {
        int a = stack[--sp][0], b = stack[sp][1];
        int d[4][2] = { {a-1,b},{a+1,b},{a,b-1},{a,b+1} };
        for (int k = 0; k < 4; ++k) {
            int x = d[k][0], y = d[k][1];
            if (x < 0 || y < 0 || x >= in->rows || y >= in->cols) continue;
            if (seen[x][y] || in->v[x][y] != bg) continue;
            seen[x][y] = 1; stack[sp][0] = x; stack[sp][1] = y; sp++;
        }
    }
    *out = *in;
    int holes = 0;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if (in->v[i][j] == bg && !seen[i][j]) { out->v[i][j] = (uint8_t)fg; holes++; }
    return holes ? 0 : -1;
}

/* 左右两半叠加：or = 任意非背景；xor = 只有一边非背景（两边不同） */
static inline int arc_op_overlay(const ArcGrid *in, ArcGrid *out, int mode) {
    if (in->cols % 2) return -1;
    int hw = in->cols / 2, bg = arc_common_color(in);
    out->rows = in->rows; out->cols = hw;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < hw; ++j) {
            uint8_t a = in->v[i][j], b = in->v[i][j + hw];
            uint8_t v;
            if (mode == 0) v = (a != bg) ? a : b;                      /* or：左优先 */
            else           v = (a != bg && b != bg) ? (uint8_t)bg : ((a != bg) ? a : b); /* xor */
            out->v[i][j] = v;
        }
    return 0;
}
static inline int arc_op_overlay_or(const ArcGrid *i, ArcGrid *o)  { return arc_op_overlay(i, o, 0); }
static inline int arc_op_overlay_xor(const ArcGrid *i, ArcGrid *o) { return arc_op_overlay(i, o, 1); }


/* ---------------- 对象级（§6.779 第二轮）：连通块 ---------------- */
typedef struct { int r0, r1, c0, c1, sz; uint8_t color; } ArcObj;

/* 4 邻接连通块（背景 = 全图最多的颜色）。返回个数；每个对象的 color 取它第一个格子的颜色。 */
static inline int arc_objects(const ArcGrid *g, ArcObj *out, int max) {
    int bg = arc_common_color(g);
    static int seen[ARC_MAX][ARC_MAX];
    memset(seen, 0, sizeof seen);
    int n = 0;
    int stack[ARC_MAX * ARC_MAX][2];
    for (int i = 0; i < g->rows; ++i)
        for (int j = 0; j < g->cols; ++j) {
            if (seen[i][j] || g->v[i][j] == bg) continue;
            if (n >= max) return n;
            int sp = 0; stack[sp][0] = i; stack[sp][1] = j; sp++;
            seen[i][j] = 1;
            ArcObj o; o.r0 = o.r1 = i; o.c0 = o.c1 = j; o.sz = 0; o.color = g->v[i][j];
            while (sp) {
                int a = stack[--sp][0], b = stack[sp][1];
                o.sz++;
                if (a < o.r0) o.r0 = a;
                if (a > o.r1) o.r1 = a;
                if (b < o.c0) o.c0 = b;
                if (b > o.c1) o.c1 = b;
                int d[4][2] = { {a-1,b},{a+1,b},{a,b-1},{a,b+1} };
                for (int k = 0; k < 4; ++k) {
                    int x = d[k][0], y = d[k][1];
                    if (x < 0 || y < 0 || x >= g->rows || y >= g->cols) continue;
                    if (seen[x][y] || g->v[x][y] == bg) continue;
                    seen[x][y] = 1; stack[sp][0] = x; stack[sp][1] = y; sp++;
                }
            }
            out[n++] = o;
        }
    return n;
}

/* 只保留"面积最小"或"面积最大"的那个对象（按包围盒裁出来） */

/* 打标签：lab = 对象编号（1 起），0 = 背景/未标 */
static inline int arc_label_objects(const ArcGrid *g, int lab[ARC_MAX][ARC_MAX],
                                    ArcObj *objs, int maxn) {
    int bg = arc_common_color(g);
    memset(lab, 0, ARC_MAX * ARC_MAX * sizeof lab[0][0]);
    int n = 0;
    int stack[ARC_MAX * ARC_MAX][2];
    for (int i = 0; i < g->rows; ++i)
        for (int j = 0; j < g->cols; ++j) {
            if (lab[i][j] || g->v[i][j] == bg) continue;
            if (n >= maxn) return n;
            int sp = 0; stack[sp][0] = i; stack[sp][1] = j; sp++;
            lab[i][j] = n + 1;
            ArcObj o; o.r0 = o.r1 = i; o.c0 = o.c1 = j; o.sz = 0; o.color = g->v[i][j];
            while (sp) {
                int a = stack[--sp][0], b = stack[sp][1];
                o.sz++;
                if (a < o.r0) o.r0 = a;
                if (a > o.r1) o.r1 = a;
                if (b < o.c0) o.c0 = b;
                if (b > o.c1) o.c1 = b;
                int d[4][2] = { {a-1,b},{a+1,b},{a,b-1},{a,b+1} };
                for (int k = 0; k < 4; ++k) {
                    int x = d[k][0], y = d[k][1];
                    if (x < 0 || y < 0 || x >= g->rows || y >= g->cols) continue;
                    if (lab[x][y] || g->v[x][y] == bg) continue;
                    lab[x][y] = n + 1; stack[sp][0] = x; stack[sp][1] = y; sp++;
                }
            }
            objs[n++] = o;
        }
    return n;
}

static inline int arc_op_pickobj(const ArcGrid *in, ArcGrid *out, int want_max) {
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_objects(in, objs, ARC_MAX * ARC_MAX);
    if (n < 1) return -1;
    int best = 0;
    for (int i = 1; i < n; ++i)
        if (want_max ? (objs[i].sz > objs[best].sz) : (objs[i].sz < objs[best].sz)) best = i;
    int r0 = objs[best].r0, r1 = objs[best].r1, c0 = objs[best].c0, c1 = objs[best].c1;
    out->rows = r1 - r0 + 1; out->cols = c1 - c0 + 1;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j) out->v[i][j] = in->v[r0 + i][c0 + j];
    return 0;
}
static inline int arc_op_smallestobj(const ArcGrid *i, ArcGrid *o) { return arc_op_pickobj(i, o, 0); }

/* 填包围盒：每个对象把自己的包围盒刷成自己的颜色 */
static inline int arc_op_fillbbox(const ArcGrid *in, ArcGrid *out) {
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_objects(in, objs, ARC_MAX * ARC_MAX);
    if (n < 1) return -1;
    int bg = arc_common_color(in);
    *out = *in;
    for (int k = 0; k < n; ++k)
        for (int i = objs[k].r0; i <= objs[k].r1; ++i)
            for (int j = objs[k].c0; j <= objs[k].c1; ++j)
                if (out->v[i][j] == bg) out->v[i][j] = objs[k].color;
    return 0;
}

/* 描包围盒：只留每个对象的包围盒边框（里面挖空） */
static inline int arc_op_outline(const ArcGrid *in, ArcGrid *out) {
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_objects(in, objs, ARC_MAX * ARC_MAX);
    if (n < 1) return -1;
    int bg = arc_common_color(in);
    memset(out, 0, sizeof *out);
    out->rows = in->rows; out->cols = in->cols;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) out->v[i][j] = (uint8_t)bg;
    for (int k = 0; k < n; ++k) {
        for (int j = objs[k].c0; j <= objs[k].c1; ++j) {
            out->v[objs[k].r0][j] = objs[k].color;
            out->v[objs[k].r1][j] = objs[k].color;
        }
        for (int i = objs[k].r0; i <= objs[k].r1; ++i) {
            out->v[i][objs[k].c0] = objs[k].color;
            out->v[i][objs[k].c1] = objs[k].color;
        }
    }
    return 0;
}

/* 按面积名次重新上色（第 1 小的对象刷成 1，第 2 小刷成 2 …） */
static inline int arc_op_objrank(const ArcGrid *in, ArcGrid *out) {
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_objects(in, objs, ARC_MAX * ARC_MAX);
    if (n < 2) return -1;
    /* 按面积排序（插入排序，n 很小） */
    int idx[ARC_MAX * ARC_MAX];
    for (int i = 0; i < n; ++i) idx[i] = i;
    for (int i = 1; i < n; ++i) {
        int x = idx[i], j = i - 1;
        while (j >= 0 && objs[idx[j]].sz > objs[x].sz) { idx[j + 1] = idx[j]; j--; }
        idx[j + 1] = x;
    }
    int bg = arc_common_color(in);
    *out = *in;
    for (int rank = 0; rank < n; ++rank) {
        int k = idx[rank];
        uint8_t col = (uint8_t)((rank % 9) + 1);
        for (int i = 0; i < in->rows; ++i)
            for (int j = 0; j < in->cols; ++j)
                if (in->v[i][j] != bg && i >= objs[k].r0 && i <= objs[k].r1 &&
                    j >= objs[k].c0 && j <= objs[k].c1) {
                    /* 只给"这个对象自己的格子"上色：用同色 + 同块判定，简化成同色 */
                    if (in->v[i][j] == objs[k].color) out->v[i][j] = col;
                }
    }
    return 0;
}


/* ---------------- 第三批原语（§6.783）：**选择/计数/对称** ----------------
 * 前两轮的教训是"再多变换也碰不到需要'挑一个对象/数一数'的题"。
 * 这批原语做的就是"按属性选"与"数出来"：颜色出现次数、对象大小、两半差异、对角对称。 */

/* 颜色出现次数（非背景）排序用的小工具 */
static inline void arc_color_counts(const ArcGrid *g, int cnt[256], int *bg_out) {
    memset(cnt, 0, 256 * sizeof cnt[0]);
    for (int i = 0; i < g->rows; ++i)
        for (int j = 0; j < g->cols; ++j) cnt[g->v[i][j]]++;
    int bg = arc_common_color(g);
    if (bg_out) *bg_out = bg;
}

/* 只留"出现次数为 1 的颜色"的格子（其余刷背景） */
static inline int arc_op_seluniq(const ArcGrid *in, ArcGrid *out) {
    int cnt[256], bg; arc_color_counts(in, cnt, &bg);
    int hit = -1;
    for (int c = 1; c < 256; ++c) if (c != bg && cnt[c] == 1) { hit = c; break; }
    if (hit < 0) return -1;
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) if (in->v[i][j] != hit) out->v[i][j] = (uint8_t)bg;
    return 0;
}

/* 只留"最多的那个非背景色" */
static inline int arc_op_selcommon(const ArcGrid *in, ArcGrid *out) {
    int cnt[256], bg; arc_color_counts(in, cnt, &bg);
    int best = -1, bc = 0;
    for (int c = 1; c < 256; ++c) if (c != bg && cnt[c] > best) { best = cnt[c]; bc = c; }
    if (best <= 0) return -1;
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) if (in->v[i][j] != bc) out->v[i][j] = (uint8_t)bg;
    return 0;
}

/* 只留"最少的那个非背景色"（并列就取最小的颜色号） */
static inline int arc_op_selrare(const ArcGrid *in, ArcGrid *out) {
    int cnt[256], bg; arc_color_counts(in, cnt, &bg);
    int best = -1, bc = 0;
    for (int c = 1; c < 256; ++c) if (c != bg && cnt[c] > 0 && (best < 0 || cnt[c] < best)) { best = cnt[c]; bc = c; }
    if (best <= 0) return -1;
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) if (in->v[i][j] != bc) out->v[i][j] = (uint8_t)bg;
    return 0;
}

/* 数出来：1×1，值是对象个数 / 不同前景色个数 */
static inline int arc_op_countobj(const ArcGrid *in, ArcGrid *out) {
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_objects(in, objs, ARC_MAX * ARC_MAX);
    if (n < 1) return -1;
    out->rows = 1; out->cols = 1; out->v[0][0] = (uint8_t)(n > 9 ? 9 : n);
    return 0;
}
static inline int arc_op_countcolors(const ArcGrid *in, ArcGrid *out) {
    int cnt[256], bg; arc_color_counts(in, cnt, &bg);
    int n = 0;
    for (int c = 0; c < 256; ++c) if (c != bg && cnt[c] > 0) n++;
    if (n < 1) return -1;
    out->rows = 1; out->cols = 1; out->v[0][0] = (uint8_t)(n > 9 ? 9 : n);
    return 0;
}

/* 挑"和多数对象不一样"的那个（按面积），裁它的包围盒 */
static inline int arc_op_oddobj(const ArcGrid *in, ArcGrid *out) {
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_objects(in, objs, ARC_MAX * ARC_MAX);
    if (n < 3) return -1;                       /* 少于 3 个谈不上"多数/少数" */
    int hist[ARC_MAX * ARC_MAX];                /* 按面积计数 */
    for (int i = 0; i < n; ++i) hist[i] = 0;
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) if (objs[i].sz == objs[j].sz) hist[i]++;
    int odd = -1;
    for (int i = 0; i < n; ++i) if (hist[i] == 1) { odd = i; break; }
    if (odd < 0) return -1;
    int r0 = objs[odd].r0, r1 = objs[odd].r1, c0 = objs[odd].c0, c1 = objs[odd].c1;
    out->rows = r1 - r0 + 1; out->cols = c1 - c0 + 1;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j) out->v[i][j] = in->v[r0 + i][c0 + j];
    return 0;
}

/* 上下两半叠加（or：任一半非背景取它；xor：两边不同才留） */
static inline int arc_op_overlay_tb(const ArcGrid *in, ArcGrid *out, int mode) {
    if (in->rows % 2) return -1;
    int hh = in->rows / 2, bg = arc_common_color(in);
    out->rows = hh; out->cols = in->cols;
    for (int i = 0; i < hh; ++i)
        for (int j = 0; j < in->cols; ++j) {
            uint8_t a = in->v[i][j], b = in->v[i + hh][j];
            uint8_t v;
            if (mode == 0) v = (a != bg) ? a : b;
            else           v = (a != bg && b != bg) ? (uint8_t)bg : ((a != bg) ? a : b);
            out->v[i][j] = v;
        }
    return 0;
}
static inline int arc_op_overlay_tb_or(const ArcGrid *i, ArcGrid *o)  { return arc_op_overlay_tb(i, o, 0); }
static inline int arc_op_overlay_tb_xor(const ArcGrid *i, ArcGrid *o) { return arc_op_overlay_tb(i, o, 1); }

/* 对角对称补全：按主对角线镜像补背景格 */
static inline int arc_op_symdiag(const ArcGrid *in, ArcGrid *out) {
    if (in->rows != in->cols) return -1;
    int bg = arc_common_color(in);
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) {
            if (out->v[i][j] == bg) continue;
            if (out->v[j][i] == bg) out->v[j][i] = out->v[i][j];
        }
    return 0;
}



/* ---------------- 结构类（§6.786）：固定位置子块 / 加边框 / 去边框 ----------------
 * 这三类是 ARC 里最朴素的"结构"题，靠几何原语和掩码都碰不到：
 *   · 输出就是输入里**固定位置**的一块（"取右下角那个面板"）；
 *   · 输出比输入大一圈（加边框，颜色 c）；
 *   · 输出比输入小一圈（去边框）。 */
static inline int arc_sub_at(const ArcGrid *in, ArcGrid *out, int r, int c, int h, int w) {
    if (r < 0 || c < 0 || r + h > in->rows || c + w > in->cols) return -1;
    out->rows = h; out->cols = w;
    for (int i = 0; i < h; ++i)
        for (int j = 0; j < w; ++j) out->v[i][j] = in->v[r + i][c + j];
    return 0;
}
static inline int arc_pad1(const ArcGrid *in, ArcGrid *out, int c) {
    if (in->rows + 2 > ARC_MAX || in->cols + 2 > ARC_MAX) return -1;
    memset(out, 0, sizeof *out);
    out->rows = in->rows + 2; out->cols = in->cols + 2;
    for (int i = 0; i < out->rows; ++i)
        for (int j = 0; j < out->cols; ++j) out->v[i][j] = (uint8_t)c;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) out->v[i + 1][j + 1] = in->v[i][j];
    return 0;
}
static inline int arc_unpad1(const ArcGrid *in, ArcGrid *out) {
    if (in->rows <= 2 || in->cols <= 2) return -1;
    return arc_sub_at(in, out, 1, 1, in->rows - 2, in->cols - 2);
}


/* ---------------- 对象属性规则（§6.787）：按"属性 → 动作"改对象 ----------------
 * 68% 的 eval 题输出和输入同尺寸，而且很多是"按某个属性挑出对象，再把它们改掉"：
 *   碰到边框的刷成 3 / 只留最大的 / 去掉面积和多数不一样的那个 ……
 * 之前只能"整张图一个变换"，这类题一道也碰不到。这里把 (属性, 动作, 颜色) 做成一个小搜索。*/

/* 打标签：lab = 对象编号（1 起），0 = 背景/未标 */

/* 每个对象一个"标志"（属性） */

/* 谓词编号：0..5 = 基础属性，6..11 = 它们的**否定**；把两个谓词**与**起来（p2 < 0 表示单谓词）。
 * 为什么要有合取：上一轮实测"单属性规则 eval +0" —— eval 的题是"同时满足两个条件的那批对象"。 */

/* action: 0=刷成颜色 c，1=去掉（刷背景），2=只留（其余刷背景） */



/* 前置声明：下面要用 apply_named，而它的定义在变换表后面 */
static inline int arc_apply_named(const ArcGrid *in, ArcGrid *out, const char *name);

/* ---------------- 局部编辑（§6.785）：mask × 保形变换 ----------------
 * ARC 里一大类题是"只改一部分"：把最大的那个对象转一下、只给被围住的区域上色、
 * 只换独一无二那个颜色 …… 之前所有原语都是"整张图一起动"，所以这类题一道都碰不到。
 * 这里加一层：先按属性算掩码，再把某个保形变换的结果**只提交在掩码内**。 */
typedef int (*ArcMaskFn)(const ArcGrid *in, uint8_t mask[ARC_MAX][ARC_MAX]);

static inline int arc_bg_of(const ArcGrid *g) { return arc_common_color(g); }

/* 非背景格 */
static inline int arc_mask_nonbg(const ArcGrid *in, uint8_t m[ARC_MAX][ARC_MAX]) {
    int bg = arc_bg_of(in);
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) m[i][j] = (uint8_t)(in->v[i][j] != bg);
    return 0;
}
/* 某个颜色的格 */
#define ARC_MASK_COLOR_FN(C) \
    static inline int arc_mask_c##C(const ArcGrid *in, uint8_t m[ARC_MAX][ARC_MAX]) { \
        for (int i = 0; i < in->rows; ++i) \
            for (int j = 0; j < in->cols; ++j) m[i][j] = (uint8_t)(in->v[i][j] == C); \
        return 0; \
    }
ARC_MASK_COLOR_FN(1) ARC_MASK_COLOR_FN(2) ARC_MASK_COLOR_FN(3)
ARC_MASK_COLOR_FN(4) ARC_MASK_COLOR_FN(5) ARC_MASK_COLOR_FN(6)

/* 第 k 大/小的对象；k=0 最大、k=1 最小、k=2 "和多数不一样" */
static inline int arc_mask_obj(const ArcGrid *in, uint8_t m[ARC_MAX][ARC_MAX], int kind) {
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_objects(in, objs, ARC_MAX * ARC_MAX);
    if (n < 1) return -1;
    int pick = -1;
    if (kind == 0 || kind == 1) {
        pick = 0;
        for (int i = 1; i < n; ++i)
            if (kind == 0 ? (objs[i].sz > objs[pick].sz) : (objs[i].sz < objs[pick].sz)) pick = i;
    } else {
        if (n < 3) return -1;
        for (int i = 0; i < n && pick < 0; ++i) {
            int cnt = 0;
            for (int j = 0; j < n; ++j) if (objs[j].sz == objs[i].sz) cnt++;
            if (cnt == 1) pick = i;
        }
        if (pick < 0) return -1;
    }
    memset(m, 0, ARC_MAX * ARC_MAX);
    for (int i = objs[pick].r0; i <= objs[pick].r1; ++i)
        for (int j = objs[pick].c0; j <= objs[pick].c1; ++j)
            if (in->v[i][j] != arc_bg_of(in)) m[i][j] = 1;
    return 0;
}
static inline int arc_mask_largest(const ArcGrid *in, uint8_t m[ARC_MAX][ARC_MAX]) { return arc_mask_obj(in, m, 0); }
static inline int arc_mask_smallest(const ArcGrid *in, uint8_t m[ARC_MAX][ARC_MAX]) { return arc_mask_obj(in, m, 1); }
static inline int arc_mask_odd(const ArcGrid *in, uint8_t m[ARC_MAX][ARC_MAX]) { return arc_mask_obj(in, m, 2); }

/* 被围住的区域（从边框灌不进背景的地方） */
static inline int arc_mask_holes(const ArcGrid *in, uint8_t m[ARC_MAX][ARC_MAX]) {
    int bg = arc_bg_of(in);
    static int seen[ARC_MAX][ARC_MAX];
    memset(seen, 0, sizeof seen);
    memset(m, 0, ARC_MAX * ARC_MAX);
    int stack[ARC_MAX * ARC_MAX][2]; int sp = 0;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if ((i == 0 || j == 0 || i == in->rows - 1 || j == in->cols - 1) &&
                in->v[i][j] == bg && !seen[i][j]) { seen[i][j] = 1; stack[sp][0] = i; stack[sp][1] = j; sp++; }
    while (sp) {
        int a = stack[--sp][0], b = stack[sp][1];
        int d[4][2] = { {a-1,b},{a+1,b},{a,b-1},{a,b+1} };
        for (int k = 0; k < 4; ++k) {
            int x = d[k][0], y = d[k][1];
            if (x < 0 || y < 0 || x >= in->rows || y >= in->cols) continue;
            if (seen[x][y] || in->v[x][y] != bg) continue;
            seen[x][y] = 1; stack[sp][0] = x; stack[sp][1] = y; sp++;
        }
    }
    int holes = 0;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if (in->v[i][j] == bg && !seen[i][j]) { m[i][j] = 1; holes++; }
    return holes ? 0 : -1;
}

typedef struct { const char *name; ArcMaskFn fn; } ArcMaskDef;
static const ArcMaskDef ARC_MASKS[] = {
    {"非背景", arc_mask_nonbg},
    {"最大对象", arc_mask_largest},
    {"最小对象", arc_mask_smallest},
    {"怪对象", arc_mask_odd},
    {"洞", arc_mask_holes},
    {"色1", arc_mask_c1}, {"色2", arc_mask_c2}, {"色3", arc_mask_c3},
    {"色4", arc_mask_c4}, {"色5", arc_mask_c5}, {"色6", arc_mask_c6},
};
#define ARC_N_MASKS ((int)(sizeof ARC_MASKS / sizeof ARC_MASKS[0]))

/* 把掩码内的格子刷成某个颜色（"只给最大的那个对象上色"这类题的骨架） */
static inline int arc_apply_fillmask(const ArcGrid *in, ArcGrid *out, ArcMaskFn mf, int c) {
    uint8_t m[ARC_MAX][ARC_MAX];
    if (mf(in, m)) return -1;
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if (m[i][j]) out->v[i][j] = (uint8_t)c;
    return 0;
}

/* 把 op(in) 的结果只提交在掩码里（形状必须一致） */
static inline int arc_apply_masked(const ArcGrid *in, ArcGrid *out, ArcMaskFn mf, const char *opname) {
    ArcGrid t;
    if (arc_apply_named(in, &t, opname)) return -1;
    if (t.rows != in->rows || t.cols != in->cols) return -1;
    uint8_t m[ARC_MAX][ARC_MAX];
    if (mf(in, m)) return -1;
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j)
            if (m[i][j]) out->v[i][j] = t.v[i][j];
    return 0;
}

/* ---------------- 变换表（名字 + 函数 + 类） ----------------
 * 表的**启用与顺序**在图里；这里只提供"有哪些可以执行"。 */
/* 加法类生成器：定义在后面的搜索层，表里先用前置声明 */
static inline int arc_gen_connect_h(const ArcGrid *i, ArcGrid *o);
static inline int arc_gen_connect_v(const ArcGrid *i, ArcGrid *o);
static inline int arc_gen_ray_up(const ArcGrid *i, ArcGrid *o);
static inline int arc_gen_ray_down(const ArcGrid *i, ArcGrid *o);
static inline int arc_gen_ray_left(const ArcGrid *i, ArcGrid *o);
static inline int arc_gen_ray_right(const ArcGrid *i, ArcGrid *o);

typedef struct { const char *name; const char *cls; ArcOpFn fn; } ArcOpDef;

static const ArcOpDef ARC_OPS[] = {
    {"rot90",        "几何", arc_op_rot90},
    {"rot180",       "几何", arc_op_rot180},
    {"rot270",       "几何", arc_op_rot270},
    {"fliplr",       "几何", arc_op_fliplr},
    {"flipud",       "几何", arc_op_flipud},
    {"transpose",    "几何", arc_op_transpose},
    {"antitranspose","几何", arc_op_antitranspose},
    {"up2",          "形状", arc_op_up2},
    {"up3",          "形状", arc_op_up3},
    {"down2",        "形状", arc_op_down2},
    {"down3",        "形状", arc_op_down3},
    {"tile2x2",      "形状", arc_op_tile2x2},
    {"tile1x2",      "形状", arc_op_tile1x2},
    {"tile2x1",      "形状", arc_op_tile2x1},
    {"mirrortile",   "形状", arc_op_mirrortile},
    {"crop0",        "裁剪", arc_op_crop0},
    {"cropcommon",   "裁剪", arc_op_crop_common},
    {"largestobj",   "对象", arc_op_largest_object},
    {"symh",         "补全", arc_op_symh},
    {"symv",         "补全", arc_op_symv},
    {"symhv",        "补全", arc_op_symhv},
    {"gravdown",     "重力", arc_op_grav_down},
    {"gravup",       "重力", arc_op_grav_up},
    {"gravleft",     "重力", arc_op_grav_left},
    {"gravright",    "重力", arc_op_grav_right},
    {"cropc1",        "裁剪", arc_op_cropc1},
    {"cropc2",        "裁剪", arc_op_cropc2},
    {"cropc3",        "裁剪", arc_op_cropc3},
    {"cropc4",        "裁剪", arc_op_cropc4},
    {"cropc5",        "裁剪", arc_op_cropc5},
    {"cropc6",        "裁剪", arc_op_cropc6},
    {"cropc7",        "裁剪", arc_op_cropc7},
    {"cropc8",        "裁剪", arc_op_cropc8},
    {"cropc9",        "裁剪", arc_op_cropc9},
    {"quad_tl",      "裁剪", arc_op_quad_tl},
    {"quad_tr",      "裁剪", arc_op_quad_tr},
    {"quad_bl",      "裁剪", arc_op_quad_bl},
    {"quad_br",      "裁剪", arc_op_quad_br},
    {"denoise",      "清理", arc_op_denoise},
    {"fillholes",    "清理", arc_op_fillholes},
    {"overlayor",    "叠加", arc_op_overlay_or},
    {"overlayxor",   "叠加", arc_op_overlay_xor},
    {"smallestobj", "对象", arc_op_smallestobj},
    {"fillbbox",    "对象", arc_op_fillbbox},
    {"outline",     "对象", arc_op_outline},
    {"objrank",     "对象", arc_op_objrank},
    {"seluniq",      "选择", arc_op_seluniq},
    {"selcommon",    "选择", arc_op_selcommon},
    {"selrare",      "选择", arc_op_selrare},
    {"oddobj",       "选择", arc_op_oddobj},
    {"countobj",     "计数", arc_op_countobj},
    {"countcolors",  "计数", arc_op_countcolors},
    {"overlaytbor",  "叠加", arc_op_overlay_tb_or},
    {"overlaytbxor", "叠加", arc_op_overlay_tb_xor},
    {"symdiag",      "补全", arc_op_symdiag},
    {"connecth",     "加法", arc_gen_connect_h},
    {"connectv",     "加法", arc_gen_connect_v},
    {"rayup",        "加法", arc_gen_ray_up},
    {"raydown",      "加法", arc_gen_ray_down},
    {"rayleft",      "加法", arc_gen_ray_left},
    {"rayright",     "加法", arc_gen_ray_right},
    {"identity",     "恒等", arc_op_identity},
};
#define ARC_N_OPS ((int)(sizeof ARC_OPS / sizeof ARC_OPS[0]))

/* 颜色映射（要从训练对里推，所以不是单个函数）：推得出就返回条数，否则 0 */
static inline int arc_infer_colormap(const ArcTask *t, uint8_t map[256]) {
    int have = 0;
    for (int i = 0; i < 256; ++i) map[i] = (uint8_t)i;
    for (int k = 0; k < t->n_train; ++k) {
        const ArcGrid *a = &t->tr_in[k], *b = &t->tr_out[k];
        if (a->rows != b->rows || a->cols != b->cols) return 0;
        for (int i = 0; i < a->rows; ++i)
            for (int j = 0; j < a->cols; ++j) {
                uint8_t x = a->v[i][j], y = b->v[i][j];
                if (x == y) continue;
                if (map[x] != x && map[x] != y) return 0;   /* 同一颜色映射到两个值 ⇒ 不成立 */
                map[x] = y; have = 1;
            }
    }
    return have;
}
static inline int arc_apply_colormap(const ArcGrid *in, ArcGrid *out, const uint8_t map[256]) {
    *out = *in;
    for (int i = 0; i < in->rows; ++i)
        for (int j = 0; j < in->cols; ++j) out->v[i][j] = map[in->v[i][j]];
    return 0;
}

/* ---------------- 搜索 ---------------- */
typedef struct {
    int solved;
    int depth;                 /* 1 或 2 */
    char how[192];             /* 用了哪几个变换（合取规则的名字会长；进日志/答复） */
    ArcGrid pred[ARC_MAX_TEST];
    int n_pred;
    long tried;                /* 试了多少个候选（可观测性） */
} ArcSolution;

/* 应用一个单变换（名字 → 表里那一项） */
static inline int arc_apply_named(const ArcGrid *in, ArcGrid *out, const char *name) {
    for (int i = 0; i < ARC_N_OPS; ++i)
        if (!strcmp(ARC_OPS[i].name, name)) return ARC_OPS[i].fn(in, out);
    return -1;
}

/* 用训练对验一个候选；全对才返回 1 */
static inline int arc_check_task(const ArcTask *t, const char *o1, const char *o2) {
    for (int k = 0; k < t->n_train; ++k) {
        ArcGrid a, b;
        if (arc_apply_named(&t->tr_in[k], &a, o1)) return 0;
        if (o2) { if (arc_apply_named(&a, &b, o2)) return 0; if (!arc_grid_eq(&b, &t->tr_out[k])) return 0; }
        else if (!arc_grid_eq(&a, &t->tr_out[k])) return 0;
    }
    return 1;
}

/* 候选搜索：**束搜索**（§6.779 第二轮）
 *
 * 为什么换掉"穷举深度 2"：43 个原语穷举深度 2 是 1849 个候选，深度 3 就是 8 万个 —— 穷举不动；
 * ARC 的解法却常常是 3~4 步（"选出子块 → 换色 → 平铺"）。束搜索用**部分命中数**当分数：
 * 每一层保留 top-W 个"已经对上最多训练对"的候选，再往下接一步。这样深度 3~4 也只要
 * W×43×深度 次网格运算（W=8 时约 1.4 千次），单个任务毫秒级。
 *
 * 判据没变：**全部训练对都对上**才算解出；解出后把同一条路径套到测试输入上。 */
#define ARC_MAX_DEPTH 4
typedef struct {
    const ArcTask *t;
    const char *const *en;
    int n_en;
    int max_depth;
    ArcGrid cur[ARC_MAX_TRAIN];
    int score;
    const char *path[ARC_MAX_DEPTH];
    int len;
    long nodes, budget;
    int found;
    ArcGrid hit[ARC_MAX_TRAIN];
    const char *hit_path[ARC_MAX_DEPTH];
    int hit_len;
} ArcSearch;

static inline int arc_apply_seq(const ArcGrid *in, ArcGrid *out, const char *const *path, int len) {
    ArcGrid c = *in;
    for (int i = 0; i < len; ++i) {
        ArcGrid nxt;
        if (arc_apply_named(&c, &nxt, path[i])) return -1;
        c = nxt;
    }
    *out = c;
    return 0;
}

/* 深度优先 + 两条剪枝（§6.779 第二轮）：
 *   ① **形状可达**：每一步之后，第一对训练网格的尺寸必须等于"输入尺寸"或"输出尺寸" ——
 *      中间步骤只会在这两种尺寸之间过渡（先裁剪再放大这种罕见组合被牺牲，换来 10 倍速度）；
 *   ② **节点预算**：整个任务最多试 budget 个候选，超了就停（不许一个任务把整包拖死）。 */
static void arc_dfs(ArcSearch *S, int depth) {
    const ArcTask *t = S->t;
    if (S->found || S->nodes > S->budget) return;
    if (depth > 0 && S->score == t->n_train) {
        for (int k = 0; k < t->n_train; ++k) S->hit[k] = S->cur[k];
        for (int k = 0; k < S->len; ++k) S->hit_path[k] = S->path[k];
        S->hit_len = S->len;
        S->found = 1;
        return;
    }
    if (depth >= S->max_depth) return;
    for (int j = 0; j < S->n_en && !S->found; ++j) {
        ArcGrid save[ARC_MAX_TRAIN];
        for (int k = 0; k < t->n_train; ++k) save[k] = S->cur[k];
        int ok = 1;
        for (int k = 0; k < t->n_train; ++k) {
            ArcGrid tmp;
            if (arc_apply_named(&S->cur[k], &tmp, S->en[j])) { ok = 0; break; }
            S->cur[k] = tmp;
        }
        if (!ok) {
            for (int k = 0; k < t->n_train; ++k) S->cur[k] = save[k];
            continue;
        }
        /* 不做形状剪枝（试过：会把"先裁剪再放大"这类合法路径剪掉，实测 train 30→27）。
         * 换成**节点预算**兜底：预算内穷举，超了就停并明确报"没解出"。 */
        S->nodes++;
        S->score = 0;
        for (int k = 0; k < t->n_train; ++k)
            if (arc_grid_eq(&S->cur[k], &t->tr_out[k])) S->score++;
        S->path[S->len++] = S->en[j];
        arc_dfs(S, depth + 1);
        S->len--;
        for (int k = 0; k < t->n_train; ++k) S->cur[k] = save[k];
        S->score = 0;
        for (int k = 0; k < t->n_train; ++k)
            if (arc_grid_eq(&S->cur[k], &t->tr_out[k])) S->score++;
    }
}

/* ---------------- 关系归纳（§6.787）：用"哪些对象变了"当监督信号学谓词 ----------------
 * 前四个"再加一层规则"的尝试都是 +0，原因是**盲枚举谓词**。这一轮换成**有监督的归纳**：
 *   ① 每个训练对里，把输入的对象逐个标成「没变 / 刷成了某色 / 删掉了 / 混合（放弃）」；
 *   ② 若所有"变了"的对象动作一致（同一个动作、同一个颜色），就找一条**谓词**把它们和"没变"的分开；
 *   ③ 谓词来自对象属性 + **对象之间的关系**（同色于最大的、与最大的一行对齐、相邻、包含……）；
 *   ④ 找到的谓词套到测试输入上。
 * 这就是"从例子里学规则"，不是"把规则表全枚举一遍"。 */

typedef struct { int kind; int color; } ArcObjLabel;   /* 0=没变 1=刷色 2=删掉 3=混合 */

typedef int (*ArcPredFn)(const ArcGrid *g, const ArcObj *objs, int n, int i);

/* 用"对象格子"和输出逐格比，给每个对象打标签；返回 0 成功，-1 表示这题用不了（有混合变化） */
static inline int arc_label_changes(const ArcGrid *in, const ArcGrid *out,
                                    const ArcObj *objs, int n, ArcObjLabel *lb) {
    (void)objs;
    if (in->rows != out->rows || in->cols != out->cols) return -1;
    int bg = arc_common_color(in);
    /* 背景格也必须原样：这条模型只解释"对象内部的变化"，
     * 一有背景格被改（画线、填洞、加边框）就判"这题用不了"，别硬套。 */
    for (int r = 0; r < in->rows; ++r)
        for (int c = 0; c < in->cols; ++c)
            if (in->v[r][c] == bg && out->v[r][c] != bg) return -1;
    int lab[ARC_MAX][ARC_MAX];
    ArcObj tmp[ARC_MAX * ARC_MAX];
    int m = arc_label_objects(in, lab, tmp, ARC_MAX * ARC_MAX);
    if (m != n) return -1;
    for (int i = 0; i < n; ++i) {
        int changed = 0, mixed = 0, to = -1;
        for (int r = 0; r < in->rows; ++r)
            for (int c = 0; c < in->cols; ++c) {
                if (lab[r][c] != i + 1) continue;
                uint8_t a = in->v[r][c], b = out->v[r][c];
                if (a == b) continue;
                changed++;
                if (to < 0) to = b;
                else if (to != b) mixed = 1;
            }
        if (!changed) lb[i].kind = 0;
        else if (mixed) return -1;
        else if (to == bg) { lb[i].kind = 2; lb[i].color = bg; }
        else { lb[i].kind = 1; lb[i].color = to; }
    }
    return 0;
}

/* ---------------- 谓词（属性 + 关系） ---------------- */
static inline int arc_is_largest(const ArcObj *objs, int n, int i) {
    for (int j = 0; j < n; ++j) if (objs[j].sz > objs[i].sz) return 0;
    return 1;
}
static inline int arc_is_smallest(const ArcObj *objs, int n, int i) {
    for (int j = 0; j < n; ++j) if (objs[j].sz < objs[i].sz) return 0;
    return 1;
}
static inline int arc_size_uniq(const ArcObj *objs, int n, int i) {
    int same = 0;
    for (int j = 0; j < n; ++j) if (objs[j].sz == objs[i].sz) same++;
    return same == 1;
}
static inline int arc_size_major(const ArcObj *objs, int n, int i) {
    int same = 0;
    for (int j = 0; j < n; ++j) if (objs[j].sz == objs[i].sz) same++;
    return same > 1;
}
static inline int arc_color_uniq(const ArcObj *objs, int n, int i) {
    int same = 0;
    for (int j = 0; j < n; ++j) if (objs[j].color == objs[i].color) same++;
    return same == 1;
}
static inline int arc_touch_border(const ArcGrid *g, const ArcObj *o) {
    return o->r0 == 0 || o->c0 == 0 || o->r1 == g->rows - 1 || o->c1 == g->cols - 1;
}

/* 关系：和"最大对象"同色 / 同行对齐 / 同列对齐 / 同尺寸 / 相邻 / 包含 */
static inline int arc_rel_to_largest_color(const ArcObj *objs, int n, int i) {
    int big = 0;
    for (int j = 1; j < n; ++j) if (objs[j].sz > objs[big].sz) big = j;
    return i != big && objs[i].color == objs[big].color;
}
static inline int arc_rel_aligned_h(const ArcObj *objs, int n, int i) {
    int big = 0;
    for (int j = 1; j < n; ++j) if (objs[j].sz > objs[big].sz) big = j;
    return i != big && objs[i].r0 == objs[big].r0 && objs[i].r1 == objs[big].r1;
}
static inline int arc_rel_aligned_v(const ArcObj *objs, int n, int i) {
    int big = 0;
    for (int j = 1; j < n; ++j) if (objs[j].sz > objs[big].sz) big = j;
    return i != big && objs[i].c0 == objs[big].c0 && objs[i].c1 == objs[big].c1;
}
static inline int arc_rel_same_size_as_majority(const ArcObj *objs, int n, int i) {
    int best = 0, bc = 0;
    for (int j = 0; j < n; ++j) {
        int same = 0;
        for (int k = 0; k < n; ++k) if (objs[k].sz == objs[j].sz) same++;
        if (same > bc) { bc = same; best = j; }
    }
    return objs[i].sz == objs[best].sz;
}
static inline int arc_rel_adjacent(const ArcObj *objs, int n, int i) {
    for (int j = 0; j < n; ++j) {
        if (i == j) continue;
        int gapr = (objs[j].r0 > objs[i].r1) ? objs[j].r0 - objs[i].r1
                 : (objs[i].r0 > objs[j].r1) ? objs[i].r0 - objs[j].r1 : 0;
        int gapc = (objs[j].c0 > objs[i].c1) ? objs[j].c0 - objs[i].c1
                 : (objs[i].c0 > objs[j].c1) ? objs[i].c0 - objs[j].c1 : 0;
        if (gapr <= 1 && gapc <= 1) return 1;
    }
    return 0;
}
static inline int arc_rel_inside(const ArcObj *objs, int n, int i) {
    for (int j = 0; j < n; ++j) {
        if (i == j) continue;
        if (objs[i].r0 >= objs[j].r0 && objs[i].r1 <= objs[j].r1 &&
            objs[i].c0 >= objs[j].c0 && objs[i].c1 <= objs[j].c1) return 1;
    }
    return 0;
}
static inline int arc_pred_border(const ArcGrid *g, const ArcObj *objs, int n, int i) {
    (void)n; return arc_touch_border(g, &objs[i]);
}
static inline int arc_pred_uniqsize(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_size_uniq(o, n, i); }
static inline int arc_pred_majsize(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_size_major(o, n, i); }
static inline int arc_pred_largest(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_is_largest(o, n, i); }
static inline int arc_pred_smallest(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_is_smallest(o, n, i); }
static inline int arc_pred_uniqcolor(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_color_uniq(o, n, i); }
static inline int arc_pred_relsame(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_rel_to_largest_color(o, n, i); }
static inline int arc_pred_alignh(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_rel_aligned_h(o, n, i); }
static inline int arc_pred_alignv(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_rel_aligned_v(o, n, i); }
static inline int arc_pred_majsize_same(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_rel_same_size_as_majority(o, n, i); }
static inline int arc_pred_adjacent(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_rel_adjacent(o, n, i); }
static inline int arc_pred_inside(const ArcGrid *g, const ArcObj *o, int n, int i) { (void)g; return arc_rel_inside(o, n, i); }

#define ARC_COLOR_PRED_FN(C) \
    static inline int arc_pred_col##C(const ArcGrid *g, const ArcObj *o, int n, int i) { \
        (void)g; (void)n; return o[i].color == C; \
    }
ARC_COLOR_PRED_FN(1) ARC_COLOR_PRED_FN(2) ARC_COLOR_PRED_FN(3)
ARC_COLOR_PRED_FN(4) ARC_COLOR_PRED_FN(5) ARC_COLOR_PRED_FN(6)
ARC_COLOR_PRED_FN(7) ARC_COLOR_PRED_FN(8) ARC_COLOR_PRED_FN(9)

typedef struct { const char *name; ArcPredFn fn; } ArcPredDef;
static const ArcPredDef ARC_PREDS[] = {
    {"碰边框", arc_pred_border},       {"面积唯一", arc_pred_uniqsize},
    {"面积多数", arc_pred_majsize},     {"最大", arc_pred_largest},
    {"最小", arc_pred_smallest},       {"颜色唯一", arc_pred_uniqcolor},
    {"同色于最大", arc_pred_relsame},   {"与最大同行", arc_pred_alignh},
    {"与最大同列", arc_pred_alignv},    {"面积同多数", arc_pred_majsize_same},
    {"相邻", arc_pred_adjacent},       {"被包含", arc_pred_inside},
    {"色1对象", arc_pred_col1}, {"色2对象", arc_pred_col2}, {"色3对象", arc_pred_col3},
    {"色4对象", arc_pred_col4}, {"色5对象", arc_pred_col5}, {"色6对象", arc_pred_col6},
    {"色7对象", arc_pred_col7}, {"色8对象", arc_pred_col8}, {"色9对象", arc_pred_col9},
};
#define ARC_N_PREDS ((int)(sizeof ARC_PREDS / sizeof ARC_PREDS[0]))

/* 把一个动作套到"谓词选中的对象"上 */
static inline int arc_apply_pred(const ArcGrid *in, ArcGrid *out, int pi, int pj, int kind, int color) {
    int lab[ARC_MAX][ARC_MAX];
    ArcObj objs[ARC_MAX * ARC_MAX];
    int n = arc_label_objects(in, lab, objs, ARC_MAX * ARC_MAX);
    if (n < 1) return -1;
    uint8_t sel[ARC_MAX * ARC_MAX];
    int any = 0;
    for (int i = 0; i < n; ++i) {
        int hit = ARC_PREDS[pi].fn(in, objs, n, i);
        if (hit && pj >= 0) hit = ARC_PREDS[pj].fn(in, objs, n, i);
        sel[i] = (uint8_t)hit;
        any |= hit;
    }
    if (!any) return -1;
    int bg = arc_common_color(in);
    *out = *in;
    for (int r = 0; r < in->rows; ++r)
        for (int c = 0; c < in->cols; ++c) {
            int id = lab[r][c];
            if (id <= 0 || !sel[id - 1]) continue;
            out->v[r][c] = (kind == 1) ? (uint8_t)color : (uint8_t)bg;
        }
    return 0;
}

/* 学一条谓词规则：返回 1 = 解出 */
static inline int arc_stage_learn(const ArcTask *t, ArcSolution *sol) {
    if (t->n_train < 1) return 0;
    int act_kind = -1, act_color = -1;
    for (int k = 0; k < t->n_train; ++k) {
        if (t->tr_in[k].rows != t->tr_out[k].rows || t->tr_in[k].cols != t->tr_out[k].cols) return 0;
        int lab[ARC_MAX][ARC_MAX];
        ArcObj objs[ARC_MAX * ARC_MAX];
        int n = arc_label_objects(&t->tr_in[k], lab, objs, ARC_MAX * ARC_MAX);
        if (n < 1) return 0;
        ArcObjLabel lb[ARC_MAX * ARC_MAX];
        if (arc_label_changes(&t->tr_in[k], &t->tr_out[k], objs, n, lb)) return 0;
        for (int i = 0; i < n; ++i) {
            if (lb[i].kind == 0) continue;
            if (act_kind < 0) { act_kind = lb[i].kind; act_color = lb[i].color; }
            else if (act_kind != lb[i].kind || (act_kind == 1 && act_color != lb[i].color)) return 0;
        }
    }
    if (act_kind < 0) return 0;
    for (int pi = 0; pi < ARC_N_PREDS; ++pi)
        for (int pj = pi; pj < ARC_N_PREDS; ++pj) {
            int ok = 1;
            for (int k = 0; k < t->n_train && ok; ++k) {
                ArcGrid o;
                if (arc_apply_pred(&t->tr_in[k], &o, pi, (pj == pi ? -1 : pj), act_kind, act_color) ||
                    !arc_grid_eq(&o, &t->tr_out[k])) ok = 0;
            }
            if (!ok) continue;
            int good = 1;
            for (int k = 0; k < t->n_test && good; ++k)
                if (arc_apply_pred(&t->te_in[k], &sol->pred[k], pi, (pj == pi ? -1 : pj),
                                   act_kind, act_color)) good = 0;
            if (!good) continue;
            sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 1;
            if (pj == pi)
                snprintf(sol->how, sizeof sol->how, "学:%s·%s", ARC_PREDS[pi].name,
                         act_kind == 1 ? "刷色" : "删掉");
            else
                snprintf(sol->how, sizeof sol->how, "学:%s∧%s·%s", ARC_PREDS[pi].name,
                         ARC_PREDS[pj].name, act_kind == 1 ? "刷色" : "删掉");
            return 1;
        }
    return 0;
}



/* ---------------- 背景动作（§6.788）：学"新加的格子"是怎么来的 ----------------
 * 上一轮量出：同尺寸题里 252 道的差别出现在**背景**（画线、投影、填洞、补对称），
 * 而"对象谓词"那条模型只覆盖 18 道。这一轮做加法侧的归纳：
 *   ① 只挑"输入里的非背景格一个没动、只是多了格子"的题（纯加法）；
 *   ② 假设新增格子来自一个**几何生成器**（连点成线 / 射线投影 / 填洞 / 补对称 / 描边），
 *      允许两个生成器**复合**；
 *   ③ 用训练对验证，再套到测试输入。
 * 生成器都是"从输入自己算出来"的，不需要测试答案。 */

static inline int arc_gen_connect(const ArcGrid *in, ArcGrid *out, int vertical) {
    *out = *in;
    int bg = arc_common_color(in);
    int filled = 0;
    if (!vertical) {
        for (int r = 0; r < in->rows; ++r)
            for (int c1 = 0; c1 < in->cols; ++c1) {
                if (in->v[r][c1] == bg) continue;
                for (int c2 = c1 + 2; c2 < in->cols; ++c2) {
                    if (in->v[r][c2] != in->v[r][c1]) continue;
                    int gap = 0;
                    for (int c = c1 + 1; c < c2; ++c) if (in->v[r][c] != bg) gap = 1;
                    if (!gap) {
                        for (int c = c1 + 1; c < c2; ++c) { out->v[r][c] = in->v[r][c1]; filled++; }
                    }
                    break;
                }
            }
    } else {
        for (int c = 0; c < in->cols; ++c)
            for (int r1 = 0; r1 < in->rows; ++r1) {
                if (in->v[r1][c] == bg) continue;
                for (int r2 = r1 + 2; r2 < in->rows; ++r2) {
                    if (in->v[r2][c] != in->v[r1][c]) continue;
                    int gap = 0;
                    for (int r = r1 + 1; r < r2; ++r) if (in->v[r][c] != bg) gap = 1;
                    if (!gap) {
                        for (int r = r1 + 1; r < r2; ++r) { out->v[r][c] = in->v[r1][c]; filled++; }
                    }
                    break;
                }
            }
    }
    return filled ? 0 : -1;
}

/* 射线：每个非背景格朝一个方向画到边界（只覆盖背景格） */
static inline int arc_gen_ray(const ArcGrid *in, ArcGrid *out, int dir) {
    *out = *in;
    int bg = arc_common_color(in);
    int painted = 0;
    for (int r = 0; r < in->rows; ++r)
        for (int c = 0; c < in->cols; ++c) {
            if (in->v[r][c] == bg) continue;
            int dr = 0, dc = 0;
            if (dir == 0) dr = -1;
            else if (dir == 1) dr = 1;
            else if (dir == 2) dc = -1;
            else dc = 1;
            for (int i = r + dr, j = c + dc; i >= 0 && j >= 0 && i < in->rows && j < in->cols; i += dr, j += dc)
                if (out->v[i][j] == bg) { out->v[i][j] = in->v[r][c]; painted++; }
        }
    return painted ? 0 : -1;
}
static inline int arc_gen_ray_up(const ArcGrid *i, ArcGrid *o)    { return arc_gen_ray(i, o, 0); }
static inline int arc_gen_ray_down(const ArcGrid *i, ArcGrid *o)  { return arc_gen_ray(i, o, 1); }
static inline int arc_gen_ray_left(const ArcGrid *i, ArcGrid *o)  { return arc_gen_ray(i, o, 2); }
static inline int arc_gen_ray_right(const ArcGrid *i, ArcGrid *o) { return arc_gen_ray(i, o, 3); }
static inline int arc_gen_connect_h(const ArcGrid *i, ArcGrid *o) { return arc_gen_connect(i, o, 0); }
static inline int arc_gen_connect_v(const ArcGrid *i, ArcGrid *o) { return arc_gen_connect(i, o, 1); }

/* 复用已有原语当生成器：填洞 / 描边 / 水平对称补全 / 垂直对称补全 */
static inline int arc_gen_holes(const ArcGrid *i, ArcGrid *o)  { return arc_op_fillholes(i, o); }
static inline int arc_gen_outline(const ArcGrid *i, ArcGrid *o) { return arc_op_outline(i, o); }
static inline int arc_gen_symh(const ArcGrid *i, ArcGrid *o)   { return arc_op_symh(i, o); }
static inline int arc_gen_symv(const ArcGrid *i, ArcGrid *o)   { return arc_op_symv(i, o); }

static inline void arc_solve(const ArcTask *t, const char *const *enabled, int n_en,
                             int max_depth, int beam_w, ArcSolution *sol) {
    memset(sol, 0, sizeof *sol);
    if (!t || t->n_train <= 0 || t->n_test <= 0) return;
    /* ① 常量输出 */
    int same = 1;
    for (int k = 1; k < t->n_train; ++k)
        if (!arc_grid_eq(&t->tr_out[0], &t->tr_out[k])) { same = 0; break; }
    if (same) {
        for (int k = 0; k < t->n_test; ++k) sol->pred[k] = t->tr_out[0];
        sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 0;
        snprintf(sol->how, sizeof sol->how, "常量输出");
        return;
    }
    /* ② 颜色映射 */
    uint8_t map[256];
    if (arc_infer_colormap(t, map)) {
        int ok = 1;
        for (int k = 0; k < t->n_train && ok; ++k) {
            ArcGrid b; arc_apply_colormap(&t->tr_in[k], &b, map);
            if (!arc_grid_eq(&b, &t->tr_out[k])) ok = 0;
        }
        if (ok) {
            for (int k = 0; k < t->n_test; ++k) arc_apply_colormap(&t->te_in[k], &sol->pred[k], map);
            sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 1;
            snprintf(sol->how, sizeof sol->how, "colormap");
            return;
        }
    }
    /* ③s **关系归纳**：用「哪些对象变了」当监督，学一条谓词（属性 + 对象关系） */
    if (arc_stage_learn(t, sol)) return;
    /* ③d 固定位置子块：所有训练对都能用**同一个 (r,c)** 裁出来（"取右下角那块"） */
    {
        int oh = t->tr_out[0].rows, ow = t->tr_out[0].cols, same = 1;
        for (int k = 1; k < t->n_train; ++k)
            if (t->tr_out[k].rows != oh || t->tr_out[k].cols != ow) { same = 0; break; }
        for (int k = 0; k < t->n_train && same; ++k)
            if (t->tr_in[k].rows < oh || t->tr_in[k].cols < ow) same = 0;
        for (int r = 0; r < ARC_MAX && same; ++r)
            for (int c = 0; c < ARC_MAX && same; ++c) {
                int ok = 1;
                for (int k = 0; k < t->n_train && ok; ++k) {
                    ArcGrid o;
                    if (arc_sub_at(&t->tr_in[k], &o, r, c, oh, ow) || !arc_grid_eq(&o, &t->tr_out[k])) ok = 0;
                }
                if (!ok) {
                    if (r + oh >= ARC_MAX && c + ow >= ARC_MAX) break;
                    continue;
                }
                int good = 1;
                for (int k = 0; k < t->n_test && good; ++k)
                    if (arc_sub_at(&t->te_in[k], &sol->pred[k], r, c, oh, ow)) good = 0;
                if (good) {
                    sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 1;
                    snprintf(sol->how, sizeof sol->how, "子块@%d,%d", r, c);
                    return;
                }
            }
    }
    /* ③e 加 1 像素边框（颜色 0..9）/ 去 1 像素边框 */
    for (int c = 0; c <= 9; ++c) {
        int ok = 1;
        for (int k = 0; k < t->n_train && ok; ++k) {
            ArcGrid o;
            if (arc_pad1(&t->tr_in[k], &o, c) || !arc_grid_eq(&o, &t->tr_out[k])) ok = 0;
        }
        if (!ok) continue;
        int good = 1;
        for (int k = 0; k < t->n_test && good; ++k)
            if (arc_pad1(&t->te_in[k], &sol->pred[k], c)) good = 0;
        if (good) {
            sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 1;
            snprintf(sol->how, sizeof sol->how, "加边框%d", c);
            return;
        }
    }
    {
        int ok = 1;
        for (int k = 0; k < t->n_train && ok; ++k) {
            ArcGrid o;
            if (arc_unpad1(&t->tr_in[k], &o) || !arc_grid_eq(&o, &t->tr_out[k])) ok = 0;
        }
        if (ok) {
            int good = 1;
            for (int k = 0; k < t->n_test && good; ++k)
                if (arc_unpad1(&t->te_in[k], &sol->pred[k])) good = 0;
            if (good) {
                sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 1;
                snprintf(sol->how, sizeof sol->how, "去边框");
                return;
            }
        }
    }
    /* ③a 局部上色：掩码内刷成同一个颜色（"只给最大对象刷成 3" 这类题） */
    for (int mi = 0; mi < ARC_N_MASKS; ++mi)
        for (int c = 1; c <= 9; ++c) {
            int ok = 1;
            for (int k = 0; k < t->n_train && ok; ++k) {
                ArcGrid o;
                if (arc_apply_fillmask(&t->tr_in[k], &o, ARC_MASKS[mi].fn, c) ||
                    !arc_grid_eq(&o, &t->tr_out[k])) ok = 0;
            }
            if (!ok) continue;
            int good = 1;
            for (int k = 0; k < t->n_test && good; ++k)
                if (arc_apply_fillmask(&t->te_in[k], &sol->pred[k], ARC_MASKS[mi].fn, c)) good = 0;
            if (!good) continue;
            sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 2;
            snprintf(sol->how, sizeof sol->how, "%s·刷成%d", ARC_MASKS[mi].name, c);
            return;
        }
    /* ③b 局部换色：掩码内按训练对推出来的颜色映射换（"最大对象整体变色"但每个色不同） */
    {
        uint8_t mmap[256];
        if (arc_infer_colormap(t, mmap)) {
            for (int mi = 0; mi < ARC_N_MASKS; ++mi) {
                int ok = 1;
                for (int k = 0; k < t->n_train && ok; ++k) {
                    ArcGrid t2, o;
                    if (arc_apply_colormap(&t->tr_in[k], &t2, mmap)) ok = 0;
                    else if (arc_apply_masked(&t->tr_in[k], &o, ARC_MASKS[mi].fn, "identity")) ok = 0;
                    else {
                        uint8_t m[ARC_MAX][ARC_MAX];
                        if (ARC_MASKS[mi].fn(&t->tr_in[k], m)) { ok = 0; break; }
                        ArcGrid r = t->tr_in[k];
                        for (int i = 0; i < r.rows; ++i)
                            for (int j = 0; j < r.cols; ++j)
                                if (m[i][j]) r.v[i][j] = t2.v[i][j];
                        if (!arc_grid_eq(&r, &t->tr_out[k])) ok = 0;
                    }
                }
                if (!ok) continue;
                int good = 1;
                for (int k = 0; k < t->n_test && good; ++k) {
                    uint8_t m[ARC_MAX][ARC_MAX];
                    ArcGrid t2;
                    if (arc_apply_colormap(&t->te_in[k], &t2, mmap) || ARC_MASKS[mi].fn(&t->te_in[k], m)) { good = 0; break; }
                    sol->pred[k] = t->te_in[k];
                    for (int i = 0; i < sol->pred[k].rows; ++i)
                        for (int j = 0; j < sol->pred[k].cols; ++j)
                            if (m[i][j]) sol->pred[k].v[i][j] = t2.v[i][j];
                }
                if (!good) continue;
                sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 2;
                snprintf(sol->how, sizeof sol->how, "%s·掩码换色", ARC_MASKS[mi].name);
                return;
            }
        }
    }
    /* ③c 局部编辑：mask × 保形变换（整张图一起动的原语碰不到的那一类） */
    for (int mi = 0; mi < ARC_N_MASKS; ++mi)
        for (int j = 0; j < n_en; ++j) {
            int ok = 1;
            for (int k = 0; k < t->n_train && ok; ++k) {
                ArcGrid o;
                if (arc_apply_masked(&t->tr_in[k], &o, ARC_MASKS[mi].fn, enabled[j]) ||
                    !arc_grid_eq(&o, &t->tr_out[k])) ok = 0;
            }
            if (!ok) continue;
            int good = 1;
            for (int k = 0; k < t->n_test && good; ++k)
                if (arc_apply_masked(&t->te_in[k], &sol->pred[k], ARC_MASKS[mi].fn, enabled[j])) good = 0;
            if (!good) continue;
            sol->n_pred = t->n_test; sol->solved = 1; sol->depth = 2;
            snprintf(sol->how, sizeof sol->how, "%s·%s", ARC_MASKS[mi].name, enabled[j]);
            return;
        }
    if (n_en <= 0) return;
    if (max_depth < 1) max_depth = 1;
    if (max_depth > ARC_MAX_DEPTH) max_depth = ARC_MAX_DEPTH;
    ArcSearch *S = (ArcSearch *)calloc(1, sizeof *S);
    if (!S) return;
    S->t = t; S->en = enabled; S->n_en = n_en;
    S->budget = (beam_w > 0) ? (long)beam_w * 30000L : 240000L;   /* 预算档位（8 → 24 万节点） */
    for (int k = 0; k < t->n_train; ++k) S->cur[k] = t->tr_in[k];
    S->score = 0;
    for (int k = 0; k < t->n_train; ++k)
        if (arc_grid_eq(&S->cur[k], &t->tr_out[k])) S->score++;
    /* **迭代加深**：先把浅的解找干净，再往深走。
     * 为什么必须这样：纯 DFS 会先钻进第一个算子的子树，可能用 3 步解出来，
     * 而 1 步的解就在后面 —— 报出来的 `how` 就变成"绕远路"，判据和人都看不懂（实测踩过）。 */
    for (int d = 1; d <= max_depth && !S->found; ++d) {
        S->max_depth = d;
        S->len = 0;
        for (int k = 0; k < t->n_train; ++k) S->cur[k] = t->tr_in[k];
        S->score = 0;
        for (int k = 0; k < t->n_train; ++k)
            if (arc_grid_eq(&S->cur[k], &t->tr_out[k])) S->score++;
        arc_dfs(S, 0);
    }
    sol->tried = S->nodes;
    if (S->found) {
        int ok = 1;
        for (int k = 0; k < t->n_test && ok; ++k)
            if (arc_apply_seq(&t->te_in[k], &sol->pred[k], S->hit_path, S->hit_len)) ok = 0;
        if (ok) {
            sol->solved = 1; sol->n_pred = t->n_test; sol->depth = S->hit_len;
            size_t off = 0; sol->how[0] = 0;
            for (int q = 0; q < S->hit_len; ++q) {
                int w = snprintf(sol->how + off, sizeof sol->how - off, "%s%s",
                                 q ? "→" : "", S->hit_path[q]);
                if (w < 0) break;
                off += (size_t)w;
                if (off >= sizeof sol->how) break;
            }
        }
    }
    free(S);
}

/* 网格 → 紧凑串（`[012/345]`），给 CLI 和基准脚本用 */
static inline void arc_grid_str(const ArcGrid *g, char *out, size_t cap) {
    size_t k = 0;
    if (k + 1 < cap) out[k++] = '[';
    for (int i = 0; i < g->rows; ++i) {
        if (i && k + 1 < cap) out[k++] = '/';
        for (int j = 0; j < g->cols; ++j) {
            if (k + 1 < cap) out[k++] = (char)('0' + (g->v[i][j] % 10));
        }
    }
    if (k + 1 < cap) out[k++] = ']';
    out[k < cap ? k : cap - 1] = 0;
}

#endif /* ARC_H */
