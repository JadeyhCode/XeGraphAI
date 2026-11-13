/* xe_learn.h — 现场学习：从文本/网页里抽“词→空→义”和“空→关系→空”，只写补丁。
 *
 * 为什么走空符号层：学来的新词不许直接连文字（那就变成字符串匹配了）；
 * 必须先落成 空:X:<义>，再由空去连义/连别的空。
 *
 * 支持的句式（够用来现场学一门语言/专业的小抄）：
 *   X 是 Y          → 词:X --空--> 空:X:Y --义--> 义:Y
 *   X 属于 Y        → 同上（义 = Y）
 *   X 也叫 Y        → 同义：把 Y 也接到同一个空上
 *   X 需要 Y        → 空:X:默认 --需要--> 空:Y:默认
 * 认不出的行**跳过**（返回计数里不算），不硬编。
 */
#ifndef XE_LEARN_H
#define XE_LEARN_H

#include <ctype.h>

static inline int xe_learn_is(AI *ai, const char *term, const char *sense) {
    Token *w = xe_tokf(ai, "词:%s", term, NULL);
    if (!w || !sense || !sense[0]) return 0;
    return xe_hole(ai, w, sense) != NULL;
}

static inline int xe_learn_rel(AI *ai, const char *a, const char *rel, const char *b) {
    Token *ha = xe_hole(ai, xe_tokf(ai, "词:%s", a, NULL), "默认");
    Token *hb = xe_hole(ai, xe_tokf(ai, "词:%s", b, NULL), "默认");
    uint8_t d = xe_rid(ai, rel);
    if (!ha || !hb || !d) return 0;
    return xe_link(ha, hb, d) == 0;
}

/* 词有几个义（空就是义位）*/
static inline int xe_sense_count(AI *ai, const char *term) {
    Token *w = xe_tokf(ai, "词:%s", term, NULL);
    if (!w) return 0;
    int n = 0;
    for (int i = 0; xe_hole_n(ai, w, i); ++i) n++;
    return n;
}

static inline int xe_has_sense(AI *ai, const char *term, const char *sense) {
    Token *w = xe_tokf(ai, "词:%s", term, NULL);
    Token *h = xe_hole_n(ai, w, 0);
    while (h) {
        Token *s = xe_sense(h);
        if (s && s->name && strstr(s->name, sense)) return 1;
        static int idx;
        idx++;
        break;
    }
    for (int i = 0; (h = xe_hole_n(ai, w, i)) != NULL; ++i) {
        Token *s = xe_sense(h);
        if (s && s->name && strstr(s->name, sense)) return 1;
    }
    return 0;
}

static inline void xe_trim(char *s) {
    if (!s) return;
    char *p = s;
    while (*p && isspace((unsigned char)*p)) p++;
    if (p != s) memmove(s, p, strlen(p) + 1);
    size_t l = strlen(s);
    while (l && isspace((unsigned char)s[l - 1])) s[--l] = 0;
}

/* 一行一条；返回学会的条数（认不出的跳过）*/
static inline int xe_learn_line(AI *ai, XePatch *p, const char *sid, const char *line_in) {
    char line[512];
    snprintf(line, sizeof line, "%s", line_in ? line_in : "");
    xe_trim(line);
    if (line[0] == '#' || line[0] == 0) return 0;
    struct { const char *op; int rel; } pats[] = {
        { " 是 ", 0 }, { " 属于 ", 0 }, { " 需要 ", 1 }, { " 也叫 ", 2 },
    };
    for (size_t k = 0; k < sizeof pats / sizeof pats[0]; ++k) {
        const char *hit = strstr(line, pats[k].op);
        if (!hit) continue;
        char a[192], b[192];
        size_t al = (size_t)(hit - line);
        if (al >= sizeof a) continue;
        memcpy(a, line, al); a[al] = 0;
        snprintf(b, sizeof b, "%s", hit + strlen(pats[k].op));
        xe_trim(a); xe_trim(b);
        if (!a[0] || !b[0]) return 0;
        int ok = 0;
        if (pats[k].rel == 0) ok = xe_learn_is(ai, a, b);
        else if (pats[k].rel == 1) ok = xe_learn_rel(ai, a, "需要", b);
        else { /* 也叫：同义，接到同一个空 */
            Token *wa = xe_tokf(ai, "词:%s", a, NULL), *wb = xe_tokf(ai, "词:%s", b, NULL);
            Token *h = xe_hole(ai, wb, a);
            Token *d = h ? xe_hole(ai, wa, a) : NULL;
            uint8_t dr = xe_rid(ai, "同义");
            if (h && d && dr && xe_link(d, h, dr) == 0) ok = 1;
        }
        if (!ok) return 0;
        if (p) {
            /* 只记**图里真实存在的边**，否则重放只复原一半（词直接连义，空层丢了）*/
            char wa[256], hb[256], eb[256];
            snprintf(wa, sizeof wa, "词:%s", a);
            snprintf(hb, sizeof hb, "空:%s:%s", a, b);
            snprintf(eb, sizeof eb, "义:%s", b);
            if (pats[k].rel == 1) {
                char ha[256];
                snprintf(ha, sizeof ha, "空:%s:默认", a);
                xe_patch_push(p, ha, "需要", hb, 1);
            } else {
                xe_patch_push(p, wa, "空", hb, 1);
                xe_patch_push(p, hb, "义", eb, 1);
            }
        }
        (void)sid;
        return 1;
    }
    return 0;
}

/* 整段文本（\n 分条）*/
static inline int xe_learn_text(AI *ai, XePatch *p, const char *sid, const char *text) {
    if (!text) return 0;
    char buf[8192];
    snprintf(buf, sizeof buf, "%s", text);
    int n = 0;
    char *save = NULL;
    for (char *ln = strtok_r(buf, "\n", &save); ln; ln = strtok_r(NULL, "\n", &save))
        n += xe_learn_line(ai, p, sid, ln);
    return n;
}

/* 把 HTML 剥成文本（够学习用：去标签、解几个实体）*/
static inline void xe_html_to_text(const char *html, char *out, size_t cap) {
    size_t o = 0;
    int in_tag = 0;
    for (size_t i = 0; html && html[i] && o + 8 < cap; ++i) {
        char c = html[i];
        if (c == '<') { in_tag = 1; continue; }
        if (c == '>') {
            in_tag = 0;
            if (o + 1 < cap && (o == 0 || out[o - 1] != '\n')) out[o++] = '\n';
            continue;
        }
        if (in_tag) continue;
        if (c == '&') {
            if (!strncmp(html + i, "&amp;", 5)) { out[o++] = '&'; i += 4; }
            else if (!strncmp(html + i, "&lt;", 4)) { out[o++] = '<'; i += 3; }
            else if (!strncmp(html + i, "&gt;", 4)) { out[o++] = '>'; i += 3; }
            else if (!strncmp(html + i, "&quot;", 6)) { out[o++] = '"'; i += 5; }
            else out[o++] = c;
            continue;
        }
        out[o++] = c;
    }
    out[o] = 0;
}

static inline int xe_learn_html(AI *ai, XePatch *p, const char *sid, const char *html) {
    char text[8192];
    xe_html_to_text(html, text, sizeof text);
    return xe_learn_text(ai, p, sid, text);
}

#endif /* XE_LEARN_H */
