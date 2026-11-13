/* tools/edge_probe.c — 查一个词在**文件里**的边（目标：统计/砍边时对账用）
 *
 * 用法：gcc -std=c11 -O2 -I. -o /tmp/edge_probe tools/edge_probe.c && /tmp/edge_probe <模型> <词> [宾语]
 * 输出：同名 token 的个数 / 每个的 id 与边数 / 是否存在"词 -[关系]-> 宾语"
 * 为什么要它：`::save` 之后想确认"那条边到底还在不在文件里"，看内存是看不出来的。
 */
#include "../agent.h"
#include "../model_io.h"

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "用法: %s <模型> <词> [宾语]\n", argv[0]); return 2; }
    AI *ai = ai_load_file(argv[1]);
    if (!ai) { fprintf(stderr, "读不了 %s\n", argv[1]); return 2; }
    const char *nm = argv[2];
    const char *obj = argc > 3 ? argv[3] : NULL;
    int n = 0;
    size_t total_edges = 0;
    for (size_t i = 0; i < ai->pool->count; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name || strcmp(t->name, nm)) continue;
        n++;
        total_edges += t->count;
        int has = 0;
        unsigned rid = 0;
        if (obj) {
            for (size_t k = 0; k < t->count; ++k) {
                const AssocEntry *e = &token_assocs_ro(t)[k];
                if (!e->main || !e->main->name || strcmp(e->main->name, obj)) continue;
                if (e->rel) { has = 1; for (uint8_t r = 1; r < REL_DYN_BASE; ++r) if (e->rel & REL_BIT(r)) { rid = r; break; } }
            }
        }
        printf("[%s] id=%u 边=%zu", nm, t->id, t->count);
        if (obj) printf(" 有→%s=%d%s", obj, has, has ? (rid ? "" : "(动态)") : "");
        printf("\n");
    }
    printf("[%s] 同名 token %d 个，边合计 %zu\n", nm, n, total_edges);
    ai_free(ai);
    return 0;
}
