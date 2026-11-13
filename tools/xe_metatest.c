/* tools/xe_metatest.c — 元认知自测：奖励由元认知给、权重随反馈变、接受线自己漂移。
 */
#include "../ai.h"
#include "../xe.h"
#include "../xe_meta.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("%s %s\n", cond ? "[OK]  " : "[FAIL]", what);
    if (!cond) fails++;
}

int main(void) {
    XeMeta m;
    xe_meta_init(&m);

    for (int i = 0; i < 20; ++i) xe_meta_reward(&m, 0.3, 0.6, 0.5);
    double good = xe_meta_reward(&m, 0.05, 0.90, 0.70);
    double bad  = xe_meta_reward(&m, 0.90, 0.10, 0.10);
    printf("      good=%.3f bad=%.3f\n", good, bad);
    ck(good > bad, "误差小/一致/新颖 得分更高");
    ck(good > 0.85 && bad < 0.75, "好样本接近 1、坏样本明显低");

    double w0 = m.w_err;
    /* 只有"误差小"是对的（一致性/新颖度都差）⇒ 权重该往误差上挪 */
    for (int i = 0; i < 20; ++i) xe_meta_feedback(&m, 0.05, 0.1, 0.1, 1);
    printf("      w_err %.3f -> %.3f\n", w0, m.w_err);
    ck(m.w_err > w0, "正反馈把误差权重推高（权重是学出来的）");
    ck(fabs(m.w_err + m.w_cons + m.w_nov - 1.0) < 1e-6, "三个权重归一");

    /* 接受线漂移：高分基线 vs 低分基线，对同一个 0.60 的判断不同 */
    XeMeta a;
    xe_meta_init(&a);
    for (int i = 0; i < 60; ++i) xe_meta_reward(&a, 0.02, 0.98, 0.9);
    XeMeta b;
    xe_meta_init(&b);
    for (int i = 0; i < 60; ++i) xe_meta_reward(&b, 0.95, 0.05, 0.05);
    double la = a.mu_reward + a.k_accept * sqrt(a.var_reward > 1e-9 ? a.var_reward : 1e-9);
    double lb = b.mu_reward + b.k_accept * sqrt(b.var_reward > 1e-9 ? b.var_reward : 1e-9);
    printf("      高分基线线=%.3f 低分基线线=%.3f\n", la, lb);
    ck(la > lb, "接受线随经验漂移（不是固定阈值）");
    ck(!xe_meta_accept(&a, 0.60), "高分基线：0.60 被拒");
    ck(xe_meta_accept(&b, 0.60), "低分基线：0.60 被接受");

    AI *ai = ai_new();
    XeMeta g;
    xe_meta_init(&g);
    int ok1 = xe_meta_generalize(ai, &g, "猫", "会", "飞", 0.9);
    ck(ok1 == 1, "无历史：第一次泛化被采纳（0.9）");
    Token *hc = xe_hole(ai, xe_tokf(ai, "词:%s", "猫", NULL), "默认");
    Token *hf = xe_hole(ai, xe_tokf(ai, "词:%s", "飞", NULL), "默认");
    uint8_t d = xe_rid(ai, "会");
    int has = 0;
    for (size_t i = 0; hc && i < hc->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(hc)[i];
        if (e->main == hf && xe_edge_is(e, d)) has = 1;
    }
    ck(has, "采纳的边真的写到图上");

    int ok2 = xe_meta_generalize(ai, &g, "鱼", "会", "飞", 0.1);
    ck(ok2 == 0, "跟自己的历史比不过：弱证据被拒");
    Token *hy = xe_hole(ai, xe_tokf(ai, "词:%s", "鱼", NULL), "默认");
    int has2 = 0;
    for (size_t i = 0; hy && i < hy->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(hy)[i];
        if (e->main == hf && xe_edge_is(e, d)) has2 = 1;
    }
    ck(!has2, "拒绝时图没被改");

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    ai_free(ai);
    return fails ? 1 : 0;
}
