/* tools/xe_patchtest.c — 图补丁自测：只存增量、能重放、大小与图规模无关。
 * gcc -std=c11 -O2 -I. -o build_xe_patchtest tools/xe_patchtest.c -lm
 */
#include "../ai.h"
#include "../xe.h"
#include "../xe_patch.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("%s %s\n", cond ? "[OK]  " : "[FAIL]", what);
    if (!cond) fails++;
}

/* 建一张 n 个节点、n 条边的初始图（名字固定，两个进程可重建同一张图）*/
static void build_base(AI *ai, int n) {
    for (int i = 0; i < n; ++i) {
        char a[32], b[32];
        snprintf(a, sizeof a, "节点%d", i);
        snprintf(b, sizeof b, "节点%d", (i + 1) % n);
        xe_learn_needs(ai, a, b, NULL, 0);
    }
}

int main(void) {
    /* ---- 小图：2 增 1 删 ---- */
    AI *a1 = ai_new();
    build_base(a1, 50);
    char fp1[33];
    xe_graph_fp(a1, fp1, sizeof fp1);
    printf("      初始图指纹：%s\n", fp1);

    XePatch p;
    xe_patch_init(&p, "s1", fp1);
    ck(xe_patch_record_edge(&p, a1, "节点0", "需要", "新目标", 1) == 0, "记 + 边");
    ck(xe_patch_record_edge(&p, a1, "节点0", "需要", "另一个目标", 1) == 0, "记 + 边 2");
    ck(xe_patch_record_edge(&p, a1, "节点0", "需要", "节点1", 0) == 0, "记 - 边");
    const char *path = "_t/xe_patch_small.txt";
    if (system("mkdir -p _t") != 0) { /* 目录建不出来就直接把补丁写仓库根 */ }
    ck(xe_patch_save(&p, path) == 0, "补丁存盘");
    long sz = 0;
    { FILE *f = fopen(path, "r"); if (f) { fseek(f, 0, SEEK_END); sz = ftell(f); fclose(f); } }
    printf("      补丁大小：%ld 字节（50 节点图）\n", sz);
    ck(sz > 0 && sz < 512, "补丁大小与图无关（< 512 B）");

    /* 重建同一张初始图，重放补丁 */
    AI *a2 = ai_new();
    build_base(a2, 50);
    char fp2[33];
    xe_graph_fp(a2, fp2, sizeof fp2);
    ck(strcmp(fp1, fp2) == 0, "两次重建的初始图指纹一致");
    XePatch q;
    ck(xe_patch_load(&q, path) == 0, "补丁读回");
    ck(q.n == 3, "补丁 3 条");
    ck(xe_patch_apply(a2, &q, 0) == 0, "补丁重放（指纹匹配）");
    Token *n0 = xe_tok(a2, "节点0");
    ck(xe_has_needs(a2, n0, xe_tok(a2, "新目标")), "重放后 +边 在");
    ck(xe_has_needs(a2, n0, xe_tok(a2, "另一个目标")), "重放后 +边2 在");
    ck(!xe_has_needs(a2, n0, xe_tok(a2, "节点1")), "重放后 -边 不在");
    ck(xe_has_needs(a2, xe_tok(a2, "节点5"), xe_tok(a2, "节点6")), "没被补丁碰的边还在");

    /* ---- 大图：同样 3 条改动，补丁不该变大 ---- */
    AI *a3 = ai_new();
    build_base(a3, 2000);
    char fp3[33];
    xe_graph_fp(a3, fp3, sizeof fp3);
    XePatch big;
    xe_patch_init(&big, "s1", fp3);
    xe_patch_record_edge(&big, a3, "节点0", "需要", "新目标", 1);
    xe_patch_record_edge(&big, a3, "节点0", "需要", "另一个目标", 1);
    xe_patch_record_edge(&big, a3, "节点0", "需要", "节点1", 0);
    const char *path2 = "_t/xe_patch_big.txt";
    xe_patch_save(&big, path2);
    long sz2 = 0;
    { FILE *f = fopen(path2, "r"); if (f) { fseek(f, 0, SEEK_END); sz2 = ftell(f); fclose(f); } }
    printf("      补丁大小：%ld 字节（2000 节点图）\n", sz2);
    /* 只差指纹里的数字位数（96→2001、70→2000），增量本身不随图增大 */
    ck(sz2 < 512 && sz2 - sz < 16, "图放大 40 倍，补丁仍 < 512 B 且几乎不变");

    /* 指纹不符要拒绝（防把补丁打到别的图上）*/
    AI *a4 = ai_new();
    build_base(a4, 60);
    ck(xe_patch_apply(a4, &q, 0) == -2, "指纹不符被拒绝");
    ck(xe_patch_apply(a4, &q, 1) == 0, "force 可强放");

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    xe_patch_free(&p); xe_patch_free(&q); xe_patch_free(&big);
    ai_free(a1); ai_free(a2); ai_free(a3); ai_free(a4);
    return fails ? 1 : 0;
}
