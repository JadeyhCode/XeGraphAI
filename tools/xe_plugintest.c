/* tools/xe_plugintest.c — 插件自测：自己的联想规则 + 自己的行为 + 不劫持。
 */
#include "ai.h"
#include "xe.h"
#include "xe_plugin.h"

static int fails = 0;
static void ck(int cond, const char *what) {
    printf("%s %s\n", cond ? "[OK]  " : "[FAIL]", what);
    if (!cond) fails++;
}

/* 插件 1：回声。只有行为，没有联想规则。*/
static int echo_act(AI *ai, XePlugin *p, const char *in, char *out, size_t cap) {
    (void)ai; (void)p;
    const char *sp = strchr(in, ' ');
    if (!sp || !sp[1]) return 0;
    snprintf(out, cap, "%s", sp + 1);
    return 1;
}

/* 插件 2：计算器。自己的联想规则把「加/减」换成运算符，行为再算。*/
static int calc_assoc(XePlugin *p, const char *in, char *out, size_t cap) {
    (void)p;
    if (!strstr(in, "加") && !strstr(in, "减")) return 0;
    const char *src = in;
    size_t o = 0;
    for (size_t i = 0; src[i] && o + 4 < cap; ++i) {
        if (!strncmp(src + i, "加", 3)) { memcpy(out + o, "+", 1); o += 1; i += 2; }
        else if (!strncmp(src + i, "减", 3)) { memcpy(out + o, "-", 1); o += 1; i += 2; }
        else out[o++] = src[i];
    }
    out[o] = 0;
    return 1;
}

/* 行为只认插件自己的语言（+ / -）：联想规则已经在派发时做过了 */
static int calc_act(AI *ai, XePlugin *p, const char *in, char *out, size_t cap) {
    (void)ai; (void)p;
    long a = 0, b = 0; char op = 0;
    if (sscanf(in, "%*[^0-9-]%ld %c %ld", &a, &op, &b) != 3) return 0;
    if (op != '+' && op != '-') return 0;
    long v = (op == '+') ? a + b : a - b;
    snprintf(out, cap, "%ld", v);
    return 1;
}

int main(void) {
    AI *ai = ai_new();
    XePluginReg reg;
    xe_plug_reg_init(&reg);

    XePlugin echo = { "回声", "复读", NULL, echo_act };
    XePlugin calc = { "计算器", "算", calc_assoc, calc_act };
    ck(xe_plug_register(&reg, &echo) == 0, "注册 回声");
    ck(xe_plug_register(&reg, &calc) == 0, "注册 计算器");
    xe_plug_bind(ai, &echo);
    xe_plug_bind(ai, &calc);

    /* 图里的插件符号与自己的边 */
    Token *pt = xe_tok(ai, "插件:计算器");
    ck(pt != NULL, "图里有 插件:计算器");
    int has_act = 0, has_assoc = 0;
    for (size_t i = 0; i < pt->count; ++i) {
        const AssocEntry *e = &token_assocs_ro(pt)[i];
        if (!e->main || !e->main->name) continue;
        if (!strncmp(e->main->name, "行为:", 7)) has_act = 1;
        if (!strncmp(e->main->name, "词:", 4)) has_assoc = 1;
    }
    ck(has_act, "插件挂着 行为:计算器");
    ck(has_assoc, "插件挂着 词:算（它的联想锚点）");

    /* 派发 */
    char out[256];
    ck(xe_plug_dispatch(ai, &reg, "复读 你好", out, sizeof out) == 1 && !strcmp(out, "你好"),
       "回声插件：复读 你好 -> 你好");
    ck(xe_plug_dispatch(ai, &reg, "算 3 加 4", out, sizeof out) == 1 && !strcmp(out, "7"),
       "计算器插件：3 加 4 -> 7（走自己的联想规则）");
    ck(xe_plug_dispatch(ai, &reg, "算 9 减 2", out, sizeof out) == 1 && !strcmp(out, "7"),
       "计算器插件：9 减 2 -> 7");
    ck(xe_plug_dispatch(ai, &reg, "今天天气不错", out, sizeof out) == 0,
       "不认识的输入：插件不劫持（返回 0）");
    ck(xe_plug_dispatch(ai, &reg, "复读", out, sizeof out) == 0,
       "插件认了但答不了：也交回主流程");

    printf(fails ? "FAILED %d\n" : "ALL OK\n", fails);
    xe_plug_reg_free(&reg);
    ai_free(ai);
    return fails ? 1 : 0;
}
