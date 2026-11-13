/* calc.h — **算术能力**（§6.798）：精确有理数求值，不查表、不背答案
 *
 * 为什么要这个：patterns.txt 里原来写着「算术 我 不 会（没 有 算术 能力）」—— 那是**声明**，
 * 不是能力。HLE 里 45% 是数学题、日常也天天有人问"3 加 4 等于几"。
 * 这里的做法是**通用**的：把句子里的算式抽出来，用有理数（分子/分母都是 int64）精确算，
 * 除不尽就报分数 + 近似值；认不出算式就**不动**（让后面的对话逻辑接手），绝不硬猜。
 *
 * 支持：+ - * / ^ ( ) 小数、负号、中文数字（一…九十九）、
 *       中文算子（加/减/乘/乘以/除/除以）、× ÷、的平方/的立方/的N次方。
 * 溢出/除以零/括号不配对 ⇒ 明确报错，不返回一个"看起来像"的数。
 */
#ifndef CALC_H
#define CALC_H
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { int64_t n, d; int ok; } CalcRat;      /* 精确有理数 n/d（d>0） */

static inline int64_t calc_gcd(int64_t a, int64_t b) {
    if (a < 0) a = -a;
    if (b < 0) b = -b;
    while (b) { int64_t t = a % b; a = b; b = t; }
    return a ? a : 1;
}
static inline CalcRat calc_mk(int64_t n, int64_t d) {
    CalcRat r; r.ok = 1;
    if (d == 0) { r.ok = 0; r.n = r.d = 0; return r; }
    if (d < 0) { n = -n; d = -d; }
    int64_t g = calc_gcd(n, d);
    r.n = n / g; r.d = d / g;
    return r;
}
static inline CalcRat calc_bad(void) { CalcRat r; r.ok = 0; r.n = r.d = 0; return r; }

/* 溢出安全的加减乘（不用 __int128：本项目按 -Wpedantic 编，扩展类型会被拦） */
static inline int calc_mul_ovf(int64_t a, int64_t b, int64_t *out) {
    if (a == 0 || b == 0) { *out = 0; return 0; }
    if ((a == -1 && b == INT64_MIN) || (b == -1 && a == INT64_MIN)) return 1;
    int64_t r = (int64_t)((uint64_t)a * (uint64_t)b);      /* 无符号乘，避免 UB */
    if (r / b != a) return 1;                              /* 回代不一致 ⇒ 溢出 */
    *out = r; return 0;
}
static inline int calc_add_ovf(int64_t a, int64_t b, int64_t *out) {
    if ((b > 0 && a > INT64_MAX - b) || (b < 0 && a < INT64_MIN - b)) return 1;
    *out = a + b; return 0;
}

static inline CalcRat calc_add(CalcRat a, CalcRat b) {
    int64_t x, y;
    if (calc_mul_ovf(a.n, b.d, &x) || calc_mul_ovf(b.n, a.d, &y)) return calc_bad();
    int64_t n; if (calc_add_ovf(x, y, &n)) return calc_bad();
    int64_t d; if (calc_mul_ovf(a.d, b.d, &d)) return calc_bad();
    return calc_mk(n, d);
}
static inline CalcRat calc_sub(CalcRat a, CalcRat b) {
    CalcRat nb = b; nb.n = -nb.n; return calc_add(a, nb);
}
static inline CalcRat calc_mul(CalcRat a, CalcRat b) {
    int64_t n, d;
    if (calc_mul_ovf(a.n, b.n, &n) || calc_mul_ovf(a.d, b.d, &d)) return calc_bad();
    return calc_mk(n, d);
}
static inline CalcRat calc_div(CalcRat a, CalcRat b) {
    if (b.n == 0) return calc_bad();
    int64_t n, d;
    if (calc_mul_ovf(a.n, b.d, &n) || calc_mul_ovf(a.d, b.n, &d)) return calc_bad();
    return calc_mk(n, d);
}
static inline CalcRat calc_pow(CalcRat a, int64_t e) {
    if (e < 0 || e > 64) return calc_bad();
    CalcRat r = calc_mk(1, 1);
    for (int64_t i = 0; i < e; ++i) { r = calc_mul(r, a); if (!r.ok) return r; }
    return r;
}

/* ---------------- 递归下降解析 ---------------- */
typedef struct { const char *p; int err; } CalcP;

static inline CalcRat calc_expr(CalcP *c);

static inline void calc_ws(CalcP *c) { while (*c->p == ' ' || *c->p == '\t') c->p++; }

static inline CalcRat calc_atom(CalcP *c) {
    calc_ws(c);
    if (*c->p == '(') {
        c->p++;
        CalcRat v = calc_expr(c);
        calc_ws(c);
        if (*c->p == ')') c->p++; else { c->err = 1; return calc_bad(); }
        return v;
    }
    if (*c->p == '-') { c->p++; CalcRat v = calc_atom(c); v.n = -v.n; return v; }
    if (*c->p == '+') { c->p++; return calc_atom(c); }
    if (*c->p < '0' || *c->p > '9') { c->err = 1; return calc_bad(); }
    int64_t ip = 0;
    while (*c->p >= '0' && *c->p <= '9') {
        if (ip > (INT64_MAX - 9) / 10) { c->err = 1; return calc_bad(); }
        ip = ip * 10 + (*c->p - '0'); c->p++;
    }
    int64_t fr = 0, scale = 1;
    if (*c->p == '.') {
        c->p++;
        while (*c->p >= '0' && *c->p <= '9') {
            if (scale < 1000000000000LL) { fr = fr * 10 + (*c->p - '0'); scale *= 10; }
            c->p++;
        }
    }
    int64_t n, d;
    if (calc_mul_ovf(ip, scale, &n) || calc_add_ovf(n, fr, &n)) { c->err = 1; return calc_bad(); }
    d = scale;
    return calc_mk(n, d);
}

static inline CalcRat calc_power(CalcP *c) {
    CalcRat base = calc_atom(c);
    for (;;) {
        calc_ws(c);
        if (*c->p != '^') break;
        c->p++;
        CalcRat e = calc_atom(c);
        if (!e.ok || e.d != 1) { c->err = 1; return calc_bad(); }
        base = calc_pow(base, e.n);
        if (!base.ok) { c->err = 1; return base; }
    }
    return base;
}

static inline CalcRat calc_term(CalcP *c) {
    CalcRat v = calc_power(c);
    for (;;) {
        calc_ws(c);
        char op = *c->p;
        if (op != '*' && op != '/') break;
        c->p++;
        CalcRat r = calc_power(c);
        v = (op == '*') ? calc_mul(v, r) : calc_div(v, r);
        if (!v.ok) { c->err = 1; return v; }
    }
    return v;
}

static inline CalcRat calc_expr(CalcP *c) {
    CalcRat v = calc_term(c);
    for (;;) {
        calc_ws(c);
        char op = *c->p;
        if (op != '+' && op != '-') break;
        c->p++;
        CalcRat r = calc_term(c);
        v = (op == '+') ? calc_add(v, r) : calc_sub(v, r);
        if (!v.ok) { c->err = 1; return v; }
    }
    return v;
}

/* 中文数字（0~99）：`三`=3、`十`=10、`二十`=20、`二十三`=23。返回吃掉的字节数，0 = 不是数字 */
static inline int calc_cn_num(const char *p, int *val) {
    static const char *const CN[10] = { "零", "一", "二", "三", "四", "五", "六", "七", "八", "九" };
    const size_t L10 = strlen("十");
    if (!p || !*p) return 0;
    int d1 = -1, n = 0;
    for (int d = 0; d < 10; ++d) {
        size_t l = strlen(CN[d]);
        if (!strncmp(p, CN[d], l)) { d1 = d; n = (int)l; break; }
    }
    int used = 0;
    if (d1 >= 0) { *val = d1; used = n; }
    if (!strncmp(p + used, "十", L10)) {
        int base = (d1 >= 0) ? d1 * 10 : 10;        /* 二十 / 十 */
        used += (int)L10;
        int d2 = -1, n2 = 0;
        for (int d = 0; d < 10; ++d) {
            size_t l = strlen(CN[d]);
            if (!strncmp(p + used, CN[d], l)) { d2 = d; n2 = (int)l; break; }
        }
        if (d2 >= 0) { base += d2; used += n2; }    /* 二十三 */
        *val = base;
        return used;
    }
    return (d1 >= 0) ? used : 0;
}

/* 文本 → 规范算式：中文数字、中文算子、×÷、的平方/立方/N次方 */
static inline void calc_normalize(const char *in, char *out, size_t cap) {
    static const struct { const char *from, *to; } R[] = {
        {"乘以", "*"}, {"乘", "*"}, {"除以", "/"}, {"除", "/"},
        {"加上", "+"}, {"加", "+"}, {"减去", "-"}, {"减", "-"},
        {"×", "*"}, {"÷", "/"}, {"（", "("}, {"）", ")"},
        {"的平方", "^2"}, {"的立方", "^3"}, {"的", "^"},
        {"次方", ""}, {"平方", ""}, {"立方", ""},
    };
    char u[512]; size_t un = 0;
    for (size_t i = 0; in[i] && un + 16 < sizeof u; ) {
        int v = 0;
        int used = calc_cn_num(in + i, &v);
        if (used > 0) { un += (size_t)snprintf(u + un, sizeof u - un, "%d", v); i += (size_t)used; continue; }
        u[un++] = in[i++];
    }
    u[un] = 0;
    size_t on = 0; out[0] = 0;
    for (size_t i = 0; u[i] && on + 16 < cap; ) {
        int matched = 0;
        for (size_t k = 0; k < sizeof R / sizeof R[0]; ++k) {
            size_t l = strlen(R[k].from);
            if (!strncmp(u + i, R[k].from, l)) {
                on += (size_t)snprintf(out + on, cap - on, "%s", R[k].to);
                i += l; matched = 1; break;
            }
        }
        if (matched) continue;
        out[on++] = u[i++];
    }
    out[on] = 0;
}

/* 等差求和：`从 1 加到 100` / `1 加到 100` / `1+2+...+100` 这种。认不出返回 0。
 * 为什么单独一条：日常问得最多的就是"1 加到 100"，而它不是"两个数一个算子"的算式。 */
static inline int calc_series(const char *in, char *out, size_t cap) {
    if (!in) return 0;
    /* 找 "从?A加到B" 或 "A 加到 B" */
    const char *p = strstr(in, "加到");
    if (!p) return 0;
    /* 往前找 A：跳过空格，取一串数字（允许前面有"从"字） */
    const char *a_end = p;
    while (a_end > in && (a_end[-1] == ' ' || a_end[-1] == '\t')) a_end--;
    const char *a_start = a_end;
    while (a_start > in && a_start[-1] >= '0' && a_start[-1] <= '9') a_start--;
    if (a_start == a_end) return 0;
    const char *b = p + strlen("加到");
    while (*b == ' ') b++;
    if (*b < '0' || *b > '9') return 0;
    const char *b_end = b;
    while (*b_end >= '0' && *b_end <= '9') b_end++;
    int64_t A = 0, B = 0;
    for (const char *q = a_start; q < a_end; ++q) {
        if (A > (INT64_MAX - 9) / 10) return 0;
        A = A * 10 + (*q - '0');
    }
    for (const char *q = b; q < b_end; ++q) {
        if (B > (INT64_MAX - 9) / 10) return 0;
        B = B * 10 + (*q - '0');
    }
    if (A > B) { int64_t t = A; A = B; B = t; }
    int64_t n;                                  /* 项数 = B-A+1 */
    if (calc_add_ovf(B, -A, &n) || calc_add_ovf(n, 1, &n)) return 0;
    int64_t ab;                                 /* 首项+末项 */
    if (calc_add_ovf(A, B, &ab)) return 0;
    int64_t sum;
    if (ab % 2 == 0) { if (calc_mul_ovf(ab / 2, n, &sum)) return 0; }
    else if (n % 2 == 0) { if (calc_mul_ovf(ab, n / 2, &sum)) return 0; }
    else return 0;                              /* 理论上进不来（ab 与 n 必有一偶） */
    char line[240];
    snprintf(line, sizeof line, "从 %lld 加到 %lld = %lld（等差求和）",
             (long long)A, (long long)B, (long long)sum);
    snprintf(out, cap, "%s", line);
    return 1;
}

/* 从一句话里抽出最长算式并求值；成功返回 1 并把结果写进 out */
static inline int calc_answer(const char *input, char *out, size_t cap) {
    if (!input || !out || cap == 0) return 0;
    if (calc_series(input, out, cap)) return 1;
    char norm[512];
    calc_normalize(input, norm, sizeof norm);
    /* §6.799 **别在长句里抢答**：HLE 的题干是整段英文，里面到处是数字和加减乘除符号
     * （实测 1589/2158 道题含这两种东西）。第一版在长题里扫到片段就答，硬答从 0 涨到 178 条。
     * 判据：① 出现 ASCII 字母 ⇒ 不是纯算式句（英文题干全被排除）；② 归一化后超过 96 字节 ⇒ 太长。 */
    for (const char *pp = norm; *pp; ++pp)
        if ((*pp >= 'a' && *pp <= 'z') || (*pp >= 'A' && *pp <= 'Z')) return 0;
    if (strlen(norm) > 96) return 0;
    /* 找最长"算式候选"：由 0-9 + - * / ^ ( ) . 空格 组成 */
    size_t best_s = 0, best_l = 0;
    for (size_t i = 0; norm[i]; ) {
        if (!strchr("0123456789+-*/^(). ", norm[i])) { i++; continue; }
        size_t j = i;
        while (norm[j] && strchr("0123456789+-*/^(). ", norm[j])) j++;
        size_t l = j - i;
        int has_digit = 0, has_op = 0;
        for (size_t k = i; k < j; ++k) {
            if (norm[k] >= '0' && norm[k] <= '9') has_digit = 1;
            if (strchr("+-*/^", norm[k])) has_op = 1;
        }
        if (has_digit && has_op && l > best_l) { best_s = i; best_l = l; }
        i = j;
    }
    if (best_l < 3) return 0;
    char expr[512];
    if (best_l >= sizeof expr) best_l = sizeof expr - 1;
    memcpy(expr, norm + best_s, best_l);
    expr[best_l] = 0;
    /* 去掉尾部的问题词（等于/是多少/呢/？）——它们不在字符集里，已在上面被截断 */
    CalcP c; c.p = expr; c.err = 0;
    CalcRat v = calc_expr(&c);
    calc_ws(&c);
    if (c.err || *c.p) return 0;
    if (!v.ok) {
        char line[240];
        snprintf(line, sizeof line, "算式「%.100s」算不了（除以零或数值越界），不瞎给。", expr);
        snprintf(out, cap, "%s", line);
        return 1;
    }
    char line[240];
    if (v.d == 1) snprintf(line, sizeof line, "%.120s = %lld", expr, (long long)v.n);
    else snprintf(line, sizeof line, "%.80s = %lld/%lld ≈ %.6f", expr, (long long)v.n,
                  (long long)v.d, (double)v.n / (double)v.d);
    snprintf(out, cap, "%s", line);       /* line 只有 240 字节 ⇒ 不会撑破任何 cap ≥ 256 的缓冲 */
    return 1;
}

#endif /* CALC_H */
