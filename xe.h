/* xe.h — XeGraphAI 的第一层新东西：空符号层 + 切碎（多跳）+ 会话记忆。
 *
 * 设计（对应 deepseek看这里.txt）：
 *   1) 文字与意思分开：词:X 只连 空:X:<sense>；空: 再连 义:<sense>。
 *      一词多义 = 同一个 词:X 连多个不同的 空:。
 *   2) 切碎：学一条长事实前先找已有多跳路径；找到就不再存长边（长事实被压成多跳）。
 *
 * 本文件只依赖现有内核原语（pool_token_intern / token_tag_dyn / rel_id_of），不改老代码。
 */
#ifndef XE_H
#define XE_H

#include <stdio.h>
#include <string.h>

#define XE_REL_HOLE  "空"
#define XE_REL_SENSE "义"
#define XE_REL_NEED  "需要"

static inline Token *xe_tok(AI *ai, const char *name) {
    if (!ai || !ai->pool || !name || !name[0]) return NULL;
    return pool_token_intern(ai->pool, name);
}

/* 关系 id 原样返回（核心关系 < REL_DYN_BASE，动态关系 >= 它）*/
static inline uint8_t xe_rid(AI *ai, const char *rel) {
    return rel_id_of(ai, rel, 1);
}

/* 核心关系与动态关系分开打边 */
static inline int xe_link(Token *a, Token *b, uint8_t rid) {
    if (!a || !b || !rid) return -1;
    if (rid >= REL_DYN_BASE) return token_tag_dyn(a, b, (uint8_t)(rid - REL_DYN_BASE + 1), 1);
    return token_tag_rel(a, b, rid, 1);
}

/* 一条边是不是这个关系（核心看 rel/nrel 位，动态看 dyn + 第 15 位）*/
static inline int xe_edge_is(const AssocEntry *e, uint8_t rid) {
    if (!e || !e->main || !rid) return 0;
    if (rid >= REL_DYN_BASE)
        return e->dyn == (uint8_t)(rid - REL_DYN_BASE + 1);
    return (e->rel & REL_BIT(rid)) != 0;
}

static inline Token *xe_tokf(AI *ai, const char *fmt, const char *a, const char *b) {
    char buf[256];
    snprintf(buf, sizeof buf, fmt, a ? a : "", b ? b : "");
    return xe_tok(ai, buf);
}

/* 空符号：**本身不携带信息**，名字只是唯一编号（空#n）；
 * 它的意思完全由它在图里的位置（连了哪些边）决定。 */
static inline Token *xe_hole_new(AI *ai) {
    static unsigned long serial = 0;
    char name[32];
    for (int tries = 0; tries < 1000000; ++tries) {
        snprintf(name, sizeof name, "空#%lu", ++serial);
        if (!pool_find(ai->pool, name)) return xe_tok(ai, name);
    }
    return NULL;
}

static inline Token *xe_sense(Token *hole);

/* 词 -> 空：按"这个词是否已经有一个指向 义:<sense> 的空"来复用（幂等）*/
static inline Token *xe_hole(AI *ai, Token *word, const char *sense) {
    if (!ai || !word || !sense || !sense[0]) return NULL;
    uint8_t d = xe_rid(ai, XE_REL_HOLE);
    uint8_t ds = xe_rid(ai, XE_REL_SENSE);
    if (!d || !ds) return NULL;
    for (size_t i = 0; i < word->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(word)[i];
        if (!e->main || !xe_edge_is(e, d)) continue;
        Token *s = xe_sense(e->main);
        if (s && s->name && strncmp(s->name, "义:", 4) == 0 && strcmp(s->name + 4, sense) == 0) return e->main;
    }
    Token *hole = xe_hole_new(ai);
    if (!hole) return NULL;
    xe_link(word, hole, d);
    Token *sn = xe_tokf(ai, "义:%s", sense, NULL);
    if (sn) xe_link(hole, sn, ds);
    return hole;
}

/* 取某个词的第 n 个空：按**建边顺序**（空名字是无意义编号，不能拿来排序）*/
static inline Token *xe_hole_n(AI *ai, Token *word, int n) {
    if (!ai || !word) return NULL;
    uint8_t d = xe_rid(ai, XE_REL_HOLE);
    int k = 0;
    for (size_t i = 0; i < word->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(word)[i];
        if (!e->main || !e->main->name || !xe_edge_is(e, d)) continue;
        if (k == n) return e->main;
        k++;
    }
    return NULL;
}

static inline Token *xe_sense(Token *hole) {
    if (!hole) return NULL;
    for (size_t i = 0; i < hole->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(hole)[i];
        if (!e->main || !e->main->name) continue;
        if (strncmp(e->main->name, "义:", 3) == 0) return e->main;
    }
    return NULL;
}

static inline Token *xe_sense_of(AI *ai, Token *word, const char *sense) {
    return xe_sense(xe_hole(ai, word, sense));
}

/* ---- 切碎：A 需要 C ---- */

static inline int xe_has_needs(AI *ai, Token *a, Token *c) {
    uint8_t d = xe_rid(ai, XE_REL_NEED);
    if (!a || !c || !d) return 0;
    for (size_t i = 0; i < a->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(a)[i];
        if (e->main == c && xe_edge_is(e, d)) return 1;
    }
    return 0;
}

static inline int xe_needs_path_dfs(AI *ai, Token *cur, Token *dst, int depth,
                                    Token **path, int *plen, int maxp) {
    if (!cur || depth <= 0) return 0;
    uint8_t d = xe_rid(ai, XE_REL_NEED);
    for (size_t i = 0; i < cur->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(cur)[i];
        if (!e->main || !xe_edge_is(e, d)) continue;
        if (e->main == dst) {
            if (*plen < maxp) path[(*plen)++] = e->main;
            return *plen;
        }
        int dup = 0;
        for (int k = 0; k < *plen; ++k) if (path[k] == e->main) dup = 1;
        if (dup) continue;
        if (*plen < maxp) path[(*plen)++] = e->main;
        if (xe_needs_path_dfs(ai, e->main, dst, depth - 1, path, plen, maxp)) return *plen;
        (*plen)--;
    }
    return 0;
}

/* 学一条 A 需要 C：图里已有 2..3 跳路径就不存长边（切碎），返回 2；
 * 否则存一条直接边，返回 1；失败 0。 */
static inline int xe_learn_needs(AI *ai, const char *a_name, const char *c_name,
                                 char *path_out, size_t cap) {
    if (!ai || !a_name || !c_name) return 0;
    Token *a = xe_tok(ai, a_name), *c = xe_tok(ai, c_name);
    if (!a || !c) return 0;
    Token *path[8]; int plen = 0;
    if (xe_needs_path_dfs(ai, a, c, 3, path, &plen, 8) > 0) {
        if (path_out && cap) {
            size_t off = 0;
            off += (size_t)snprintf(path_out + off, cap - off, "%s", a_name);
            for (int i = 0; i < plen && off < cap; ++i)
                off += (size_t)snprintf(path_out + off, cap - off, " -> %s", path[i]->name);
            snprintf(path_out + off, cap - off, "  [%d 跳]", plen);
        }
        return 2;
    }
    uint8_t d = xe_rid(ai, XE_REL_NEED);
    if (!d) return 0;
    if (xe_link(a, c, d) != 0) return 0;
    if (path_out && cap) snprintf(path_out, cap, "%s -> %s  [1 跳]", a_name, c_name);
    return 1;
}

/* ---- 自由图符号：按会话隔离的记忆锚点 ---- */
static inline Token *xe_session(AI *ai, const char *sid) {
    if (!sid || !sid[0]) sid = "default";
    return xe_tokf(ai, "自由图:%s", sid, NULL);
}

static inline int xe_session_remember(AI *ai, const char *sid, const char *rel,
                                      const char *obj) {
    Token *s = xe_session(ai, sid), *o = xe_tok(ai, obj);
    uint8_t d = xe_rid(ai, rel);
    if (!s || !o || !d) return 0;
    return xe_link(s, o, d) == 0;
}

static inline int xe_session_knows(AI *ai, const char *sid, const char *rel,
                                   const char *obj) {
    Token *s = xe_session(ai, sid), *o = xe_tok(ai, obj);
    uint8_t d = xe_rid(ai, rel);
    if (!s || !o || !d) return 0;
    for (size_t i = 0; i < s->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(s)[i];
        if (e->main == o && xe_edge_is(e, d)) return 1;
    }
    return 0;
}

#endif /* XE_H */
