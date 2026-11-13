/* tools/codetest_dep.c — 片段依赖排序的边界自测（§6.805）：
 *   ① 真调用点才算依赖（qsort 不命中 sort、函数指针传参要算）
 *   ② 成环必须**明说**（返回 -1 + 原因），不再静默凑合
 *   ③ 正常依赖能排对（被调者在前）
 */
#include "../agent.h"
#include "../action.h"
#include "../code.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("%s %s\n", cond ? "[OK]  " : "[FAIL]", what);
    if (!cond) fails++;
}

int main(void) {
    /* ① 假依赖/漏依赖 */
    ck(code_calls("qsort(a, n, sizeof a[0], cmp);", "sort") == 0, "qsort( 不算调用 sort");
    ck(code_calls("sort(a, n);", "sort") == 1, "sort(...) 算调用");
    ck(code_calls("sort (a, n);", "sort") == 1, "sort (...) 算调用");
    ck(code_calls("qsort(a, n, sizeof a[0], cmp_long);", "cmp_long") == 1,
       "当函数指针传参也算依赖");
    ck(code_calls("int maxv = 0;", "max") == 0, "maxv 不算调用 max");

    /* ③ 正常依赖：i 调 j ⇒ j 先 */
    const char *fc[4] = {"static long a(long n){ return b(n); }",
                         "static long b(long n){ return n; }"};
    const char *fn[4] = {"片段:a", "片段:b"};
    ck(code_order_pieces(fc, fn, 2, NULL, 0) == 0, "两条能排");
    ck(strstr(fc[0], "片段") == NULL && strncmp(fc[0], "static long b", 13) == 0,
       "被调的 b 排到了前面");

    /* ② 成环 */
    const char *gc[4] = {"static long a(long n){ return b(n); }",
                         "static long b(long n){ return a(n); }"};
    const char *gn[4] = {"片段:a", "片段:b"};
    char why[160] = {0};
    ck(code_order_pieces(gc, gn, 2, why, sizeof why) == -1, "成环返回 -1");
    ck(strstr(why, "成环") != NULL, "成环说清了原因");

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
