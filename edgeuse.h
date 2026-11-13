/* edgeuse.h — **边命中率统计**（目标：统计现有边的命中率、砍死边）
 *
 * 只统计**回答/推理时真正读到的边**（`rel_direct` / `rel_direct_neg` / `rel_next_cands`），
 * 不统计"浏览"（`::rel <词>` 列知识）、也不统计学习时的写入 —— 那两类会把每条边都算成"用过"。
 *
 * 实现：一次 `edgeuse_init()` 把 (token, 边下标) 映射成全局序号（前缀和），
 * 之后 `edgeuse_hit()` 只是给位图置 1。工作负载期间图不变（不学习），所以映射稳定；
 * 之后新长出来的词/边不在统计范围内（会单独报出来）。
 */
#ifndef EDGEUSE_H
#define EDGEUSE_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t *g_eu_base;       /* token id → 该 token 在全局边序号里的起点 */
static uint8_t  *g_eu_hit;        /* 位图：1 = 这次工作负载里被读过 */
static size_t    g_eu_total;      /* 统计范围内的边数 */
static size_t    g_eu_ntok;       /* 建表时的 token 数（id 上限） */
static uint32_t *g_eu_cnt;        /* 建表时每个 token 的边数：负载里新长的边不记（否则下标会串到下一个 token） */
static int       g_eu_on;

static inline int edgeuse_init(const TokenPool *p) {
    if (!p || !p->count) return -1;
    g_eu_ntok = p->count + 1024;                 /* 留余量：工作负载里可能造新词 */
    free(g_eu_base); g_eu_base = NULL;
    free(g_eu_hit);  g_eu_hit = NULL;
    free(g_eu_cnt);  g_eu_cnt = NULL;
    g_eu_base = (uint32_t *)calloc(g_eu_ntok, sizeof *g_eu_base);
    g_eu_cnt  = (uint32_t *)calloc(g_eu_ntok, sizeof *g_eu_cnt);
    if (!g_eu_base || !g_eu_cnt) { free(g_eu_base); free(g_eu_cnt); g_eu_base = NULL; g_eu_cnt = NULL; return -1; }
    size_t total = 0;
    for (size_t i = 0; i < g_eu_ntok; ++i) {
        g_eu_base[i] = (uint32_t)total;
        if (i < p->count && p->tokens[i]) {
            g_eu_cnt[i] = (uint32_t)p->tokens[i]->count;
            total += p->tokens[i]->count;
        }
    }
    g_eu_total = total;
    g_eu_hit = (uint8_t *)calloc(total ? total : 1, 1);
    if (!g_eu_hit) { free(g_eu_base); g_eu_base = NULL; return -1; }
    g_eu_on = 1;
    return 0;
}

static inline void edgeuse_hit(const Token *t, size_t idx) {
    if (!g_eu_on || !t || !g_eu_base) return;
    uint32_t id = t->id;
    if (id >= g_eu_ntok) return;                  /* 建表之后才造的词：不计 */
    if (!g_eu_cnt || idx >= g_eu_cnt[id]) return; /* 负载里新长出来的边：不计（下标会串到下个 token） */
    size_t g = (size_t)g_eu_base[id] + idx;
    if (g < g_eu_total) g_eu_hit[g] = 1;
}

static inline int edgeuse_is_on(void) { return g_eu_on; }
static inline size_t edgeuse_total(void) { return g_eu_total; }

#endif /* EDGEUSE_H */
