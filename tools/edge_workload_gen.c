/* tools/edge_workload_gen.c — 从图里**生成**边命中的工作负载（目标：统计边的命中率、砍死边）
 *
 * 为什么从图里生成：只有"把每条关系边都问一遍"，0 命中才说明**这条边读不出来**，
 * 而不是"这次负载没问到它"。输出：每行 `主语\t关系名\t宾语`（只含有关系位的边）。
 *
 * 用法：gcc -std=c11 -O2 -I. -o /tmp/edgegen tools/edge_workload_gen.c && /tmp/edgegen model.bin
 */
#include "../agent.h"
#include "../model_io.h"

int main(int argc, char **argv) {
    const char *path = argc > 1 ? argv[1] : "model.bin";
    AI *ai = ai_load_file(path);
    if (!ai) { fprintf(stderr, "读不了 %s\n", path); return 2; }
    size_t n = 0;
    for (size_t i = 0; i < ai->pool->count; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name || t->is_garbage) continue;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (!e->main || !e->main->name) continue;
            if (e->dyn) {
                const char *dn = rel_dyn_name(ai, (uint8_t)(REL_DYN_BASE + e->dyn - 1));
                if (dn) { printf("%s\t%s\t%s\t%s\n", t->name, dn, e->main->name,
                                 edge_dyn_neg(e) ? "neg" : "pos"); n++; }
                continue;
            }
            for (size_t k = 0; AI_RELS[k].name; ++k) {
                if (!(e->rel & REL_BIT(AI_RELS[k].id))) continue;
                int dup = 0;
                for (size_t q = 0; q < k; ++q)
                    if (AI_RELS[q].id == AI_RELS[k].id) { dup = 1; break; }
                if (dup) continue;
                printf("%s\t%s\t%s\tpos\n", t->name, AI_RELS[k].name, e->main->name); n++;
            }
            for (size_t k = 0; AI_RELS[k].name; ++k) {
                if (!(e->nrel & REL_BIT(AI_RELS[k].id))) continue;
                int dup = 0;
                for (size_t q = 0; q < k; ++q)
                    if (AI_RELS[q].id == AI_RELS[k].id) { dup = 1; break; }
                if (dup) continue;
                printf("%s\t%s\t%s\tneg\n", t->name, AI_RELS[k].name, e->main->name); n++;
            }
        }
    }
    fprintf(stderr, "关系边 %zu 条\n", n);
    ai_free(ai);
    return 0;
}
