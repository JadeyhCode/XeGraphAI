/* tools/xe_sesstest.c — 会话记忆自测：按会话隔离、补丁落盘、跨进程可恢复。
 */
#include "ai.h"
#include "xe.h"
#include "xe_patch.h"
#include "xe_session.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("%s %s\n", cond ? "[OK]  " : "[FAIL]", what);
    if (!cond) fails++;
}

int main(void) {
    if (system("rm -rf _t/xe_sess") != 0) { /* 清掉上次的会话目录 */ }
    setenv("XE_SESS_DIR", "_t/xe_sess", 1);

    /* 进程 1：s1 记 3 条，s2 记 1 条 */
    AI *a = ai_new();
    ck(xe_sess_remember(a, "s1", "喜欢", "猫") == 0, "s1 记 喜欢猫");
    ck(xe_sess_remember(a, "s1", "喜欢", "狗") == 0, "s1 记 喜欢狗");
    ck(xe_sess_remember(a, "s1", "怕", "蛇") == 0, "s1 记 怕蛇");
    ck(xe_sess_remember(a, "s2", "喜欢", "鱼") == 0, "s2 记 喜欢鱼");

    char names[8][96];
    int n1 = xe_sess_recall(a, "s1", "喜欢", names, 8);
    ck(n1 == 2, "s1 喜欢 2 条");
    ck(xe_sess_recall(a, "s2", "喜欢", names, 8) == 1, "s2 喜欢 1 条");
    ck(!xe_session_knows(a, "s2", "喜欢", "猫"), "s2 看不到 s1 的猫");
    ck(!xe_session_knows(a, "s1", "喜欢", "鱼"), "s1 看不到 s2 的鱼");

    /* 补丁文件：每会话一个，大小与图无关 */
    long sz1 = 0, sz2 = 0;
    { FILE *f = fopen("_t/xe_sess/s1.patch", "r"); if (f) { fseek(f, 0, SEEK_END); sz1 = ftell(f); fclose(f); } }
    { FILE *f = fopen("_t/xe_sess/s2.patch", "r"); if (f) { fseek(f, 0, SEEK_END); sz2 = ftell(f); fclose(f); } }
    printf("      s1.patch=%ld B  s2.patch=%ld B\n", sz1, sz2);
    ck(sz1 > 0 && sz1 < 512, "s1 补丁存在且很小");
    ck(sz2 > 0 && sz2 < sz1, "s2 只有 1 条，补丁更小");
    { struct stat st; ck(stat("_t/xe_sess/_base.fp", &st) == 0, "_base.fp 已写"); }
    ai_free(a);

    /* 进程 2：重建初始图，装载 s1 —— 三条记忆回来；再装 s2 —— 只有鱼 */
    AI *b = ai_new();
    ck(xe_sess_load(b, "s1") == 0, "装载 s1 补丁");
    ck(xe_session_knows(b, "s1", "喜欢", "猫"), "恢复：s1 喜欢猫");
    ck(xe_session_knows(b, "s1", "怕", "蛇"), "恢复：s1 怕蛇");
    ck(!xe_session_knows(b, "s1", "喜欢", "鱼"), "s1 里没有鱼");
    ck(xe_sess_load(b, "s2") == 0, "装载 s2 补丁");
    ck(xe_session_knows(b, "s2", "喜欢", "鱼"), "恢复：s2 喜欢鱼");
    ck(!xe_session_knows(b, "s2", "喜欢", "猫"), "s2 里没有猫");

    /* 遗忘：删一条，落盘；再重建就是删掉的状态 */
    ck(xe_sess_forget(b, "s1", "怕", "蛇") == 0, "s1 忘掉 怕蛇");
    ck(!xe_session_knows(b, "s1", "怕", "蛇"), "内存里已忘");
    ai_free(b);
    AI *c = ai_new();
    xe_sess_load(c, "s1");
    ck(!xe_session_knows(c, "s1", "怕", "蛇"), "重载后仍是忘掉的");
    ck(xe_session_knows(c, "s1", "喜欢", "猫"), "其它记忆没被误删");
    ai_free(c);

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
