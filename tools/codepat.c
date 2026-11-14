/* tools/codepat.c — 概念层关键词（模式）单测（§6.820）
 *   ① 展开：`最大{值|的数}` → 最大値 / 最大的数
 *   ② 查找：命中位置与长度正确；没有 `{` 时走快路径
 *   ③ 端到端：`::code synth` 能用模式关键词命中“换一种说法”的描述
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
    char ex[KW_PAT_MAX][96];
    int n = code_kw_expand("最大{值|的数|元素}", ex, KW_PAT_MAX);
    ck(n == 3, "展开成 3 种说法");
    int has1 = 0, has2 = 0, has3 = 0;
    for (int i = 0; i < n; ++i) {
        if (!strcmp(ex[i], "最大值")) has1 = 1;
        if (!strcmp(ex[i], "最大的数")) has2 = 1;
        if (!strcmp(ex[i], "最大元素")) has3 = 1;
    }
    ck(has1 && has2 && has3, "三种说法都在（最大值 / 最大的数 / 最大元素）");

    size_t ml = 0;
    const char *hit = code_kw_find("读入一行整数，输出最大的数。", "最大{值|的数|元素}", &ml);
    ck(hit != NULL, "「最大的数」命中模式关键词");
    ck(ml == strlen("最大的数"), "命中长度 = 具体说法长度（不是模式长度）");
    ck(code_kw_find("输出最大值。", "最大{值|的数|元素}", &ml) != NULL, "「最大值」也命中同一条知识");
    ck(code_kw_find("输出最小。", "最大{值|的数|元素}", &ml) == NULL, "不相关说法不命中");

    ck(code_kw_find("输出所有元素的乘积", "乘积", &ml) != NULL, "没有 { 时走普通子串");

    /* §6.827 中文数字：十X / X十 / X十Y */
    ck(code_cn_num("七进制") == 7, "七 = 7");
    ck(code_cn_num("十") == 10, "十 = 10");
    ck(code_cn_num("十六") == 16, "十六 = 16");
    ck(code_cn_num("二十") == 20, "二十 = 20");
    ck(code_cn_num("二十三") == 23, "二十三 = 23");
    ck(code_cn_num_before("转十六进制", 9) == 16, "关键词前取十六 = 16");

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    return fails ? 1 : 0;
}
