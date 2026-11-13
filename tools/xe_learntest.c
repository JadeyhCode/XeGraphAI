/* tools/xe_learntest.c — 现场学习自测：从文本/HTML 学“词→空→义”和“空→关系→空”，只写补丁。
 */
#include "ai.h"
#include "xe.h"
#include "xe_patch.h"
#include "xe_session.h"
#include <unistd.h>
#include "xe_learn.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("%s %s\n", cond ? "[OK]  " : "[FAIL]", what);
    if (!cond) fails++;
}

int main(void) {
    if (system("rm -rf _t/xe_learn") != 0) { }
    setenv("XE_SESS_DIR", "_t/xe_learn", 1);
    xe_sess_mkdir();
    AI *ai = ai_new();

    const char *lesson =
        "# C 语言小抄\n"
        "指针 是 变量\n"
        "数组 是 结构\n"
        "函数 是 结构\n"
        "递归 需要 栈\n"
        "这里有一句认不出的话\n";

    XePatch p;
    xe_patch_init(&p, "learn1", "");
    int n = xe_learn_text(ai, &p, "learn1", lesson);
    printf("      学会 %d 条\n", n);
    ck(n == 4, "4 bar 学得会（认不出的那句跳过）");
    ck(xe_has_sense(ai, "指针", "变量"), "指针 的义是 变量");
    ck(xe_has_sense(ai, "数组", "结构"), "数组 的义是 结构");
    ck(xe_sense_count(ai, "函数") == 1, "函数 1 个义位");

    /* 空→关系→空：“递归 需要 栈” */
    Token *hr = xe_hole(ai, xe_tokf(ai, "词:%s", "递归", NULL), "默认");
    Token *hs = xe_hole(ai, xe_tokf(ai, "词:%s", "栈", NULL), "默认");
    uint8_t d = xe_rid(ai, "需要");
    int has_rel = 0;
    for (size_t i = 0; hr && i < hr->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(hr)[i];
        if (e->main == hs && xe_edge_is(e, d)) has_rel = 1;
    }
    ck(has_rel, "空:递归 -> 空:栈（需要）");

    /* 学来的东西必须落进补丁，不是只活在内存 */
    printf("      补丁增量 %zu 条\n", p.n);
    ck(p.n == 7, "补丁 7 条增量（3 条 是/属于 = 各 2 条边；1 条 需要 = 1 条边）");
    const char *pp = "_t/xe_learn/learn1.patch";
    ck(xe_patch_save(&p, pp) == 0, "补丁存盘");
    long sz = 0;
    { FILE *f = fopen(pp, "r"); if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); } }
    printf("      补丁 %ld B\n", sz);
    ck(sz > 0 && sz < 1024, "补丁很小");

    /* 新进程：重放补丁，学会的东西还在 */
    AI *b = ai_new();
    XePatch q;
    ck(xe_patch_load(&q, pp) == 0, "读回补丁");
    ck(xe_patch_apply(b, &q, 1) == 0, "重放补丁");
    ck(xe_has_sense(b, "指针", "变量"), "重放后 指针 仍通");

    /* 网页路径：HTML -> 文本 -> 学到（离线，用内联 HTML 模拟抓回来的页面）*/
    const char *html =
        "<html><body><h1>小抄</h1>"
        "<p>队列 是 结构</p>"
        "<p>链表 需要 指针</p>"
        "<script>var x=1;</script></body></html>";
    XePatch w;
    xe_patch_init(&w, "learn2", "");
    int m = xe_learn_html(b, &w, "learn2", html);
    printf("      网页学会 %d 条\n", m);
    ck(m == 2, "HTML 里学 2 条（script 内容不算）");
    ck(xe_has_sense(b, "队列", "结构"), "队列 的义是 结构");

    /* 保存成会话补丁：learn2 的记忆挂到 自由图:learn2 */
    { char path[256]; xe_sess_path("learn2", path, sizeof path); xe_patch_save(&w, path); }
    { char path[256]; xe_sess_path("learn2", path, sizeof path);
      ck(access(path, R_OK) == 0, "learn2 的会话补丁文件存在"); }
    { AI *c2 = ai_new();
      ck(xe_patch_apply(c2, &w, 1) == 0 && xe_has_sense(c2, "队列", "结构"),
         "另一进程重放 learn2 补丁：队列 仍通");
      ai_free(c2); }

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    xe_patch_free(&p); xe_patch_free(&q); xe_patch_free(&w);
    ai_free(ai); ai_free(b);
    return fails ? 1 : 0;
}
