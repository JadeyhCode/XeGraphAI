/* units.h — **单位换算 / 百分比**（§6.800）：通用能力，表可以下沉到图
 *
 * 原则（和算术一样）：不背答案、只做通用运算；**算不了就说算不了**（量纲不符、缺单位、超范围）。
 *
 * 表怎么放：内置一张（长度/质量/时间/体积/温度），`::unit sync` 把它写成图事实：
 *     单位表:长度 --[有]--> 厘米
 *     厘米 --[倍数]--> 1/100          （1 厘米 = 1/100 米）
 *     摄氏度 --[偏移]--> 27315/100    （开尔文 = 摄氏度*1 + 273.15）
 * 换算时**图里的值优先**（`units_graph_apply`）；`AIP_UNITS=builtin` 时忽略图，
 * 用来量"图上那张表是不是真能顶住"。
 */
#ifndef UNITS_H
#define UNITS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "calc.h"

typedef struct {
    const char *dim;         /* 长度 / 质量 / 时间 / 体积 / 温度 */
    const char *unit;        /* 中文单位名（最长优先匹配） */
    CalcRat    scale;        /* 1 单位 = scale 个基准单位 */
    CalcRat    offset;       /* 温度用：基准 = v*scale + offset */
} UnitDef;

#define UNITS_MAX 48
static UnitDef g_units[UNITS_MAX];
static int g_units_n = -1;          /* -1 = 还没装 */

/* 比例都用有理数写，避免 0.01 这种二进制不精确值 */
static inline CalcRat u_r(int64_t n, int64_t d) { return calc_mk(n, d); }

static inline void units_builtin(void) {
    if (g_units_n >= 0) return;
    g_units_n = 0;
    struct { const char *dim, *u; int64_t sn, sd, on, od; } T[] = {
        {"长度", "公里",   1000, 1,      0, 1},
        {"长度", "千米",   1000, 1,      0, 1},
        {"长度", "米",        1, 1,      0, 1},
        {"长度", "分米",      1, 10,     0, 1},
        {"长度", "厘米",      1, 100,    0, 1},
        {"长度", "毫米",      1, 1000,   0, 1},
        {"长度", "英里", 1609344, 1000,  0, 1},
        {"长度", "英尺",   3048, 10000,  0, 1},
        {"长度", "英寸",    254, 10000,  0, 1},
        {"质量", "吨",     1000, 1,      0, 1},
        {"质量", "千克",      1, 1,      0, 1},
        {"质量", "公斤",      1, 1,      0, 1},
        {"质量", "克",        1, 1000,   0, 1},
        {"质量", "斤",        1, 2,      0, 1},
        {"质量", "磅", 45359237, 100000000, 0, 1},
        {"时间", "天",     86400, 1,     0, 1},
        {"时间", "小时",    3600, 1,     0, 1},
        {"时间", "分钟",      60, 1,     0, 1},
        {"时间", "秒",         1, 1,     0, 1},
        {"体积", "立方米",  1000, 1,     0, 1},
        {"体积", "升",         1, 1,     0, 1},
        {"体积", "毫升",       1, 1000,  0, 1},
        {"温度", "开尔文",     1, 1,     0, 1},
        {"温度", "摄氏度",     1, 1, 27315, 100},   /* K = C + 273.15 */
        {"温度", "华氏度",     5, 9, 45967, 180},   /* K = (F+459.67)*5/9 */
    };
    for (size_t i = 0; i < sizeof T / sizeof T[0] && g_units_n < UNITS_MAX; ++i) {
        g_units[g_units_n].dim = T[i].dim;
        g_units[g_units_n].unit = T[i].u;
        g_units[g_units_n].scale = u_r(T[i].sn, T[i].sd);
        g_units[g_units_n].offset = u_r(T[i].on, T[i].od);
        ++g_units_n;
    }
}

static inline const UnitDef *units_find(const char *name) {
    if (!name) return NULL;
    units_builtin();
    const UnitDef *best = NULL;
    size_t bl = 0;
    for (int i = 0; i < g_units_n; ++i) {
        size_t l = strlen(g_units[i].unit);
        if (l > bl && !strncmp(name, g_units[i].unit, l)) { best = &g_units[i]; bl = l; }
    }
    return best;
}

/* 从一个位置开始，找最长的单位名（用来在句子里定位两个单位） */
static inline const UnitDef *units_at(const char *p, size_t *len) {
    if (!p || !*p) return NULL;
    units_builtin();
    const UnitDef *best = NULL;
    size_t bl = 0;
    for (int i = 0; i < g_units_n; ++i) {
        size_t l = strlen(g_units[i].unit);
        if (l > bl && !strncmp(p, g_units[i].unit, l)) { best = &g_units[i]; bl = l; }
    }
    if (len) *len = bl;
    return best;
}

/* 换算：v 从 from 到 to。量纲不同返回 0（拒绝，不猜） */
static inline int units_convert(CalcRat v, const UnitDef *from, const UnitDef *to, CalcRat *out) {
    if (!from || !to || !out) return 0;
    if (strcmp(from->dim, to->dim) != 0) return 0;          /* 长度→质量 这种必须拒绝 */
    CalcRat base = calc_add(calc_mul(v, from->scale), from->offset);
    if (!base.ok) return 0;
    CalcRat t = calc_sub(base, to->offset);
    if (!t.ok) return 0;
    CalcRat r = calc_div(t, to->scale);
    if (!r.ok) return 0;
    *out = r;
    return 1;
}

/* 句子里抽两个单位做换算：`3 米 等于 多少 厘米` / `1 小时 转 分钟` / `100 厘米 是 几 米` */
static inline int units_answer(const char *input, char *out, size_t cap) {
    if (!input || !out || cap == 0) return 0;
    /* 先找第一个数 */
    const char *p = input;
    while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
    if (*p < '0' && *p != '-' && (*p < '0' || *p > '9')) { if (!*p) return 0; }
    if (*p != '-' && (*p < '0' || *p > '9')) return 0;
    /* 取数字（整数/小数） */
    size_t nl = 0;
    char numbuf[64];
    if (*p == '-') numbuf[nl++] = *p++;
    while ((*p >= '0' && *p <= '9') || *p == '.') { if (nl + 1 < sizeof numbuf) numbuf[nl++] = *p; p++; }
    numbuf[nl] = 0;
    CalcP cp; cp.p = numbuf; cp.err = 0;
    CalcRat v = calc_expr(&cp);
    if (cp.err || *cp.p || !v.ok) return 0;
    /* 第一个单位：数后面（允许空格） */
    while (*p == ' ') p++;
    size_t l1 = 0;
    const UnitDef *u1 = units_at(p, &l1);
    if (!u1 || l1 == 0) return 0;
    p += l1;
    /* 第二个单位：往后找（跳过 等于/是/转/换算成/多少/几 等词） */
    const UnitDef *u2 = NULL;
    size_t l2 = 0;
    for (; *p; ) {
        const UnitDef *u = units_at(p, &l2);
        if (u && l2) { u2 = u; break; }
        p++;
    }
    if (!u2) return 0;
    CalcRat r;
    if (!units_convert(v, u1, u2, &r)) {
        snprintf(out, cap, "%s 和 %s 不是同一种量（%s vs %s），不换算。",
                 u1->unit, u2->unit, u1->dim, u2->dim);
        return 1;
    }
    char line[240], a[64], b[64];
    if (r.d == 1) snprintf(b, sizeof b, "%lld", (long long)r.n);
    else snprintf(b, sizeof b, "%lld/%lld ≈ %.6f", (long long)r.n, (long long)r.d, (double)r.n / (double)r.d);
    if (v.d == 1) snprintf(a, sizeof a, "%lld", (long long)v.n);
    else snprintf(a, sizeof a, "%lld/%lld", (long long)v.n, (long long)v.d);
    snprintf(line, sizeof line, "%s %s = %s %s", a, u1->unit, b, u2->unit);
    snprintf(out, cap, "%s", line);
    return 1;
}


/* ---------------- 带量纲的比较 / 相加（§6.801） ----------------
 * 「3 米 和 200 厘米 哪个长」「3 米 + 20 厘米 等于 多少」这类问题需要**两个量**。
 * 规矩和单量换算一样：量纲不同就拒绝（不猜），能算就给精确有理数。 */
typedef struct { CalcRat v; const UnitDef *u; } UnitQty;

/* 从文本里扫出所有 (数值, 单位) 对；返回个数（最多 max 个） */
static inline int units_scan(const char *in, UnitQty *q, int max) {
    if (!in || !q || max <= 0) return 0;
    int n = 0;
    const char *p = in;
    while (*p && n < max) {
        /* 定位数字 */
        while (*p && ((*p < '0' || *p > '9') && *p != '-')) p++;
        if (!*p) break;
        char buf[64]; size_t k = 0;
        if (*p == '-') { buf[k++] = *p++; }
        while ((*p >= '0' && *p <= '9') || *p == '.') { if (k + 1 < sizeof buf) buf[k++] = *p; p++; }
        buf[k] = 0;
        CalcP c; c.p = buf; c.err = 0;
        CalcRat v = calc_expr(&c);
        if (c.err || *c.p || !v.ok) continue;
        while (*p == ' ') p++;
        size_t l = 0;
        const UnitDef *u = units_at(p, &l);
        if (!u || l == 0) continue;              /* 数字后面没单位 ⇒ 不是量 */
        q[n].v = v; q[n].u = u; ++n;
        p += l;
    }
    return n;
}

/* 比较：`3 米 和 200 厘米 哪个长` / `3 米 比 200 厘米 长 吗` */
static inline int units_compare(const char *in, char *out, size_t cap) {
    int cmp_words = (strstr(in, "哪个") || strstr(in, "谁") || strstr(in, "比") ||
                     strstr(in, "大于") || strstr(in, "小于") || strstr(in, "更长") ||
                     strstr(in, "更高") || strstr(in, "更重") || strstr(in, "更久"));
    if (!cmp_words) return 0;
    UnitQty q[2];
    int n = units_scan(in, q, 2);
    if (n != 2) return 0;
    if (strcmp(q[0].u->dim, q[1].u->dim) != 0) {
        snprintf(out, cap, "%s 和 %s 不是同一种量（%s vs %s），不比。",
                 q[0].u->unit, q[1].u->unit, q[0].u->dim, q[1].u->dim);
        return 1;
    }
    CalcRat a2, b2;
    if (!units_convert(q[0].v, q[0].u, q[1].u, &a2)) return 0;   /* 都换到第二个的单位比较 */
    b2 = q[1].v;
    int sign = 0;                                              /* 1: 第一个大, -1: 第二个大, 0: 相等 */
    {
        CalcRat d = calc_sub(a2, b2);
        if (!d.ok) return 0;
        if (d.n > 0) sign = 1; else if (d.n < 0) sign = -1;
    }
    char av[64], bv[64];
    if (a2.d == 1) snprintf(av, sizeof av, "%lld", (long long)a2.n);
    else snprintf(av, sizeof av, "%lld/%lld", (long long)a2.n, (long long)a2.d);
    if (b2.d == 1) snprintf(bv, sizeof bv, "%lld", (long long)b2.n);
    else snprintf(bv, sizeof bv, "%lld/%lld", (long long)b2.n, (long long)b2.d);
    if (sign == 0)
        snprintf(out, cap, "一样（%s == %s）", av, bv);
    else if (sign > 0)
        snprintf(out, cap, "第一个大：%s > %s（同一个单位）", av, bv);
    else
        snprintf(out, cap, "第二个大：%s < %s（同一个单位）", av, bv);
    return 1;
}

/* 相加：`3 米 + 20 厘米 等于 多少` / `3 米 加 20 厘米`；结果用第一个量纲单位 */
static inline int units_sum(const char *in, char *out, size_t cap) {
    int plus_words = (strstr(in, "加") || strstr(in, "+") || strstr(in, "加上") ||
                      strstr(in, "一共") || strstr(in, "总共") || strstr(in, "总共"));
    if (!plus_words) return 0;
    UnitQty q[2];
    int n = units_scan(in, q, 2);
    if (n != 2) return 0;
    if (strcmp(q[0].u->dim, q[1].u->dim) != 0) {
        snprintf(out, cap, "%s 和 %s 不是同一种量（%s vs %s），不加。",
                 q[0].u->unit, q[1].u->unit, q[0].u->dim, q[1].u->dim);
        return 1;
    }
    CalcRat b2;
    if (!units_convert(q[1].v, q[1].u, q[0].u, &b2)) return 0;
    CalcRat sum = calc_add(q[0].v, b2);
    if (!sum.ok) return 0;
    char a[64], b[64], r[64];
    if (q[0].v.d == 1) snprintf(a, sizeof a, "%lld", (long long)q[0].v.n);
    else snprintf(a, sizeof a, "%lld/%lld", (long long)q[0].v.n, (long long)q[0].v.d);
    if (q[1].v.d == 1) snprintf(b, sizeof b, "%lld", (long long)q[1].v.n);
    else snprintf(b, sizeof b, "%lld/%lld", (long long)q[1].v.n, (long long)q[1].v.d);
    if (sum.d == 1) snprintf(r, sizeof r, "%lld", (long long)sum.n);
    else snprintf(r, sizeof r, "%lld/%lld ≈ %.6f", (long long)sum.n, (long long)sum.d,
                  (double)sum.n / (double)sum.d);
    snprintf(out, cap, "%s %s + %s %s = %s %s", a, q[0].u->unit, b, q[1].u->unit, r, q[0].u->unit);
    return 1;
}


/* ---------------- 日常数学：折扣 / 平均 / 比…多几 %（§6.802） ----------------
 * 三条都站在同一个原则上：能精确就精确（有理数），认不出就返回 0 让后面的路径接手。 */

/* 折扣：`打八折` = 80%（8 折）、`打八五折` = 85%（8.5 折）、`100 元 打八折` = 80 元、`9 折` = 90%
 * §6.804 连续折扣：`先打八折 再打九折` ⇒ 两个折相乘（0.8×0.9 = 0.72） */
static inline int discount_answer(const char *in, char *out, size_t cap) {
    if (!in || !out) return 0;
    const char *z = strstr(in, "折");
    if (!z) return 0;
    const char *p = z;
    while (p > in && p[-1] == ' ') p--;
    /* 1) 阿拉伯数字（允许小数）：8 / 8.5 / 85（≥10 当百分数） */
    CalcRat zhe = calc_bad();                     /* 存"折"的数值：8 折 = 8，八五折 = 8.5 */
    int from_ascii = 0;
    {
        const char *ae = p;
        while (ae > in && ((ae[-1] >= '0' && ae[-1] <= '9') || ae[-1] == '.')) ae--;
        if (ae < p) {
            char buf[32]; size_t k = 0;
            for (const char *q = ae; q < p && k + 1 < sizeof buf; ++q) buf[k++] = *q;
            buf[k] = 0;
            CalcP c; c.p = buf; c.err = 0;
            CalcRat v = calc_expr(&c);
            if (!c.err && !*c.p && v.ok) { zhe = v; from_ascii = 1; }
        }
    }
    /* 2) 中文数字：紧贴"折"的一个或两个汉字（八 / 八五 / 十） */
    if (!zhe.ok) {
        int d1 = -1, n1 = 0;
        /* 找"折"前面最后一个字符的起点 */
        const char *q = p;
        while (q > in && (*q & 0xC0) == 0x80) q--;          /* 跳过 UTF-8 续字节 */
        if (q > in) {
            const char *c1 = q - 1;
            while (c1 > in && (unsigned char)*c1 >= 0x80 && (unsigned char)*c1 < 0xC0) c1--;
            int got = 0, nn = calc_cn_num(c1, &got);
            if (nn > 0 && c1 + nn == p) { d1 = got; n1 = nn; }
        }
        if (d1 >= 0) {
            /* 再看前面还有没有紧邻的一个汉字（八五 = 8.5） */
            const char *c2end = p - n1;
            const char *c2 = c2end;
            while (c2 > in && (*c2 & 0xC0) == 0x80) c2--;
            const char *c2s = (c2 > in) ? c2 - 1 : in;
            while (c2s > in && (unsigned char)*c2s >= 0x80 && (unsigned char)*c2s < 0xC0) c2s--;
            int got2 = 0, nn2 = (c2end > c2s) ? calc_cn_num(c2s, &got2) : 0;
            /* 顺序是"十位在前"：八五折 = 8.5 折（第一版写反了，实测出 5.8） */
            if (nn2 > 0 && c2s + nn2 == c2end && got2 >= 1 && got2 <= 9)
                zhe = calc_add(calc_mk(got2, 1), calc_div(calc_mk(d1, 1), calc_mk(10, 1)));
            else
                zhe = calc_mk(d1, 1);
        }
    }
    if (!zhe.ok) return 0;
    /* 只有**阿拉伯数字**写的 ≥10 才是百分数（`85 折` = 85%）；中文一律 /10（八五折 = 8.5 折 = 85%） */
    CalcRat frac = (from_ascii && zhe.n >= 10) ? calc_div(zhe, calc_mk(100, 1))
                                               : calc_div(zhe, calc_mk(10, 1));
    if (!frac.ok) return 0;
    /* §6.804 句子里还有别的"折" ⇒ 逐个乘起来（`先打八折 再打九折`） */
    {
        const char *z2 = strstr(z, "折");
        while (z2) {
            const char *p2 = z2;
            while (p2 > z && p2[-1] == ' ') p2--;
            /* 取紧贴 z2 的中文/阿拉伯数字 */
            const char *q2 = p2;
            while (q2 > z && (*q2 & 0xC0) == 0x80) q2--;
            const char *c2 = (q2 > z) ? q2 - 1 : z;
            while (c2 > z && (unsigned char)*c2 >= 0x80 && (unsigned char)*c2 < 0xC0) c2--;
            int got = 0, nn = calc_cn_num(c2, &got);
            if (nn > 0 && c2 + nn == p2 && got >= 1 && got <= 9) {
                CalcRat extra = calc_div(calc_mk(got, 1), calc_mk(10, 1));
                CalcRat prod = calc_mul(frac, extra);
                if (prod.ok) frac = prod;
            }
            z2 = strstr(z2 + strlen("折"), "折");
        }
    }

    /* 原价：`打` 之前的第一个数 */
    int has_price = 0;
    CalcRat price = calc_bad();
    const char *da = strstr(in, "打");
    const char *limit = (da && da < z) ? da : z;
    {
        const char *r = in;
        while (r < limit) {
            while (r < limit && (*r < '0' || *r > '9')) r++;
            if (r >= limit) break;
            const char *e = r;
            while (e < limit && ((*e >= '0' && *e <= '9') || *e == '.')) e++;
            char buf[32]; size_t k = 0;
            for (const char *q = r; q < e && k + 1 < sizeof buf; ++q) buf[k++] = *q;
            buf[k] = 0;
            CalcP c; c.p = buf; c.err = 0;
            CalcRat v = calc_expr(&c);
            if (!c.err && !*c.p && v.ok) { price = v; has_price = 1; }
            break;
        }
    }
    char line[240], zb[64];
    if (zhe.d == 1) snprintf(zb, sizeof zb, "%lld", (long long)zhe.n);
    else snprintf(zb, sizeof zb, "%.2g", (double)zhe.n / (double)zhe.d);
    if (has_price) {
        CalcRat r = calc_mul(price, frac);
        if (!r.ok) return 0;
        char a[64], b[64];
        if (price.d == 1) snprintf(a, sizeof a, "%lld", (long long)price.n);
        else snprintf(a, sizeof a, "%lld/%lld", (long long)price.n, (long long)price.d);
        if (r.d == 1) snprintf(b, sizeof b, "%lld", (long long)r.n);
        else snprintf(b, sizeof b, "%lld/%lld ≈ %.2f", (long long)r.n, (long long)r.d,
                      (double)r.n / (double)r.d);
        snprintf(line, sizeof line, "%s 打 %s 折 = %s", a, zb, b);
    } else {
        snprintf(line, sizeof line, "打 %s 折（原价的 %lld%%）", zb,
                 (long long)(frac.n * 100 / frac.d));
    }
    snprintf(out, cap, "%s", line);
    return 1;
}

/* 平均：`平均 3 4 5` / `3 和 4 和 5 的平均` / `3、4、5 平均值` */
static inline int average_answer(const char *in, char *out, size_t cap) {
    if (!in || !out) return 0;
    if (!strstr(in, "平均")) return 0;
    CalcRat sum = calc_mk(0, 1);
    int n = 0;
    const char *p = in;
    while (*p) {
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        const char *e = p;
        while ((*e >= '0' && *e <= '9') || *e == '.') e++;
        char buf[32]; size_t k = 0;
        for (const char *q = p; q < e && k + 1 < sizeof buf; ++k, ++q) buf[k] = *q;
        buf[k] = 0;
        CalcP c; c.p = buf; c.err = 0;
        CalcRat v = calc_expr(&c);
        if (!c.err && !*c.p && v.ok) { sum = calc_add(sum, v); ++n; }
        p = e;
    }
    if (n < 2) return 0;
    CalcRat avg = calc_div(sum, calc_mk(n, 1));
    if (!avg.ok) return 0;
    char line[240], s1[64], s2[64];
    if (sum.d == 1) snprintf(s1, sizeof s1, "%lld", (long long)sum.n);
    else snprintf(s1, sizeof s1, "%lld/%lld", (long long)sum.n, (long long)sum.d);
    if (avg.d == 1) snprintf(s2, sizeof s2, "%lld", (long long)avg.n);
    else snprintf(s2, sizeof s2, "%lld/%lld ≈ %.6f", (long long)avg.n, (long long)avg.d,
                  (double)avg.n / (double)avg.d);
    snprintf(line, sizeof line, "%d 个数的平均 = %s / %d = %s", n, s1, n, s2);
    snprintf(out, cap, "%s", line);
    return 1;
}

/* 比…多/少 几 %：`100 比 80 多 多少%` / `80 比 100 少 百分之几` */
static inline int percent_diff_answer(const char *in, char *out, size_t cap) {
    if (!in || !out) return 0;
    if (!strstr(in, "比")) return 0;
    if (!(strstr(in, "%") || strstr(in, "％") || strstr(in, "百分之") || strstr(in, "几%"))) return 0;
    double a[2]; int n = 0;
    const char *p = in;
    while (*p && n < 2) {
        while (*p && ((*p < '0' || *p > '9') && *p != '-')) p++;
        if (!*p) break;
        const char *e = p;
        if (*e == '-') e++;
        while ((*e >= '0' && *e <= '9') || *e == '.') e++;
        char buf[32]; size_t k = 0;
        for (const char *q = p; q < e && k + 1 < sizeof buf; ++k, ++q) buf[k] = *q;
        buf[k] = 0;
        a[n++] = atof(buf);
        p = e;
    }
    if (n < 2 || a[1] == 0) return 0;
    CalcRat A = calc_mk((int64_t)(a[0] * 1000000.0), 1000000);
    CalcRat B = calc_mk((int64_t)(a[1] * 1000000.0), 1000000);
    /* `多少` 里的"少"不是"减少"（踩过：`100 比 80 多 多少%` 被答成"少 -25%"） */
    const char *shao = strstr(in, "少");
    while (shao && shao - in >= (long)strlen("多") && !strncmp(shao - strlen("多"), "多", strlen("多")))
        shao = strstr(shao + strlen("少"), "少");
    int less = (shao != NULL) || strstr(in, "低") || strstr(in, "减");
    CalcRat d = less ? calc_sub(B, A) : calc_sub(A, B);
    if (!calc_add(d, calc_mk(0, 1)).ok) return 0;
    CalcRat r = calc_div(calc_mul(d, calc_mk(100, 1)), B);
    if (!r.ok) return 0;
    char line[240], rb[64];
    if (r.d == 1) snprintf(rb, sizeof rb, "%lld", (long long)r.n);
    else snprintf(rb, sizeof rb, "%lld/%lld ≈ %.4f", (long long)r.n, (long long)r.d,
                  (double)r.n / (double)r.d);
    snprintf(line, sizeof line, "%g 比 %g %s %s%%", a[0], a[1], less ? "少" : "多", rb);
    snprintf(out, cap, "%s", line);
    return 1;
}


/* ---------------- 速率与单价（§6.803）：量纲的乘/除 ----------------
 * 单价 × 数量 = 总价、速度 × 时间 = 路程、路程 ÷ 时间 = 速度。
 * 仍然只用**通用规则**：量纲要配得上（时间配时间、长度配长度），配不上就拒绝。 */

static inline CalcRat u_num(const char *in, const char **end) {
    const char *p = in;
    while (*p && (*p < '0' || *p > '9') && *p != '-') p++;
    if (!*p) { if (end) *end = p; return calc_bad(); }
    const char *e = p;
    if (*e == '-') e++;
    while ((*e >= '0' && *e <= '9') || *e == '.') e++;
    char buf[32]; size_t k = 0;
    for (const char *q = p; q < e && k + 1 < sizeof buf; ++k, ++q) buf[k] = *q;
    buf[k] = 0;
    CalcP c; c.p = buf; c.err = 0;
    CalcRat v = calc_expr(&c);
    if (end) *end = e;
    return (c.err || *c.p) ? calc_bad() : v;
}

static inline void u_fmt(CalcRat v, char *out, size_t cap) {
    if (v.d == 1) snprintf(out, cap, "%lld", (long long)v.n);
    else snprintf(out, cap, "%lld/%lld ≈ %.4f", (long long)v.n, (long long)v.d, (double)v.n / (double)v.d);
}

/* 速率：`每秒 3 米` / `3 米每秒` / `每小时 60 千米` → (值, 长度单位/时间单位) */
static inline int rate_parse(const char *in, CalcRat *v, const UnitDef **len, const UnitDef **tim) {
    if (!in) return 0;
    const char *per = strstr(in, "每");
    if (!per) return 0;
    const char *p = per + strlen("每");
    /* `每 秒 3 米`：每 后面先时间单位，再数值+长度单位 */
    {
        size_t l = 0;
        const UnitDef *tu = units_at(p, &l);
        if (tu && l && !strcmp(tu->dim, "时间")) {
            CalcRat val = u_num(p + l, NULL);
            const char *q = p + l;
            while (*q && (*q < '0' || *q > '9')) q++;
            while (*q && ((*q >= '0' && *q <= '9') || *q == '.')) q++;
            while (*q == ' ') q++;
            size_t l2 = 0;
            const UnitDef *lu = units_at(q, &l2);
            if (val.ok && lu && l2 && !strcmp(lu->dim, "长度")) {
                *v = val; *len = lu; *tim = tu; return 1;
            }
        }
    }
    /* `3 米 每 秒` / `3 米每秒`：数值+长度单位，然后 每 + 时间单位 */
    {
        CalcRat val = u_num(in, NULL);
        if (!val.ok) return 0;
        const char *q = in;
        while (*q && (*q < '0' || *q > '9')) q++;
        while (*q && ((*q >= '0' && *q <= '9') || *q == '.')) q++;
        while (*q == ' ') q++;
        size_t l2 = 0;
        const UnitDef *lu = units_at(q, &l2);
        if (!lu || !l2 || strcmp(lu->dim, "长度")) return 0;
        const char *r = q + l2;
        while (*r == ' ') r++;
        if (strncmp(r, "每", strlen("每"))) return 0;
        r += strlen("每");
        size_t l3 = 0;
        const UnitDef *tu = units_at(r, &l3);
        if (!tu || !l3 || strcmp(tu->dim, "时间")) return 0;
        *v = val; *len = lu; *tim = tu; return 1;
    }
}

/* 速度类问题：
 *   ① 有速率 + 有匹配的时间量 ⇒ 路程 = 速度×时间
 *   ② 有长度量 + 有时间量（"用了/花了/需要"）⇒ 速度 = 路程÷时间 */
static inline int speed_answer(const char *in, char *out, size_t cap) {
    if (!in || !out) return 0;
    CalcRat v = calc_bad(); const UnitDef *lu = NULL, *tu = NULL;
    int has_rate = rate_parse(in, &v, &lu, &tu);
    UnitQty q[3];
    int n = units_scan(in, q, 3);
    if (has_rate) {
        /* 找时间量（和速率分母同量纲），不能是速率自己的那个数 */
        for (int i = 0; i < n; ++i) {
            if (strcmp(q[i].u->dim, "时间")) continue;
            CalcRat t2;
            if (!units_convert(q[i].v, q[i].u, tu, &t2)) continue;
            CalcRat d = calc_mul(v, t2);
            if (!d.ok) return 0;
            char a[64], b[64], c[64];
            u_fmt(v, a, sizeof a); u_fmt(t2, b, sizeof b); u_fmt(d, c, sizeof c);
            snprintf(out, cap, "%s %s/%s × %s %s = %s %s", a, lu->unit, tu->unit, b, tu->unit, c, lu->unit);
            return 1;
        }
        return 0;
    }
    /* 路程 ÷ 时间 */
    if (strstr(in, "用") || strstr(in, "花") || strstr(in, "需") || strstr(in, "走完") || strstr(in, "跑完")) {
        int li = -1, ti = -1;
        for (int i = 0; i < n; ++i) {
            if (!strcmp(q[i].u->dim, "长度") && li < 0) li = i;
            if (!strcmp(q[i].u->dim, "时间") && ti < 0) ti = i;
        }
        if (li >= 0 && ti >= 0) {
            CalcRat d = q[li].v, t2 = q[ti].v;
            CalcRat sp = calc_div(d, t2);
            if (!sp.ok) return 0;
            char a[64], b[64], c[64];
            u_fmt(d, a, sizeof a); u_fmt(t2, b, sizeof b); u_fmt(sp, c, sizeof c);
            snprintf(out, cap, "%s %s ÷ %s %s = %s %s/%s", a, q[li].u->unit, b, q[ti].u->unit,
                     c, q[li].u->unit, q[ti].u->unit);
            return 1;
        }
    }
    return 0;
}

/* 单价 × 数量：`3 元 一 个 买 5 个` / `每个 3 元 买 5 个` / `单价 3 元 数量 5 个` */
static inline int price_answer(const char *in, char *out, size_t cap) {
    if (!in || !out) return 0;
    if (!(strstr(in, "元") || strstr(in, "块") || strstr(in, "角"))) return 0;
    /* 单价：`3 元` 前面那个数 */
    const char *yuan = strstr(in, "元");
    if (!yuan) return 0;
    CalcRat unit_price = u_num(in, NULL);
    if (!unit_price.ok) return 0;
    /* 数量：`元` 之后出现的第一个"数 + 个/件/本/支/瓶" */
    const char *p = yuan + strlen("元");
    CalcRat count = calc_bad();
    while (*p) {
        while (*p && (*p < '0' || *p > '9')) p++;
        if (!*p) break;
        const char *e = p;
        while ((*e >= '0' && *e <= '9') || *e == '.') e++;
        while (*e == ' ') e++;
        size_t l = 0;
        const UnitDef *u = units_at(e, &l);
        int is_cnt = (u && !strcmp(u->dim, "数量"));
        if (!is_cnt) {
            static const char *const CNT[] = { "个", "件", "本", "支", "瓶", "张", "只", "包", "箱", "斤" };
            for (size_t k = 0; k < sizeof CNT / sizeof CNT[0]; ++k)
                if (!strncmp(e, CNT[k], strlen(CNT[k]))) { is_cnt = 1; break; }
        }
        if (is_cnt) {
            char buf[32]; size_t k = 0;
            for (const char *q = p; q < e && k + 1 < sizeof buf; ++k, ++q) buf[k] = *q;
            /* e 可能已经跳过空格；用原始数字串解析 */
            char *sp = buf;
            while (*sp == ' ') ++sp;
            char buf2[32]; size_t k2 = 0;
            for (const char *q = sp; *q && *q != ' ' && k2 + 1 < sizeof buf2; ++k2, ++q) buf2[k2] = *q;
            buf2[k2] = 0;
            CalcP c; c.p = buf2; c.err = 0;
            CalcRat v = calc_expr(&c);
            if (!c.err && !*c.p && v.ok) { count = v; break; }
        }
        p = e;
    }
    if (!count.ok || count.n <= 0) return 0;
    CalcRat total = calc_mul(unit_price, count);
    if (!total.ok) return 0;
    char a[64], b[64], c[64];
    u_fmt(unit_price, a, sizeof a); u_fmt(count, b, sizeof b); u_fmt(total, c, sizeof c);
    snprintf(out, cap, "%s 元/个 × %s 个 = %s 元", a, b, c);
    return 1;
}


/* 百分比链：`100 涨 10% 再 降 10%` = 99、`200 降 20% 再 涨 50%` = 240（§6.814）
 * 规则：起点 × Π(1 ± p/100)；认不出（没有 %、没有涨/降词）就返回 0。 */
static inline int pct_chain_answer(const char *in, char *out, size_t cap) {
    if (!in || !out) return 0;
    if (!(strstr(in, "%") || strstr(in, "％") || strstr(in, "百分之"))) return 0;
    int has_dir = strstr(in, "涨") || strstr(in, "升") || strstr(in, "增") ||
                  strstr(in, "降") || strstr(in, "跌") || strstr(in, "减");
    if (!has_dir) return 0;
    /* 起点：第一个数 */
    const char *p = in;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return 0;
    CalcRat v = u_num(in, NULL);
    if (!v.ok) return 0;
    /* 逐段找"方向 + 百分数" */
    int steps = 0;
    const char *q = p;
    while (*q) {
        const char *dirp = NULL; int up = 0;
        for (const char *r = q; *r; ++r) {
            if (strstr(r, "涨") == r) { dirp = r; up = 1; break; }
            if (strstr(r, "升") == r) { dirp = r; up = 1; break; }
            if (strstr(r, "增") == r) { dirp = r; up = 1; break; }
            if (strstr(r, "降") == r) { dirp = r; up = 0; break; }
            if (strstr(r, "跌") == r) { dirp = r; up = 0; break; }
            if (strstr(r, "减") == r) { dirp = r; up = 0; break; }
        }
        if (!dirp) break;
        /* 方向词后面的第一个数 = 百分比 */
        const char *r = dirp;
        while (*r && (*r < '0' || *r > '9')) r++;
        if (!*r) break;
        CalcRat pv = u_num(r, NULL);
        if (!pv.ok) break;
        CalcRat one = calc_mk(1, 1);
        CalcRat pct = calc_div(pv, calc_mk(100, 1));
        CalcRat f = up ? calc_add(one, pct) : calc_sub(one, pct);
        if (!f.ok) return 0;
        CalcRat nv = calc_mul(v, f);
        if (!nv.ok) return 0;
        v = nv;
        ++steps;
        /* 继续从百分数之后找 */
        while (*r && (*r >= '0' && *r <= '9')) r++;
        if (*r == '.') { r++; while (*r >= '0' && *r <= '9') r++; }
        q = r;
    }
    if (steps < 2) return 0;                 /* 单步的由百分比/折扣那几条负责 */
    char a[64], b[64];
    u_fmt(calc_mk(0, 1), a, sizeof a);       /* 占位，避免未用警告 */
    (void)a;
    u_fmt(v, b, sizeof b);
    snprintf(out, cap, "连乘 %d 次 = %s（每步按当前值算，不是加总）", steps, b);
    return 1;
}

/* 百分比：`200 的 15%` / `15% 的 200` / `200 的 15％` */
static inline int percent_answer(const char *input, char *out, size_t cap) {
    if (!input || !out) return 0;
    /* 找第一个数 A 和百分数 B% */
    const char *p = input;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return 0;
    char buf[64]; size_t n = 0;
    while ((*p >= '0' && *p <= '9') || *p == '.') { if (n + 1 < sizeof buf) buf[n++] = *p; p++; }
    buf[n] = 0;
    CalcP c1; c1.p = buf; c1.err = 0;
    CalcRat a = calc_expr(&c1);
    if (c1.err || *c1.p || !a.ok) return 0;
    /* `15% 的 200`：第一个数就带 %，交换角色 */
    int first_is_pct = (*p == '%') || !strncmp(p, "％", strlen("％"));
    /* 找第二个数（%） */
    const char *q = p;
    while (*q && (*q < '0' || *q > '9')) q++;
    if (!*q) return 0;
    char buf2[64]; size_t n2 = 0;
    while ((*q >= '0' && *q <= '9') || *q == '.') { if (n2 + 1 < sizeof buf2) buf2[n2++] = *q; q++; }
    buf2[n2] = 0;
    int is_pct = (*q == '%') || !strncmp(q, "％", strlen("％")) || !strncmp(q, "百分", strlen("百分"));
    if (!is_pct && first_is_pct) { /* 第一个带 %、第二个不带 ⇒ 也接受（15% 的 200） */ }
    else if (!is_pct) return 0;
    CalcP c2; c2.p = buf2; c2.err = 0;
    CalcRat b = calc_expr(&c2);
    if (c2.err || *c2.p || !b.ok) return 0;
    /* `15% 的 200`：百分数在前 ⇒ 交换角色（结果一样是 A*B/100，但提示语要按原句写） */
    const char *first = buf, *second = buf2;
    CalcRat r = calc_div(calc_mul(a, b), calc_mk(100, 1));
    (void)first; (void)second;
    if (!r.ok) return 0;
    char line[240], bb[64];
    if (r.d == 1) snprintf(bb, sizeof bb, "%lld", (long long)r.n);
    else snprintf(bb, sizeof bb, "%lld/%lld ≈ %.6f", (long long)r.n, (long long)r.d, (double)r.n / (double)r.d);
    if (first_is_pct) snprintf(line, sizeof line, "%s%% 的 %s = %s", buf, buf2, bb);
    else              snprintf(line, sizeof line, "%s 的 %s%% = %s", buf, buf2, bb);
    snprintf(out, cap, "%s", line);
    return 1;
}

/* 图覆盖：`::unit sync` 写进去的值优先（`倍数` / `偏移` 用 "n/d" 形式存） */
static inline void units_set_scale(const char *unit, CalcRat scale, CalcRat offset) {
    units_builtin();
    for (int i = 0; i < g_units_n; ++i)
        if (unit && !strcmp(g_units[i].unit, unit)) {
            if (scale.ok) g_units[i].scale = scale;
            if (offset.ok) g_units[i].offset = offset;
            return;
        }
}
static inline void units_reset(void) { g_units_n = -1; }

#endif /* UNITS_H */
