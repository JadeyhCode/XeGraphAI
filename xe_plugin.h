/* xe_plugin.h — 插件：扩展符号可以有自己的**联想规则**和**行为**。
 *
 * 每个插件登记三样东西：
 *   1) 符号      图里有一个 插件:<名字>；
 *   2) 联想规则  自己的 assoc()（把输入里的词换成另一套词），可空；
 *   3) 行为     自己的 act()（真正干活），可空。
 *
 * 派发顺序：**先问联想规则，再调行为**；两者都不认就返回 0（交回主流程，
 * 插件不许劫持不认识的输入）。
 */
#ifndef XE_PLUGIN_H
#define XE_PLUGIN_H

struct XePlugin;

typedef struct XePlugin {
    const char *name;                 /* 插件名（符号 插件:<name>）*/
    const char *hint;                 /* 输入里出现这个词就轮到它 */
    int (*assoc)(struct XePlugin *p, const char *in, char *out, size_t cap);
    int (*act)(AI *ai, struct XePlugin *p, const char *in, char *out, size_t cap);
} XePlugin;

typedef struct XePluginReg {
    XePlugin *p;
    size_t n, cap;
} XePluginReg;

static inline void xe_plug_reg_init(XePluginReg *r) { memset(r, 0, sizeof *r); }
static inline void xe_plug_reg_free(XePluginReg *r) { free(r->p); r->p = NULL; r->n = r->cap = 0; }

static inline int xe_plug_register(XePluginReg *r, const XePlugin *p) {
    if (!r || !p || !p->name || !p->name[0]) return -1;
    if (r->n == r->cap) {
        size_t nc = r->cap ? r->cap * 2 : 4;
        XePlugin *np = (XePlugin *)realloc(r->p, nc * sizeof *np);
        if (!np) return -1;
        r->p = np; r->cap = nc;
    }
    r->p[r->n++] = *p;
    return 0;
}

/* 把插件挂到图上：插件:<name> --[行为]--> 行为:<name>
 *                          --[联想]--> 词:<hint>（有 hint 才连）*/
static inline void xe_plug_bind(AI *ai, const XePlugin *p) {
    if (!ai || !p || !p->name) return;
    char pn[128], bn[128];
    snprintf(pn, sizeof pn, "插件:%s", p->name);
    snprintf(bn, sizeof bn, "行为:%s", p->name);
    Token *pt = xe_tok(ai, pn), *bt = xe_tok(ai, bn);
    uint8_t d = xe_rid(ai, "行为");
    if (pt && bt && d) xe_link(pt, bt, d);
    if (p->hint && p->hint[0]) {
        char hn[128];
        snprintf(hn, sizeof hn, "词:%s", p->hint);
        Token *ht = xe_tok(ai, hn);
        uint8_t dl = xe_rid(ai, "联想");
        if (pt && ht && dl) xe_link(pt, ht, dl);
    }
}

static inline XePlugin *xe_plug_pick(XePluginReg *r, const char *in) {
    if (!r || !in) return NULL;
    for (size_t i = 0; i < r->n; ++i) {
        if (r->p[i].hint && r->p[i].hint[0] && strstr(in, r->p[i].hint)) return &r->p[i];
    }
    return NULL;
}

/* 派发：联想规则先答；答不了再走行为；都不认返回 0 */
static inline int xe_plug_dispatch(AI *ai, XePluginReg *r, const char *in,
                                   char *out, size_t cap) {
    if (!r || !in || !out || cap == 0) return 0;
    XePlugin *p = xe_plug_pick(r, in);
    if (!p) return 0;
    out[0] = 0;
    /* 联想规则先把输入“翻译”成插件自己的语言，再交给行为去算；
     * 没有行为（或行为不认）时，联想的结果本身就是答复。 */
    char buf[512];
    const char *cur = in;
    if (p->assoc) {
        buf[0] = 0;
        if (p->assoc(p, in, buf, sizeof buf) == 1 && buf[0]) cur = buf;
    }
    if (p->act && p->act(ai, p, cur, out, cap) == 1) return 1;
    if (cur != in) { snprintf(out, cap, "%s", cur); return 1; }
    return 0;
}

#endif /* XE_PLUGIN_H */
