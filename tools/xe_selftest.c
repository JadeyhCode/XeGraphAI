/* tools/xe_selftest.c — XeGraphAI 自带自测：空符号层 / 切碎 / 会话记忆。
 * 编法：gcc -std=c11 -O2 -I. -o build_xe_selftest tools/xe_selftest.c -lm
 */
#include "../ai.h"
#include "../xe.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("%s %s\n", cond ? "[OK]  " : "[FAIL]", what);
    if (!cond) fails++;
}

int main(void) {
    AI *ai = ai_new();
    if (!ai) { printf("ai_new failed\n"); return 2; }

    /* 1) 一词多义：苹果 = 水果 / 公司 */
    Token *apple = xe_tok(ai, "词:苹果");
    ck(apple != NULL, "建词符号 词:苹果");
    Token *h1 = xe_hole(ai, apple, "水果");
    Token *h2 = xe_hole(ai, apple, "公司");
    ck(h1 && h2 && h1 != h2, "同词两个空（多义）");
    ck(xe_hole_n(ai, apple, 0) != NULL && xe_hole_n(ai, apple, 1) != NULL,
       "按序取空 0/1");
    Token *s1 = xe_sense_of(ai, apple, "水果");
    ck(s1 && strcmp(s1->name, "义:水果") == 0, "空 -> 义:水果");
    /* 文字不能直接连意思（词 -> 义 必须经过空）*/
    int direct = 0;
    for (size_t i = 0; i < apple->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(apple)[i];
        if (e->main && e->main->name && strncmp(e->main->name, "义:", 3) == 0) direct = 1;
    }
    ck(!direct, "词不直接连义（只连空）");

    /* 2) 切碎：做游戏 -> 显示画面 -> 窗口库；再学长事实时不存长边 */
    ck(xe_learn_needs(ai, "做游戏", "显示画面", NULL, 0) == 1, "学 做游戏->显示画面（1 跳）");
    ck(xe_learn_needs(ai, "显示画面", "窗口库", NULL, 0) == 1, "学 显示画面->窗口库（1 跳）");
    char path[256] = {0};
    int r = xe_learn_needs(ai, "做游戏", "窗口库", path, sizeof path);
    ck(r == 2, "长事实被切碎（走已有多跳）");
    printf("       路径：%s\n", path);
    Token *g = xe_tok(ai, "做游戏"), *w = xe_tok(ai, "窗口库");
    ck(!xe_has_needs(ai, g, w), "没有新增 做游戏->窗口库 长边");

    /* 3) 会话记忆隔离 */
    ck(xe_session_remember(ai, "s1", "喜欢", "猫"), "s1 记住 喜欢猫");
    ck(xe_session_knows(ai, "s1", "喜欢", "猫"), "s1 查得到");
    ck(!xe_session_knows(ai, "s2", "喜欢", "猫"), "s2 查不到（会话隔离）");
    ck(xe_session(ai, "s1") != xe_session(ai, "s2"), "两个会话是不同符号");

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    ai_free(ai);
    return fails ? 1 : 0;
}
