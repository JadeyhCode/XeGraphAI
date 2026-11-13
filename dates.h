/* dates.h — **日期运算**（§6.804）：给定明确日期才算；没有时钟就如实说
 *
 * 支持：`2026-03-01 之后 30 天 是 几号`、`2026-03-01 之前 10 天`、
 *       `2026-03-01 和 2026-03-15 差 几 天`、`2026-03-01 是 星期 几`。
 * 不支持（明确拒绝，不猜）：月/年加减（"一个月后"涉及月末约定，各语言不一样）。
 * 全程用公历的**天数序**算法（Howard Hinnant 的 days_from_civil），不依赖系统时钟。
 */
#ifndef DATES_H
#define DATES_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "calc.h"

/* 公历 (y,m,d) → 距 1970-01-01 的天数 */
static inline int64_t date_days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}
static inline void date_civil_from_days(int64_t z, int *y, int *m, int *d) {
    z += 719468;
    int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    unsigned doe = (unsigned)(z - era * 146097);
    unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    int64_t yy = (int64_t)yoe + era * 400;
    unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    unsigned mp = (5 * doy + 2) / 153;
    unsigned dd = doy - (153 * mp + 2) / 5 + 1;
    unsigned mm = mp + (mp < 10 ? 3 : -9);
    *y = (int)(yy + (mm <= 2));
    *m = (int)mm;
    *d = (int)dd;
}
static inline int date_valid(int y, int m, int d) {
    if (y < 1 || y > 9999 || m < 1 || m > 12 || d < 1 || d > 31) return 0;
    static const int dm[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    int leap = (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0);
    int lim = dm[m - 1] + ((m == 2 && leap) ? 1 : 0);
    return d <= lim;
}

/* 从串里解析第一个日期：YYYY-MM-DD / YYYY/MM/DD / YYYY年M月D日 */
static inline int date_parse(const char *s, int *y, int *m, int *d, const char **end) {
    if (!s) return 0;
    const char *p = s;
    while (*p && (*p < '0' || *p > '9')) p++;
    if (!*p) return 0;
    char buf[32]; size_t k = 0;
    while (*p && k + 1 < sizeof buf) {
        char c = *p;
        if ((c >= '0' && c <= '9') || c == '-' || c == '/' || c == '.') { buf[k++] = c; p++; }
        else if (!strncmp(p, "年", strlen("年")) || !strncmp(p, "月", strlen("月"))) { buf[k++] = '-'; p += 3; }
        else if (!strncmp(p, "日", strlen("日")) || !strncmp(p, "号", strlen("号"))) { p += 3; break; }
        else break;
    }
    buf[k] = 0;
    if (end) *end = p;
    int yy = 0, mm = 0, dd = 0;
    if (sscanf(buf, "%d-%d-%d", &yy, &mm, &dd) != 3) return 0;
    if (!date_valid(yy, mm, dd)) return 0;
    *y = yy; *m = mm; *d = dd;
    return 1;
}

static inline int date_answer(const char *in, char *out, size_t cap) {
    if (!in || !out) return 0;
    int y1 = 0, m1 = 0, d1 = 0;
    const char *e1 = NULL;
    if (!date_parse(in, &y1, &m1, &d1, &e1)) return 0;
    /* ① 两个日期比差 */
    {
        int y2 = 0, m2 = 0, d2 = 0;
        const char *e2 = NULL;
        if (date_parse(e1, &y2, &m2, &d2, &e2)) {
            int64_t a = date_days_from_civil(y1, m1, d1), b = date_days_from_civil(y2, m2, d2);
            int64_t diff = b - a;
            snprintf(out, cap, "%04d-%02d-%02d 到 %04d-%02d-%02d 相差 %lld 天",
                     y1, m1, d1, y2, m2, d2, (long long)(diff < 0 ? -diff : diff));
            return 1;
        }
    }
    /* ② 星期几 */
    if (strstr(in, "星期") || strstr(in, "周几") || strstr(in, "礼拜")) {
        static const char *const WN[7] = { "周日", "周一", "周二", "周三", "周四", "周五", "周六" };
        int64_t z = date_days_from_civil(y1, m1, d1);
        int w = (int)(((z % 7) + 7 + 4) % 7);        /* 1970-01-01 是周四 */
        snprintf(out, cap, "%04d-%02d-%02d 是 %s", y1, m1, d1, WN[w]);
        return 1;
    }
    /* ③ 之后/之前 N 天 */
    {
        const char *p = e1;
        int before = 0;
        if (strstr(p, "之后") || strstr(p, "以后") || strstr(p, "后")) before = 0;
        else if (strstr(p, "之前") || strstr(p, "以前") || strstr(p, "前")) before = 1;
        else return 0;
        if (strstr(p, "月") || strstr(p, "年")) {
            snprintf(out, cap, "月和年的加减涉及月末约定（各家不一样），我不猜；只算天数。");
            return 1;
        }
        CalcRat v = calc_bad();
        const char *q = p;
        while (*q && (*q < '0' || *q > '9')) q++;
        if (*q) {
            char buf[32]; size_t k = 0;
            while (*q && ((*q >= '0' && *q <= '9') || *q == '.')) { if (k + 1 < sizeof buf) buf[k++] = *q; q++; }
            buf[k] = 0;
            CalcP c; c.p = buf; c.err = 0;
            v = calc_expr(&c);
            if (c.err || *c.p) v = calc_bad();
        }
        if (!v.ok || v.d != 1) return 0;
        int64_t z = date_days_from_civil(y1, m1, d1) + (before ? -v.n : v.n);
        int y3, m3, d3;
        date_civil_from_days(z, &y3, &m3, &d3);
        snprintf(out, cap, "%04d-%02d-%02d %s %lld 天 = %04d-%02d-%02d",
                 y1, m1, d1, before ? "往前" : "往后", (long long)v.n, y3, m3, d3);
        return 1;
    }
}

#endif /* DATES_H */
