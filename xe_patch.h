/* xe_patch.h — 图补丁：只存**相对初始图的增量**，不存整图。
 *
 * 一条补丁记录 = 一条边的增/删：
 *   + 做游戏 \t 需要 \t 显示画面
 *   - 做游戏 \t 需要 \t 窗口库
 *
 * 为什么只存增量：图会长到几十万条边，整图存盘每次都是 O(全图)；
 * 而一次会话真正改的通常只有几条到几十条 ⇒ 补丁是 O(改动)。
 * 文件头记 会话名 + 初始图指纹，装载时先验指纹，再按顺序重放。
 */
#ifndef XE_PATCH_H
#define XE_PATCH_H

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define XE_PATCH_MAGIC "xe-patch 1"

typedef struct XePatchEntry {
    char from[96];
    char rel[32];
    char to[96];
    int  add;                 /* 1 = 新增边，0 = 删除边 */
} XePatchEntry;

typedef struct XePatch {
    XePatchEntry *e;
    size_t n, cap;
    char session[64];
    char base_fp[33];
} XePatch;

static inline void xe_patch_init(XePatch *p, const char *session, const char *base_fp) {
    memset(p, 0, sizeof *p);
    if (session) snprintf(p->session, sizeof p->session, "%s", session);
    if (base_fp) snprintf(p->base_fp, sizeof p->base_fp, "%s", base_fp);
}

static inline void xe_patch_free(XePatch *p) {
    if (!p) return;
    free(p->e); p->e = NULL; p->n = p->cap = 0;
}

static inline int xe_patch_push(XePatch *p, const char *from, const char *rel,
                                const char *to, int add) {
    if (!p || !from || !rel || !to) return -1;
    if (p->n == p->cap) {
        size_t nc = p->cap ? p->cap * 2 : 8;
        XePatchEntry *ne = (XePatchEntry *)realloc(p->e, nc * sizeof *ne);
        if (!ne) return -1;
        p->e = ne; p->cap = nc;
    }
    XePatchEntry *x = &p->e[p->n++];
    snprintf(x->from, sizeof x->from, "%s", from);
    snprintf(x->rel, sizeof x->rel, "%s", rel);
    snprintf(x->to, sizeof x->to, "%s", to);
    x->add = add;
    return 0;
}

/* 图指纹：节点数 + 边数 + 一个顺序无关的边哈希（只用于"补丁是给哪张图打的"校验）*/
static inline void xe_graph_fp(AI *ai, char *out, size_t cap) {
    unsigned long long h = 1469598103934665603ULL;
    size_t nodes = ai && ai->pool ? ai->pool->count : 0;
    size_t edges = 0;
    for (size_t i = 0; i < nodes; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t) continue;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (!e->main) continue;
            edges++;
            unsigned long long v = ((unsigned long long)t->id << 32) ^ (unsigned long long)e->main->id ^ (unsigned long long)(e->rel + 31u * e->dyn);
            h ^= v; h *= 1099511628211ULL;
        }
    }
    snprintf(out, cap, "%zu-%zu-%016llx", nodes, edges, h);
}

/* 删除一条边：核心关系清位，动态关系清 dyn（不动其它位，零格式改动）*/
static inline int xe_unlink(AI *ai, Token *a, Token *b, uint8_t rid) {
    if (!a || !b || !rid) return -1;
    for (size_t i = 0; i < a->count; ++i) {
        AssocEntry *e = &token_assocs(a)[i];
        if (e->main != b) continue;
        if (rid >= REL_DYN_BASE) {
            if (e->dyn == (uint8_t)(rid - REL_DYN_BASE + 1)) { e->dyn = 0; a->edges_dirty = 1; return 0; }
        } else if (e->rel & REL_BIT(rid)) {
            e->rel &= (uint16_t)~REL_BIT(rid); a->edges_dirty = 1; return 0;
        }
    }
    return -1;
}

static inline int xe_patch_record_edge(XePatch *p, AI *ai, const char *from,
                                       const char *rel, const char *to, int add) {
    Token *a = xe_tok(ai, from), *b = xe_tok(ai, to);
    uint8_t rid = xe_rid(ai, rel);
    if (!a || !b || !rid) return -1;
    if (add) { if (xe_link(a, b, rid) != 0) return -1; }
    else     { if (xe_unlink(ai, a, b, rid) != 0) return -1; }
    return xe_patch_push(p, from, rel, to, add);
}

static inline int xe_patch_save(const XePatch *p, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) return -1;
    fprintf(f, "# %s\n", XE_PATCH_MAGIC);
    fprintf(f, "# session %s\n", p->session[0] ? p->session : "default");
    fprintf(f, "# base %s\n", p->base_fp);
    for (size_t i = 0; i < p->n; ++i)
        fprintf(f, "%c %s\t%s\t%s\n", p->e[i].add ? '+' : '-', p->e[i].from, p->e[i].rel, p->e[i].to);
    fclose(f);
    return 0;
}

static inline int xe_patch_load(XePatch *p, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[512];
    memset(p, 0, sizeof *p);
    while (fgets(line, sizeof line, f)) {
        size_t l = strlen(line);
        while (l && (line[l - 1] == '\n' || line[l - 1] == '\r')) line[--l] = 0;
        if (!line[0]) continue;
        if (line[0] == '#') {
            if (strncmp(line, "# session ", 10) == 0) snprintf(p->session, sizeof p->session, "%s", line + 10);
            else if (strncmp(line, "# base ", 7) == 0) snprintf(p->base_fp, sizeof p->base_fp, "%s", line + 7);
            continue;
        }
        if (line[0] != '+' && line[0] != '-') continue;
        char from[96] = {0}, rel[32] = {0}, to[96] = {0};
        if (sscanf(line + 2, "%95[^\t]\t%31[^\t]\t%95[^\t]", from, rel, to) != 3) continue;
        xe_patch_push(p, from, rel, to, line[0] == '+');
    }
    fclose(f);
    return 0;
}

/* 重放补丁：先验指纹（不一致返回 -2，但仍可 --force 重放），再逐条应用 */
static inline int xe_patch_apply(AI *ai, const XePatch *p, int force) {
    char fp[33];
    xe_graph_fp(ai, fp, sizeof fp);
    if (!force && p->base_fp[0] && strcmp(fp, p->base_fp) != 0) return -2;
    for (size_t i = 0; i < p->n; ++i) {
        Token *a = xe_tok(ai, p->e[i].from), *b = xe_tok(ai, p->e[i].to);
        uint8_t rid = xe_rid(ai, p->e[i].rel);
        if (!a || !b || !rid) return -1;
        int rc = p->e[i].add ? xe_link(a, b, rid) : xe_unlink(ai, a, b, rid);
        if (rc != 0) return -1;
    }
    return 0;
}

#endif /* XE_PATCH_H */
