/* web.h — **网页搜索**：只抽"标题 / 链接 / 摘要"，一律**待证**
 *
 * 为什么它和 net.h 分开：net.h 的纪律是"不做 HTML 正文抽取"（只走 arXiv API + PDF）。
 * 网页搜索绕不开 HTML，所以这里**只做最小的一件事**：从搜索结果页里抽标题/链接/摘要，
 * 用来给"线索"。抽出来的东西**永远不进事实层**（要信走 `::claim`），而且：
 *   · 默认关：`::net on` 才出去（复用 net.h 的开关与白名单，不另开一道门）；
 *   · 只认 https、不跟重定向、不碰 Cookie、不做正文抽取、不登录；
 *   · 每次 ≤ 2 MB / ≤ 15 s（net.h 的上限），失败照实说。
 *
 * 现在的源只有 `cn.bing.com`（实测可达；wikipedia/DDG/searx 在本机不可达）。
 * 换源 = 改 `net/allow.txt` + 加一个解析器；**解析器坏了就说"没搜到"，不许瞎猜**。
 */
#ifndef WEB_H
#define WEB_H

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include "net.h"

#define WEB_MAX_HITS 5
typedef struct { char title[200]; char url[512]; char snip[280]; } WebHit;

/* 去掉标签 + 折叠空白（抽 HTML 只做这一步） */
static inline void web_clean(char *s) {
    if (!s) return;
    char *w = s;
    for (char *p = s; *p; ++p) {
        if (*p == '<') {
            while (*p && *p != '>') p++;
            if (!*p) break;
            *w++ = ' ';
            continue;
        }
        *w++ = *p;
    }
    *w = 0;
    /* 折叠空白 */
    char *o = s;
    int sp = 0;
    for (char *p = s; *p; ++p) {
        if (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') { sp = 1; continue; }
        if (sp && o > s) *o++ = ' ';
        sp = 0;
        *o++ = *p;
    }
    *o = 0;
    /* 实体：常见命名实体 + 十进制/十六进制数字实体（Bing 的摘要里一堆 `&#0183;`/`&ensp;`） */
    struct { const char *a, *b; } rep[] = {
        {"&amp;", "&"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""},
        {"&#39;", "'"}, {"&nbsp;", " "}, {"&apos;", "'"},
        {"&ensp;", " "}, {"&emsp;", " "}, {"&thinsp;", " "},
        {"&mdash;", "—"}, {"&ndash;", "–"}, {"&hellip;", "…"},
        {"&middot;", "·"}, {"&ldquo;", "“"}, {"&rdquo;", "”"},
        {"&lsquo;", "‘"}, {"&rsquo;", "’"}, {NULL, NULL}
    };
    for (size_t i = 0; rep[i].a; ++i) {
        char *h;
        while ((h = strstr(s, rep[i].a)) != NULL) {
            size_t al = strlen(rep[i].a), bl = strlen(rep[i].b);
            memmove(h + bl, h + al, strlen(h + al) + 1);
            memcpy(h, rep[i].b, bl);
        }
    }
    /* 数字实体 `&#NNN;` / `&#xNN;`：只解到 UTF-8（够显示；解不了的原样留着） */
    {
        char *h = s;
        while ((h = strstr(h, "&#")) != NULL) {
            char *semi = strchr(h, ';');
            if (!semi || semi - h > 12) { h++; continue; }
            int base = 10;
            char *dig = h + 2;
            if (*dig == 'x' || *dig == 'X') { base = 16; dig++; }
            unsigned v = 0;
            int ok = dig < semi;
            for (char *d = dig; d < semi; ++d) {
                int c = (*d >= '0' && *d <= '9') ? *d - '0'
                      : (*d >= 'a' && *d <= 'f') ? *d - 'a' + 10
                      : (*d >= 'A' && *d <= 'F') ? *d - 'A' + 10 : -1;
                if (c < 0 || c >= base) { ok = 0; break; }
                v = v * (unsigned)base + (unsigned)c;
            }
            if (!ok || v == 0 || v > 0x10FFFF) { h++; continue; }
            char u8[5];
            size_t ul;
            if (v < 0x80) { u8[0] = (char)v; ul = 1; }
            else if (v < 0x800) { u8[0] = (char)(0xC0 | (v >> 6)); u8[1] = (char)(0x80 | (v & 0x3F)); ul = 2; }
            else if (v < 0x10000) { u8[0] = (char)(0xE0 | (v >> 12)); u8[1] = (char)(0x80 | ((v >> 6) & 0x3F));
                                    u8[2] = (char)(0x80 | (v & 0x3F)); ul = 3; }
            else { u8[0] = (char)(0xF0 | (v >> 18)); u8[1] = (char)(0x80 | ((v >> 12) & 0x3F));
                   u8[2] = (char)(0x80 | ((v >> 6) & 0x3F)); u8[3] = (char)(0x80 | (v & 0x3F)); ul = 4; }
            memmove(h + ul, semi + 1, strlen(semi + 1) + 1);
            memcpy(h, u8, ul);
            h += ul;
        }
    }
    /* 中文之间的空格去掉（剥标签时插进去的；`猫 科 猫 属` ⇒ `猫科猫属`） */
    {
        char *o = s;
        for (char *q = s; *q; ++q) {
            if (*q == ' ' && o > s && ((unsigned char)o[-1]) >= 0x80 && ((unsigned char)q[1]) >= 0x80)
                continue;
            *o++ = *q;
        }
        *o = 0;
    }
}

/* 从 Bing 结果页里抽 ≤max 条。返回条数（0 = 没抽到，照实说）。 */
static inline int web_parse_bing(const char *html, size_t n, WebHit *hits, int max) {
    if (!html || !n || !hits || max <= 0) return 0;
    const char *end = html + n;
    const char *p = html;
    int got = 0;
    while (got < max) {
        const char *b = strstr(p, "b_algo");
        if (!b || b >= end) break;
        const char *next = strstr(b + 6, "b_algo");
        if (!next || next > end) next = end;
        /* 标题：窗口里第一个 <h2 ...><a href="URL" ...>TITLE</a> */
        const char *h2 = strstr(b, "<h2");
        if (h2 && h2 < next) {
            const char *ah = strstr(h2, "<a ");
            const char *href = ah && ah < next ? strstr(ah, "href=\"") : NULL;
            if (href && href < next) {
                const char *u0 = href + 6, *u1 = strchr(u0, '"');
                const char *t0 = u1 ? strchr(u1, '>') : NULL;
                const char *t1 = t0 ? strstr(t0, "</a>") : NULL;
                if (u1 && t0 && t1 && t1 < next) {
                    WebHit *h = &hits[got];
                    size_t ul = (size_t)(u1 - u0);
                    if (ul >= sizeof h->url) ul = sizeof h->url - 1;
                    memcpy(h->url, u0, ul);
                    h->url[ul] = 0;
                    size_t tl = (size_t)(t1 - (t0 + 1));
                    if (tl >= sizeof h->title) tl = sizeof h->title - 1;
                    memcpy(h->title, t0 + 1, tl);
                    h->title[tl] = 0;
                    web_clean(h->title);
                    h->snip[0] = 0;
                    /* 摘要：标题之后的第一个 <p ...> ... </p> */
                    const char *pp = strstr(t1, "<p");
                    if (pp && pp < next) {
                        const char *p0 = strchr(pp, '>');
                        const char *p1 = p0 ? strstr(p0, "</p>") : NULL;
                        if (p0 && p1 && p1 < next) {
                            size_t sl = (size_t)(p1 - (p0 + 1));
                            if (sl >= sizeof h->snip) sl = sizeof h->snip - 1;
                            memcpy(h->snip, p0 + 1, sl);
                            h->snip[sl] = 0;
                            web_clean(h->snip);
                        }
                    }
                    if (h->title[0] && (strncmp(h->url, "http", 4) == 0)) got++;
                }
            }
        }
        p = next;
    }
    return got;
}

/* 搜一次。返回条数；0 = 没搜成（why 里写原因）。 */
static inline int web_search(const char *q, WebHit *hits, int max, char *why, size_t wcap) {
    if (why && wcap) why[0] = 0;
    if (!q || !*q) { if (why) snprintf(why, wcap, "查询是空的"); return 0; }
    char qe[512];
    net_urlencode(q, qe, sizeof qe);
    if (!qe[0]) { if (why) snprintf(why, wcap, "查询编码失败"); return 0; }
    char url[768];
    snprintf(url, sizeof url,
             "https://cn.bing.com/search?q=%s&count=%d&setlang=zh-Hans", qe, max);
    unsigned char *buf = NULL;
    size_t n = 0;
    if (net_fetch(url, &buf, &n) != 0) {
        if (why) snprintf(why, wcap, "没抓成（联网关着/不在白名单/超时，见 ::net log）");
        return 0;
    }
    int got = web_parse_bing((const char *)buf, n, hits, max);
    if (!got && why) snprintf(why, wcap, "抓到了 %zu 字节，但一条结果也没抽出来（页面结构变了就照实说）", n);
    return got;
}

#endif /* WEB_H */
