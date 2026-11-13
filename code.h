/* code.h — **"写-跑-看错-改"的机械闭环**（降级版；在内核之外，和 action.h 同一层）
 *
 * 用户 2026-10-07 的目标线 A：「让它从'会对话'走到'会写代码'」。
 * 这一步**不假装**它已经会写代码 —— 它是一个**机械**闭环，四件事：
 *     写文件 → 跑编译器 → 看错误 → 按**一张规则表**改一次 → 再跑一次
 * 所以验收是"**闭环真的走了一遍**"，不是"它把程序改对了"。降级版的边界写在下面"局限"里。
 *
 * 三个入口（同一份实现）：
 *   · `::code try <相对路径>` —— 对 `_t/work/` 里的一个源文件走一遍闭环；
 *   · `AIP_CODE=<相对路径>`   —— **代理循环**（`agent_step` 末尾）自己走一遍（默认关，只走一次）；
 *   · `::code`                —— 看这一轮的痕（编译几次 / 首次 rc / 最终 rc / 用了哪条修法）。
 *
 * 修法表：**一行一条**（错误的特征 → 改什么 → 为什么）。只有两种，
 * 都是"编译器指哪儿、规则就改哪儿"的机械动作：
 *   ① 报「expected … ;」   → 在它指的**上一行末尾**补一个 `;`
 *      （gcc 报的位置是"缺分号之后的第一处"，分号该补在上一行 —— 这条规则只认这一种形状）
 *   ② 报「implicit declaration of function」 → 按函数名补对应头文件
 *      （printf/puts… → stdio.h；str…/mem… → string.h；malloc/free/exit… → stdlib.h）
 * 认不出来的错误就**停在原地并说明**（"没有已知的机械修法"）—— 降级版不猜。
 *
 * 局限（照实说）：
 *   · 只会这两类修法；**不生成代码**（源文件是外面写好的，或者记事本的草稿）；
 *   · 最多改**一次**（`CODE_MAX_FIX`）：改完还错就报出来，不进入无限重试；
 *   · 编译命令固定 `gcc -std=c11 -Wall -Werror=implicit-function-declaration`
 *     （把"没声明的函数"从警告提成错误，否则规则②永远没机会触发 —— GCC 11 只给警告）；
 *   · 产物是 `<源文件>.out`，就落在 `_t/work/` 里（和 action.h 的边界一致）。 */
#ifndef CODE_H
#define CODE_H

#include "ai.h"
#include <dirent.h>
#include "lex.h"        /* ASCII 代码词法（::ascii on 时把"逐字保留"记进痕；见下） */

#define CODE_MAX_FIX   1
static inline int code_emit_multi(AI *ai, const char *const *names, const char *const *codes,
                                  int n, const char *outbase, char *note, size_t cap);   /* §6.807 前置声明 */

#define CODE_MAX_PIECES 16        /* 一次组合最多拼几个片段（§6.708 依赖排序的数组上限） */
#define CODE_SRC_CAP   16384      /* 源文件读进来的上限（够写个小程序） */
#define CODE_ERR_CAP   192
#define CODE_OUT_NAME  ".out"

/* 修法表：一行一条（错误的特征 / 这条规则改什么 / 为什么） */
typedef struct {
    const char *err;     /* 错误行里出现这个子串就认它 */
    const char *fix;     /* 规则名（进痕、进日志） */
    const char *why;
} CodeFixRule;

static const CodeFixRule CODE_FIXES[] = {
    { "expected … ;",         "补分号",
      "gcc 说这一行缺 `;` → 在它指的**上一行末尾**补一个（它报的是缺分号之后的第一处）" },
    { "implicit declaration", "补 #include",
      "用了没声明的函数 → 按函数名补对应的头（printf/puts→stdio.h；str*/mem*→string.h；malloc/free/exit→stdlib.h）" },
    /* §6.622 **这条规则在 gcc 上以前是死的**：表里写的是 `"' undeclared"`（clang 的 ASCII 引号），
     * 而 gcc 报的是 `‘x’ undeclared`（UTF-8 弯引号）⇒ 永远匹配不上，规则 ③ 形同不存在。
     * 现在只要求出现 `undeclared`（规则 ② 的 `implicit declaration` 排在前面，函数那类仍走 ②）。 */
    { "undeclared",           "补声明",
      "用了没声明的**名字**（不是函数）→ 在出错行前补一句 `int <名字>;`（类型只能猜 int，"
      "错了它会再来一轮 —— 这就是「机械降级」的代价，照实说）" },
    { NULL, NULL, NULL }
};
#define CODE_FIX_N ((sizeof CODE_FIXES / sizeof CODE_FIXES[0]) - 1)

/* 一轮闭环的痕（`::code` 看的就是它；不进存档） */
typedef struct {
    int  attempts;                 /* 编译了几次（0 = 没跑起来） */
    int  first_rc, final_rc;
    int  rule;                     /* 用了哪条修法（-1 = 没用上） */
    char path[ACTION_PATH_CAP];    /* 源文件（相对 _t/work/） */
    char where[80];                /* 第一条错误的位置（如 `草稿.c:3:5`） */
    char err[CODE_ERR_CAP];        /* 第一条错误原文（截断） */
    char note[CODE_ERR_CAP];       /* 改了什么 / 为什么没改 */
    char lex[CODE_ERR_CAP];        /* 词法往返（只在 `::ascii on` 时算；空 = 这一层关着） */
    int  rule_line;                /* §6.698 规则实际改的是第几行（0 = 没改） */
} CodeRun;

static CodeRun g_code = { 0, -1, -1, -1, {0}, {0}, {0}, {0}, {0}, 0 };
static int g_code_auto_done = 0;   /* `AIP_CODE` 那条路只走一次（别每轮都编译） */
static inline CodeRun *code_last(void) { return &g_code; }

/* ---- 两条机械修法（都改**内存里的源文本**，改不动就返回 -1） ---- */

/* ① 在第 line 行（1 起）末尾补一个 `;` */
static inline int code_fix_semicolon(char *src, size_t cap, int line) {
    if (!src || line <= 0) return -1;
    size_t n = strlen(src), ln = 1, i = 0;
    while (i < n && ln < (size_t)line) { if (src[i] == '\n') ln++; i++; }
    if (ln != (size_t)line) return -1;
    size_t end = i;
    while (end < n && src[end] != '\n') end++;
    /* 末尾是 ';' 或 '{' 就不补（那说明规则认错了形状） */
    size_t k = end;
    while (k > i && (src[k - 1] == ' ' || src[k - 1] == '\t' || src[k - 1] == '\r')) k--;
    if (k == i || src[k - 1] == ';' || src[k - 1] == '{' || src[k - 1] == '}') return -1;
    if (n + 2 > cap) return -1;
    memmove(src + k + 1, src + k, n - k + 1);
    src[k] = ';';
    return 0;
}

/* ② 在文件开头补一个 `#include <…>`（已经有就不补） */
static inline const char *code_header_for(const char *err) {
    static const struct { const char *fn; const char *hdr; } M[] = {
        { "printf",   "stdio.h" }, { "puts",     "stdio.h" }, { "putchar", "stdio.h" },
        { "fprintf",  "stdio.h" }, { "snprintf", "stdio.h" }, { "fputs",   "stdio.h" },
        { "strlen",   "string.h" }, { "strcmp",  "string.h" }, { "strcpy",  "string.h" },
        { "strncpy",  "string.h" }, { "memcpy",  "string.h" }, { "memset",  "string.h" },
        { "strstr",   "string.h" }, { "strcat",  "string.h" },
        { "malloc",   "stdlib.h" }, { "calloc",  "stdlib.h" }, { "realloc", "stdlib.h" },
        { "free",     "stdlib.h" }, { "exit",    "stdlib.h" }, { "atoi",    "stdlib.h" },
        { "atof",     "stdlib.h" }, { "abs",     "stdlib.h" },
        { NULL, NULL }
    };
    if (!err) return NULL;
    for (size_t i = 0; M[i].fn; ++i)
        if (strstr(err, M[i].fn)) return M[i].hdr;
    return NULL;   /* 认不出函数名 → 不猜 */
}
static inline int code_fix_include(char *src, size_t cap, const char *hdr) {
    if (!src || !hdr) return -1;
    char probe[64];
    snprintf(probe, sizeof probe, "#include <%s>", hdr);
    if (strstr(src, probe)) return -1;            /* 已经有了 */
    size_t need = strlen(probe) + 1 + strlen(src) + 1;
    if (need > cap) return -1;
    char tmp[CODE_SRC_CAP];
    snprintf(tmp, sizeof tmp, "%s\n%s", probe, src);
    snprintf(src, cap, "%s", tmp);
    return 0;
}

/* **按错误文本选修法**（抽成一处：这样能用假错误串直接单测，不用真跑 gcc） */
/* §6.622 抠引号里的名字（两种引号：ASCII `'x'` / gcc 的 `‘x’`）。 */
static inline const char *code_quote_at(const char *p);

static inline const CodeFixRule *code_rule_for(const char *err) {
    if (!err || !err[0]) return NULL;
    /* 规则 0（补分号）要**真的在说分号**，而且不能是"文件到头了"那种 ——
     * `expected declaration or statement at end of input` 也含 expected/;，
     * 但补分号救不了它（认错形状就会瞎改，实测踩过）。这条判据以前散在 `code_round` 里，
     * 现在收进这个 helper：**一处放**，另一处只调它（也就能用假错误串直接单测）。 */
    if (strstr(err, "expected") && strstr(err, ";") && !strstr(err, "end of input"))
        return &CODE_FIXES[0];
    for (size_t i = 1; i < CODE_FIX_N; ++i) {
        if (!strstr(err, CODE_FIXES[i].err)) continue;
        /* §6.622 规则 ③（补声明）**只对合法的 ASCII 标识符**成立：
         * `return 不存在的东西;` 报 `‘不存在的东西’ undeclared` —— 给它补一句
         * `int 不存在的东西;` 不是"机械修法"，是瞎猜（而且 gcc 真会收下 Unicode 标识符）。
         * 认不出合法名字就当"没有已知修法"，照实说停手。 */
        if (strcmp(CODE_FIXES[i].fix, "补声明") == 0) {
            const char *q = code_quote_at(err);
            const char *n = q ? q + (q[0] == '\'' ? 1 : 3) : NULL;
            const char *e = n ? code_quote_at(n) : NULL;   /* ⚠ 必须停在**闭合引号**，不能扫到串尾 */
            int ascii_ok = (n && e && e > n);
            for (const char *t = n; ascii_ok && t && t < e; ++t)
                if (!((*t >= 'A' && *t <= 'Z') || (*t >= 'a' && *t <= 'z') ||
                      (*t >= '0' && *t <= '9') || *t == '_')) { ascii_ok = 0; break; }
            if (!ascii_ok) return NULL;
        }
        return &CODE_FIXES[i];
    }
    return NULL;
}

/* 补声明：在 line 前插一句 `int <name>;`（名字从 `'X' undeclared` 里抠）。
 * 类型只能猜 int —— 猜错就靠下一轮编译错误再修（这就是降级版的代价，不装懂）。 */
/* §6.622 **引号有两种**：clang 用 ASCII `'name'`，gcc 用 UTF-8 弯引号 `‘name’`。
 * 只认 ASCII 的话，gcc 上的「补声明」永远抠不出名字（实测 `x = 1;` ⇒ 认得出结论但名字抠不出来）。*/
static inline const char *code_quote_at(const char *p) {
    if (!p) return NULL;
    for (; *p; ++p) {
        if (*p == '\'') return p;
        if ((unsigned char)p[0] == 0xE2 && (unsigned char)p[1] == 0x80 &&
            ((unsigned char)p[2] == 0x98 || (unsigned char)p[2] == 0x99)) return p;
    }
    return NULL;
}
static inline size_t code_quote_len(const char *q) { return (q && *q == '\'') ? 1u : 3u; }

static inline int code_fix_declare(char *src, size_t cap, const char *err, int line) {
    if (!src || !err) return -1;
    const char *q = code_quote_at(err);
    if (!q) return -1;
    q += code_quote_len(q);
    const char *e = code_quote_at(q);
    if (!e || e <= q) return -1;
    char name[64];
    size_t k = 0;
    while (q < e && k + 1 < sizeof name) name[k++] = *q++;
    name[k] = 0;
    if (k == 0 || k >= sizeof name - 8) return -1;
    char ins[96];
    snprintf(ins, sizeof ins, "int %s;\n", name);
    /* 在出错行之前插入（按行数定位；行号越界就插到开头） */
    size_t off = 0; int ln = 1;
    while (src[off] && ln < line) { if (src[off] == '\n') ln++; off++; }
    size_t il = strlen(ins);
    if (strlen(src) + il + 1 > cap) return -1;
    memmove(src + off + il, src + off, strlen(src + off) + 1);
    memcpy(src + off, ins, il);
    return 0;
}


/* 有界拼字符串：小缓冲别用 `snprintf("%s")` 收长名字/长理由 ——
 * `-Werror=format-truncation` 会拦下来（项目里这个坑踩过好几次）。 */
static inline void code_cat(char *dst, size_t cap, const char *a, const char *b, const char *c) {
    const char *parts[3] = { a, b, c };
    size_t k = 0;
    if (!dst || !cap) return;
    for (int i = 0; i < 3; ++i) {
        if (!parts[i]) continue;
        for (const char *q = parts[i]; *q && k + 1 < cap; ++q) dst[k++] = *q;
    }
    dst[k] = 0;
}

/* 闭环的痕也落一个观测标记（`[代码:<最终rc>]`；不是知识）——
 * 和 `[运行:rc]` 共用 action.h 里那**同一处**落标记的实现。 */
static inline int code_observe(AI *ai, int rc) {
    return action_observe_mark(ai, MICRO_CODE_TOKEN_PREFIX, rc, "写代码:闭环");
}

/* ---- 闭环本体 ---- */

/* 从编译器输出里挑**第一条 error**，并取出它的位置串（`文件:行:列`）。 */
static inline int code_first_error(const char *out, char *where, size_t wcap,
                                   char *msg, size_t mcap, int *line_out) {
    if (where && wcap) where[0] = 0;
    if (msg && mcap) msg[0] = 0;
    if (line_out) *line_out = 0;
    if (!out) return 0;
    for (const char *q = out; *q; ) {
        const char *nl = strchr(q, '\n');
        size_t len = nl ? (size_t)(nl - q) : strlen(q);
        const char *hit = strstr(q, ": error: ");
        if (hit && (size_t)(hit - q) < len) {
            /* 位置串：从行首到 ": error:" 之前的最后一个冒号前的部分（`f.c:3:5`） */
            if (where && wcap) {
                size_t k = (size_t)(hit - q);
                if (k >= wcap) k = wcap - 1;
                memcpy(where, q, k);
                where[k] = 0;
            }
            if (msg && mcap) {
                const char *m = hit + strlen(": error: ");
                size_t k = (nl ? (size_t)(nl - m) : strlen(m));
                if (k >= mcap) k = mcap - 1;
                memcpy(msg, m, k);
                msg[k] = 0;
            }
            if (line_out && where) {          /* 位置串的最后一个字段前一个字段 = 行号 */
                const char *last = strrchr(where, ':');
                if (last) {
                    const char *prev = last;
                    while (prev > where && prev[-1] != ':') prev--;
                    *line_out = atoi(prev);
                }
            }
            return 1;
        }
        if (!nl) break;
        q = nl + 1;
    }
    return 0;
}

/* 走一遍闭环：`rel` 是 `_t/work/` 里的相对路径。
 * 返回最终退出码（0 = 编过）/ -2 = 层关着或参数不合法（理由在 note 里）。 */
/* §6.642 **多文件工程**：把 _t/work/<dir>/ 下所有 .c 一次编出来（还是走受审的 gcc）。
 * 只做"编"，不做"改" —— 多文件的自动修法还没设计（照实说，见审计）。
 * 返回 0 = 编过；非 0 = gcc 的 rc；-2 = 没跑成（没开 ::act / 目录里没 .c）。 */
static inline int code_build_dir(AI *ai, const char *dir, char *note, size_t cap) {
    if (!ai || !dir || !dir[0]) return -2;
    if (!ai->act_on) {
        if (note && cap) snprintf(note, cap, "外部执行层关着（::act on）—— 编不了");
        return -2;
    }
    /* 自己列目录拼命令行（**不用通配符**）：受审执行层不保证过 shell，通配符可能原样传给 gcc
     * ⇒ "no such file"、而错误提取又抓不到（实测：报 rc=1 但错误字段是空的）。 */
    char cmd[ACTION_CMD_CAP];
    snprintf(cmd, sizeof cmd, "gcc -std=c11 -Wall -o %s/a.out", dir);
    char full[ACTION_PATH_CAP + 64];
    snprintf(full, sizeof full, "%s/%s", ACTION_WORK_DIR, dir);
    DIR *dp = opendir(full);
    if (!dp) {
        if (note && cap) snprintf(note, cap, "打不开目录 %s", full);
        return -2;
    }
    size_t nsrc = 0;
    struct dirent *de;
    while ((de = readdir(dp))) {
        size_t l = strlen(de->d_name);
        if (l < 3 || strcmp(de->d_name + l - 2, ".c")) continue;
        size_t cl = strlen(cmd);
        int w = snprintf(cmd + cl, sizeof cmd - cl, " %s/%s", dir, de->d_name);
        if (w < 0 || cl + (size_t)w >= sizeof cmd) break;
        nsrc++;
    }
    closedir(dp);
    if (nsrc == 0) {
        if (note && cap) snprintf(note, cap, "%s 里没有 .c（编什么？）", dir);
        return -2;
    }
    int rc = action_run(ai, cmd);
    if (note && cap) {
        if (rc == 0) snprintf(note, cap, "%s：%zu 个 .c 一次编过（产物 %s/a.out）", dir, nsrc, dir);
        else {
            char where[CODE_ERR_CAP], err[CODE_ERR_CAP];
            int line = 0;
            code_first_error(action_book()->last_out, where, sizeof where, err, sizeof err, &line);
            snprintf(note, cap, "%s 编不过（rc=%d）：%s %s", dir, rc, where[0] ? where : "", err);
        }
    }
    (void)code_observe(ai, rc);
    return rc;
}

static inline int code_round(AI *ai, const char *rel);   /* §6.646：单文件测试要先走它 */

/* §6.660 **断言计数（约定，不是框架）**：产物输出里以 `ok` / `FAIL` 起头的行各算一条断言。
 * 这样"测试全绿"不只靠退出码，还能看到"跑了几条、几条红"。
 * ⚠ 只是**约定**：没有断言库、没有注册、没有覆盖率；产物仍然只看 rc + 这两类行。 */
static inline void code_count_asserts(const char *out, size_t *n_ok, size_t *n_fail) {
    if (n_ok) *n_ok = 0;
    if (n_fail) *n_fail = 0;
    if (!out) return;
    const char *p = out;
    while (*p) {
        const char *line = p;
        while (*p && *p != '\n') p++;
        size_t l = (size_t)(p - line);
        if (line[0] == 'o' && line[1] == 'k' && (l == 2 || line[2] == ' ' || line[2] == ':') && n_ok)
            (*n_ok)++;
        else if (l >= 4 && !strncmp(line, "FAIL", 4) && n_fail) (*n_fail)++;
        if (*p == '\n') p++;
    }
}

/* §6.646 **单文件产物也能跑**：`::code test <文件.c>` —— 先走一遍"写-编-看错-改"闭环，
 * 然后跑 `<文件>.out`（单文件闭环的产物名）。判据同上：产物退出码。
 * 只跑 `.out`（和 §6.645 同一条窄入口）。 */
/* noinline：它内部好几处 `snprintf(note, cap, …)` 拼长句，被内联进 `code_synth` 之后
 * 编译器能算出上限、于是 `-Wformat-truncation` 直接报错（不是真会截断，是静态分析保守）。
 * 保持独立函数体就没有这个问题，也更好在 gdb 里断。 */
static inline int code_test_file(AI *ai, const char *rel, char *note, size_t cap) {
    if (!ai || !rel || !rel[0]) return -2;
    int rc = code_round(ai, rel);
    if (rc != 0) {
        if (note && cap) snprintf(note, cap, "%s：**没编过**（rc=%d）—— 不跑", rel, rc);
        return rc;
    }
    char prog[ACTION_PATH_CAP + 32];
    snprintf(prog, sizeof prog, "%s%s", rel, CODE_OUT_NAME);   /* <文件>.out */
    int trc = action_run_product(ai, prog);
    size_t nok2 = 0, nbad2 = 0;
    code_count_asserts(action_book()->last_out, &nok2, &nbad2);
    int strict_red2 = (ai->code_strict && trc == 0 && nbad2 > 0);   /* §6.661 严格 */
    if (note && cap) {
        if (trc == 0 && !strict_red2)
            snprintf(note, cap, "%s：编过、**测试全绿**（%s.out rc=0；断言 %zu ok / %zu FAIL）",
                     rel, rel, nok2, nbad2);
        else if (strict_red2)
            snprintf(note, cap, "%s：产物 rc=0，但打了 %zu 条 FAIL（严格模式）⇒ **测试不过**",
                     rel, nbad2);
        else {
            const char *o = action_book()->last_out;
            size_t ol = o ? strlen(o) : 0;
            if (ol > 200) o += ol - 200;
            snprintf(note, cap, "%s：编过但**测试不过**（rc=%d；断言 %zu ok / %zu FAIL）：%.100s",
                     rel, trc, nok2, nbad2, o ? o : "");
        }
    }
    return trc;
}

/* §6.700 **合成器 v1**：中文描述 → 图里查任务 → 骨架 → 代码 → 写文件 → 走既有闭环。
 *
 * 它做的**只是检索 + 落地 + 验证**，不做真正的"组合"（那是 M2 之后的事）：
 *   ① 在图上扫 `任务:*`，拿它们 `关键词` 边和描述做**子串**匹配，命中最多的那个胜出；
 *   ② 顺着 `骨架` 边找到 `骨架:X`，再顺着 `代码` 边把源码取出来；
 *   ③ 写到 `_t/work/synth_<任务>.c`（走受审的写文件通道），然后 `code_test_file` 编/跑/修。
 * **消融**：把 `骨架:* 代码` 的边删掉 ⇒ 第 ② 步取不到源码 ⇒ 必须写不出来。
 * 这就是"知识真的在用"的判据（不是"代码里硬编码了一段程序"）。
 */
/* 组合器（任务和配方共用）：job --模板--> 模板:X（含 <SLOTS>）
 *   --用--> 片段:A …（按依赖排序拼接）--主体--> 一段代码
 * 返回 code_test_file 的 rc（0/1）；-3 = 没有模板/主体/代码（调用方回退/报没有）；
 * -2 = 内存不够或写文件失败。C 里不点任何任务/配方名。 */
/* §6.711 输入契约：描述里点名的「通道」（命令行 / 标准输入）。
 * 数据：通道 --[关键词]--> 词；种类 --[通道:X]--> 模板。C 里不点通道名/模板名。 */
static inline Token *code_chan_in_desc(AI *ai, const char *desc) {
    uint8_t rid_kw = rel_id_of(ai, "关键词", 0);
    if (rid_kw < REL_DYN_BASE || !desc || !desc[0]) return NULL;
    uint8_t d_kw = (uint8_t)(rid_kw - REL_DYN_BASE + 1);
    Token *best = NULL;
    size_t best_len = 0;
    for (size_t i = 0; i < ai->pool->count; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name || strncmp(t->name, "通道:", 7) != 0) continue;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (!e->main || e->dyn != d_kw || !e->main->name) continue;
            size_t l = strlen(e->main->name);
            if (l > best_len && strstr(desc, e->main->name)) { best = t; best_len = l; }
        }
    }
    return best;
}

/* §6.711 选模板：描述里点名了另一个通道 ⇒ 换该通道下同一「种类」的模板。 */
static inline Token *code_pick_template(AI *ai, Token *tmpl, const char *desc) {
    if (!tmpl || !desc || !desc[0]) return tmpl;
    uint8_t rid_kind = rel_id_of(ai, "种类", 0);
    if (rid_kind < REL_DYN_BASE) return tmpl;
    uint8_t d_kind = (uint8_t)(rid_kind - REL_DYN_BASE + 1);
    Token *ktok = NULL;
    for (size_t j = 0; j < tmpl->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(tmpl)[j];
        if (e->main && e->dyn == d_kind) { ktok = e->main; break; }
    }
    Token *chan = ktok ? code_chan_in_desc(ai, desc) : NULL;
    if (!chan || !chan->name) return tmpl;
    uint8_t rid_ch = rel_id_of(ai, chan->name, 0);
    if (rid_ch < REL_DYN_BASE) return tmpl;
    uint8_t d_ch = (uint8_t)(rid_ch - REL_DYN_BASE + 1);
    for (size_t j = 0; j < ktok->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(ktok)[j];
        if (e->main && e->main->name && e->dyn == d_ch &&
            strncmp(e->main->name, "模板:", 7) == 0 && e->main != tmpl)
            return e->main;
    }
    return tmpl;
}

/* §6.805 片段依赖判据：**真正的调用点**才算依赖。
 * 旧判据是裸子串（strstr），把注释/字符串/更长的标识符都算进去：
 * sort 会被 qsort( 命中、max 会被 maxv 命中 ⇒ 假依赖；
 * 而 "foo ("（名字和括号之间有空格）又漏掉 ⇒ 漏依赖。
 * 这里要求：前面不是标识符字符、名字之后（跳过空白）紧跟 (。 */
static inline int code_calls(const char *code, const char *name) {
    if (!code || !name || !name[0]) return 0;
    size_t nl = strlen(name);
    for (const char *p = code; (p = strstr(p, name)) != NULL; p += nl) {
        int prev_ok = (p == code) ||
                      !((p[-1] >= 97 && p[-1] <= 122) || (p[-1] >= 65 && p[-1] <= 90) ||
                        (p[-1] >= 48 && p[-1] <= 57) || p[-1] == 95);
        if (!prev_ok) continue;
        /* 后面也必须是标识符边界：这样 `qsort` 不会命中 `sort`，
         * 而 `qsort(..., cmp_long)`（当函数指针传）仍然算依赖。 */
        char nxt = p[nl];
        int next_ok = !((nxt >= 97 && nxt <= 122) || (nxt >= 65 && nxt <= 90) ||
                        (nxt >= 48 && nxt <= 57) || nxt == 95);
        if (next_ok) return 1;
    }
    return 0;
}

/* 拓扑排序：0 = 排好（fcodes/fnames 原地重排）；-1 = 成环（note 写原因）。 */
static inline int code_order_pieces(const char **fcodes, const char **fnames, int nf,
                                    char *note, size_t cap) {
    if (nf <= 1) return 0;
    int dep[CODE_MAX_PIECES][CODE_MAX_PIECES];
    memset(dep, 0, sizeof dep);
    for (int i = 0; i < nf; ++i) {
        for (int j = 0; j < nf; ++j) {
            if (i == j) continue;
            const char *need_j = fnames[j];
            const char *cj = need_j ? strrchr(need_j, 58) : NULL;   /* 冒号 */
            if (cj && cj[1]) need_j = cj + 1;
            if (!need_j || !need_j[0]) continue;
            if (code_calls(fcodes[i], need_j)) dep[i][j] = 1;   /* i 依赖 j ⇒ j 先 */
        }
    }
    int done[CODE_MAX_PIECES];
    memset(done, 0, sizeof done);
    const char *oc[CODE_MAX_PIECES], *on[CODE_MAX_PIECES];
    int k = 0;
    while (k < nf) {
        int pick = -1;
        for (int i = 0; i < nf && pick < 0; ++i) {
            if (done[i]) continue;
            int ready = 1;
            for (int j = 0; j < nf; ++j)
                if (!done[j] && dep[i][j]) { ready = 0; break; }
            if (ready) pick = i;
        }
        if (pick < 0) {
            if (note && cap) snprintf(note, cap, "片段依赖成环（%d 个片段），排序不出来 ⇒ 不拼", nf);
            return -1;
        }
        done[pick] = 1;
        oc[k] = fcodes[pick]; on[k] = fnames[pick]; k++;
    }
    for (int i = 0; i < nf; ++i) { fcodes[i] = oc[i]; fnames[i] = on[i]; }
    return 0;
}
/* §6.712 发射：模板（含 <SLOTS>）+ 片段 + 主体 ⇒ 写 synth_<outbase>.c、编、跑。
 * head 是给 note 的前缀（已经带上 "→ "）；C 里不点任何任务/配方名。 */
static inline int code_emit(AI *ai, Token *tmpl, const char *bodytext,
                            const char **fcodes, const char **fnames, int nf,
                            const char *outbase, const char *head, char *note, size_t cap) {
    if (!ai || !tmpl || !tmpl->name || !outbase) return -3;
    uint8_t rid_cd = rel_id_of(ai, "代码", 0);
    if (rid_cd < REL_DYN_BASE) return -3;
    uint8_t dyn_cd = (uint8_t)(rid_cd - REL_DYN_BASE + 1);
    const char *tcode = NULL;
    for (size_t j = 0; j < tmpl->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(tmpl)[j];
        if (e->main && e->dyn == dyn_cd && e->main->name) { tcode = e->main->name; break; }
    }
    if (!tcode) {
        if (note && cap) snprintf(note, cap, "%s模板 %s 的 代码 边被删了 ⇒ 没编过",
                                  head ? head : "", tmpl->name);
        return -2;
    }
    char *buf = (char *)malloc(CODE_SRC_CAP);
    if (!buf) { if (note && cap) snprintf(note, cap, "内存不够"); return -2; }
    size_t len = 0;
    /* §6.805 片段依赖排序：按**调用点**判依赖，拓扑排序，成环就明说（不再静默凑合）。 */
    if (code_order_pieces(fcodes, fnames, nf, note, cap) != 0) {
        free(buf);
        return -2;
    }
    for (int i = 0; i < nf; ++i) {
        int n = snprintf(buf + len, CODE_SRC_CAP - len, "%s\n", fcodes[i]);
        if (n < 0 || (size_t)n >= CODE_SRC_CAP - len) break;
        len += (size_t)n;
    }
    const char *sl = strstr(tcode, "<SLOTS>");
    if (sl) {
        size_t hl = (size_t)(sl - tcode);
        if (len + hl + strlen(bodytext ? bodytext : "") + 8 < CODE_SRC_CAP) {
            memcpy(buf + len, tcode, hl); len += hl;
            int n = snprintf(buf + len, CODE_SRC_CAP - len, "%s", bodytext ? bodytext : "");
            if (n > 0) len += (size_t)n;
            const char *tail = sl + 7;
            n = snprintf(buf + len, CODE_SRC_CAP - len, "%s\n", tail);
            if (n > 0) len += (size_t)n;
        }
    }
    char rel2[256];
    snprintf(rel2, sizeof rel2, "synth_%s.c", outbase);
    int w = action_write_file(ai, rel2, buf);
    free(buf);
    if (w != 0) {
        if (note && cap) snprintf(note, cap, "%s写文件失败：%s", head ? head : "", g_act.last_reason);
        return -2;
    }
    char nb2[1024] = {0};
    int rc2 = code_test_file(ai, rel2, nb2, sizeof nb2);
    if (note && cap) snprintf(note, cap, "%s%.120s", head ? head : "", nb2);
    return rc2;
}

/* 组合器（任务和配方共用）：job --模板--> 模板:X（含 <SLOTS>）
 *   --用--> 片段:A …（按依赖排序拼接）--主体--> 一段代码
 * 返回 code_test_file 的 rc（0/1）；-3 = 没有模板/主体/代码（调用方回退/报没有）；
 * -2 = 内存不够或写文件失败。C 里不点任何任务/配方名。 */
static inline int code_compose(AI *ai, Token *job, const char *desc, char *note, size_t cap, const char *kind) {
    if (!ai || !job || !job->name) return -3;
    uint8_t rid_tp = rel_id_of(ai, "模板", 0);
    uint8_t rid_use = rel_id_of(ai, "用", 0);
    uint8_t rid_body = rel_id_of(ai, "主体", 0);
    uint8_t rid_cd = rel_id_of(ai, "代码", 0);
    if (rid_tp < REL_DYN_BASE || rid_use < REL_DYN_BASE ||
        rid_body < REL_DYN_BASE || rid_cd < REL_DYN_BASE) return -3;
    uint8_t d_tp = (uint8_t)(rid_tp - REL_DYN_BASE + 1);
    uint8_t d_use = (uint8_t)(rid_use - REL_DYN_BASE + 1);
    uint8_t d_body = (uint8_t)(rid_body - REL_DYN_BASE + 1);
    uint8_t dyn_cd = (uint8_t)(rid_cd - REL_DYN_BASE + 1);
    Token *tmpl = NULL, *body = NULL;
    for (size_t j = 0; j < job->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(job)[j];
        if (!e->main) continue;
        if (e->dyn == d_tp) tmpl = e->main;
        else if (e->dyn == d_body) body = e->main;
    }
    if (!tmpl || !body || !body->name) return -3;
    tmpl = code_pick_template(ai, tmpl, desc);
    const char *fcodes[CODE_MAX_PIECES];
    const char *fnames[CODE_MAX_PIECES];
    int nf = 0;
    int n_use = 0;                    /* 用 边总数（用于报告"超过 16 个被截断"）*/
    for (size_t j = 0; j < job->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(job)[j];
        if (e->main && e->dyn == d_use) n_use++;
    }
    for (size_t j = 0; j < job->count && nf < CODE_MAX_PIECES; ++j) {
        const AssocEntry *e = &token_assocs_ro(job)[j];
        if (!e->main || e->dyn != d_use) continue;
        const char *fc = NULL;
        for (size_t k = 0; k < e->main->count; ++k) {
            const AssocEntry *f = &token_assocs_ro(e->main)[k];
            if (f->main && f->dyn == dyn_cd && f->main->name) { fc = f->main->name; break; }
        }
        if (!fc) continue;
        fcodes[nf] = fc;
        fnames[nf] = e->main->name ? e->main->name : "";
        nf++;
    }
    char hd[224];
    if (n_use > CODE_MAX_PIECES)
        snprintf(hd, sizeof hd, "%s：片段 %d 个（上限 %d，**只拼前 %d 个**）+ 模板 %s + 主体（%s %s）→ ",
                 kind ? kind : "组合", n_use, CODE_MAX_PIECES, CODE_MAX_PIECES,
                 tmpl->name ? tmpl->name : "?", kind ? kind : "组合", job->name);
    else
        snprintf(hd, sizeof hd, "%s：%d 个片段 + 模板 %s + 主体（%s %s）→ ",
                 kind ? kind : "组合", nf, tmpl->name ? tmpl->name : "?",
                 kind ? kind : "组合", job->name);
    return code_emit(ai, tmpl, body->name, fcodes, fnames, nf, job->name + 7, hd, note, cap);
}
/* §6.712 运算链：描述里出现 ≥2 个 运算:* 的关键词 ⇒ 按出现先后拼成
 *   long x = N; <语句1> <语句2> … printf("%ld\n", x);
 * 模板取第一个运算的 模板 边（再按描述换通道）。C 里不点运算名。
 * 返回 -1 = 不适用（<2 个运算 / 没有模板），让调用方回退到配方。 */
/* §6.725 虚词归一：匹配前去掉"全部/所有/里面/一下"这类不承载语义的词，
 * 让"数字全部加起来"能命中"数字加起来"（数据：词 --[虚词]--> 1）。 */
static inline void code_norm_desc(AI *ai, const char *desc, char *out, size_t cap) {
    if (!out || cap == 0) return;
    snprintf(out, cap, "%s", desc ? desc : "");
    uint8_t rid_f = rel_id_of(ai, "虚词", 0);
    if (rid_f < REL_DYN_BASE) return;
    uint8_t d_f = (uint8_t)(rid_f - REL_DYN_BASE + 1);
    for (size_t i = 0; i < ai->pool->count; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name || !t->name[0]) continue;
        int is_filler = 0;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (e->main && e->dyn == d_f) { is_filler = 1; break; }
        }
        if (!is_filler) continue;
        size_t wl = strlen(t->name);
        char *p = out;
        while ((p = strstr(p, t->name)) != NULL) {
            memmove(p, p + wl, strlen(p + wl) + 1);
        }
    }
}

/* §6.726 算子类别：op -> 模板 -> 种类 -> 类别（标量/数组/字符串/矩阵） */
static inline Token *code_op_category(AI *ai, Token *op) {
    if (!ai || !op) return NULL;
    uint8_t rid_tp = rel_id_of(ai, "模板", 0);
    uint8_t rid_kind = rel_id_of(ai, "种类", 0);
    uint8_t rid_cls = rel_id_of(ai, "类别", 0);
    if (rid_tp < REL_DYN_BASE || rid_kind < REL_DYN_BASE || rid_cls < REL_DYN_BASE) return NULL;
    uint8_t d_tp = (uint8_t)(rid_tp - REL_DYN_BASE + 1);
    uint8_t d_kind = (uint8_t)(rid_kind - REL_DYN_BASE + 1);
    uint8_t d_cls = (uint8_t)(rid_cls - REL_DYN_BASE + 1);
    Token *tmpl = NULL, *kind = NULL;
    for (size_t j = 0; j < op->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(op)[j];
        if (e->main && e->dyn == d_tp) { tmpl = e->main; break; }
    }
    if (!tmpl) return NULL;
    for (size_t j = 0; j < tmpl->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(tmpl)[j];
        if (e->main && e->dyn == d_kind) { kind = e->main; break; }
    }
    if (!kind) return NULL;
    for (size_t j = 0; j < kind->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(kind)[j];
        if (e->main && e->dyn == d_cls) return e->main;
    }
    return NULL;
}

#define CODE_MAX_OPS 8
/* §6.812 中文数字当参数（"七进制"里的七）：只在参数位出现时生效。 */
static inline int code_cn_num(const char *p) {
    static const struct { const char *w; int v; } T[] = {
        {"十", 10}, {"一", 1}, {"二", 2}, {"三", 3}, {"四", 4},
        {"五", 5}, {"六", 6}, {"七", 7}, {"八", 8}, {"九", 9}, {NULL, 0}
    };
    for (int i = 0; T[i].w; ++i)
        if (!strncmp(p, T[i].w, strlen(T[i].w))) return T[i].v;
    return -1;
}
/* §6.813 **覆盖率门槛（不硬答）**：描述里被选中的知识只覆盖了一小部分 ⇒ 宁可拒绝。
 * 实测反例（blind14 首跑）："判断是否已经从小到大有序" 撞上 任务:冒泡排序 的关键词"从小到大"，
 * "输出它们平方的和" 撞上 配方:整数平方 的"平方"，"相邻两个数之差的最大值" 撞上"最大值" ——
 * 三条都拼出了**语义错误但能跑**的程序。覆盖率 = 命中关键词长度和 / 描述长度（UTF-8 字节）。 */
static inline double code_cover_ratio(const char *desc, Token *unit, uint8_t d_kw) {
    if (!desc || !unit || !desc[0]) return 0.0;
    size_t dl = strlen(desc);
    if (!dl) return 0.0;
    size_t hit = 0;
    for (size_t j = 0; j < unit->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(unit)[j];
        if (!e->main || e->dyn != d_kw || !e->main->name) continue;
        if (strstr(desc, e->main->name)) hit += strlen(e->main->name);
    }
    return (double)hit / (double)dl;
}
/* §6.815 **语义闸门**：描述里提到的“内容词”，命中的知识里到底有没有？
 * 反例（blind15 实测）：
 *   「输出最小值的**下标**」→ 命中 任务:最值平均（它只会打 max/min/avg）；
 *   「输出**负数**的个数」→ 命中一个只数了 1 个的算子；
 *   「最大值与最小值**之差**」→ 又命中 最值平均。
 * 三条都拼出了“能跑但语义错”的程序 —— 这比拒绝更糟。
 * 判据：把描述里的 2 字词（去掉停用词）逐个拿到**命中单元的知识文本**里找
 * （关键词 + 用途 + 目标代码）；覆盖率低于阈值就拒绝。知识文本是数据，不是硬编码。 */
static int code_is_stop2(const char *g) {
    static const char *S[] = {"一个","程序","读入","输出","标准","输入","命令","行参","参数","整数","字符","数组",
                               "数字","每个","所有","然后","并且","给定","要求","计算","打印","结果","这些",
                               "其中","它们","时候","里面","之后","之前","是否","判断","多少","个数","一共",
                               "请写","用给","给一","如下","以下","并且","或者","以及","例如","比如", NULL};
    for (int i = 0; S[i]; ++i) if (!strncmp(g, S[i], 6)) return 1;
    return 0;
}

/* 词是否在整张图里出现过（任意 token 名包含它）—— 用来区分“图里认识但命中单元没实现”的词，
 * 和“图里根本不认识的生词”（后者不判，免得把新描述一律拒掉）。 */
static inline int code_word_known(AI *ai, const char *g) {
    for (size_t i = 0; i < ai->pool->count; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name) continue;
        if (strstr(t->name, g)) return 1;
    }
    return 0;
}
/* §6.816 **需求词闸门（窄口径）**：只盯少数“输出形态词”——正常描述里很少见，
 * 一旦出现而命中单元的知识里没有，就说明我们没实现这个需求 ⇒ 拒绝。
 * 比“内容词覆盖率”稳：覆盖率会把隐藏组大批误拒（实测见文档 9.21）。 */
static inline int code_gate_artifacts(AI *ai, Token *unit, const char *desc, char *missing, size_t cap) {
    static const char *W[] = {"下标", "索引", "之差", "包含", "有没有", "是不是", "交替", "互为", "个数", NULL};
    if (!ai || !unit || !desc) return 0;
    char kn[8192];
    size_t kl = 0;
    kl += (size_t)snprintf(kn + kl, sizeof kn - kl, "%s ", unit->name ? unit->name : "");
    for (size_t j = 0; j < unit->count && kl + 512 < sizeof kn; ++j) {
        const AssocEntry *e = &token_assocs_ro(unit)[j];
        if (!e->main || !e->main->name) continue;
        kl += (size_t)snprintf(kn + kl, sizeof kn - kl, "%s ", e->main->name);
    }
    int bad = 0;
    if (missing && cap) missing[0] = 0;
    for (int i = 0; W[i]; ++i) {
        if (!strstr(desc, W[i])) continue;
        if (strstr(kn, W[i])) continue;
        bad++;
        if (missing && cap) {
            size_t ml = strlen(missing);
            if (ml + strlen(W[i]) + 2 < cap) snprintf(missing + ml, cap - ml, "%s ", W[i]);
        }
    }
    return bad;
}
static inline double code_desc_cover(AI *ai, Token *unit, const char *desc, char *missing, size_t cap) {
    if (!ai || !unit || !desc) return 1.0;
    /* 知识文本：单元名 + 关键词 + 用途 + 目标代码（骨架/模板/主体/语句都算） */
    char kn[8192];
    size_t kl = 0;
    kl += (size_t)snprintf(kn + kl, sizeof kn - kl, "%s ", unit->name ? unit->name : "");
    for (size_t j = 0; j < unit->count && kl + 512 < sizeof kn; ++j) {
        const AssocEntry *e = &token_assocs_ro(unit)[j];
        if (!e->main || !e->main->name) continue;
        if (e->dyn == 0 || e->main->name[0] == 0) continue;
        kl += (size_t)snprintf(kn + kl, sizeof kn - kl, "%s ", e->main->name);
    }
    size_t total = 0, hit = 0;
    if (missing && cap) missing[0] = 0;
    for (size_t i = 0; desc[i] && desc[i + 5] && total < 64; i += 3) {
        if (((unsigned char)desc[i] < 0x80) || ((unsigned char)desc[i + 3] < 0x80)) continue;
        char g[7];
        memcpy(g, desc + i, 6); g[6] = 0;
        if (code_is_stop2(g)) continue;
        if (!code_word_known(ai, g)) continue;      /* 图里没这个词 ⇒ 不判 */
        total++;
        if (strstr(kn, g)) { hit++; continue; }
        if (missing && cap) {
            size_t ml = strlen(missing);
            if (ml + 7 < cap) snprintf(missing + ml, cap - ml, "%s ", g);
        }
    }
    if (total < 3) return 1.0;          /* 认识的词太少，不判 */
    return (double)hit / (double)total;
}
static inline int code_chain(AI *ai, const char *desc, int min_ops, int need_param, char *note, size_t cap) {
    if (!ai || !ai->pool || !desc || !desc[0]) return -1;
    uint8_t rid_kw = rel_id_of(ai, "关键词", 0);
    uint8_t rid_stmt = rel_id_of(ai, "语句", 0);
    uint8_t rid_tp = rel_id_of(ai, "模板", 0);
    if (rid_kw < REL_DYN_BASE || rid_stmt < REL_DYN_BASE || rid_tp < REL_DYN_BASE) return -1;
    uint8_t d_kw = (uint8_t)(rid_kw - REL_DYN_BASE + 1);
    uint8_t d_stmt = (uint8_t)(rid_stmt - REL_DYN_BASE + 1);
    uint8_t d_tp = (uint8_t)(rid_tp - REL_DYN_BASE + 1);
    /* §6.726 描述里点名的大类（数组/字符串/矩阵）：类别不符的运算直接跳过 */
    Token *desc_cat = NULL;
    size_t desc_cat_pos = (size_t)-1;
    {
        uint8_t rid_cls = rel_id_of(ai, "类别", 0);
        uint8_t rid_word = rel_id_of(ai, "词", 0);
        if (rid_cls >= REL_DYN_BASE && rid_word >= REL_DYN_BASE) {
            uint8_t d_word = (uint8_t)(rid_word - REL_DYN_BASE + 1);
            /* 取**最早出现**的大类词（"读入矩阵…再输出数组的和"应该判成矩阵，不是数组） */
            for (size_t i = 0; i < ai->pool->count; ++i) {
                Token *ct = ai->pool->tokens[i];
                if (!ct || !ct->name || strncmp(ct->name, "类别:", 7) != 0) continue;
                for (size_t j = 0; j < ct->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(ct)[j];
                    if (!e->main || e->dyn != d_word || !e->main->name) continue;
                    const char *hit = strstr(desc, e->main->name);
                    if (!hit) continue;
                    size_t p = (size_t)(hit - desc);
                    if (p < desc_cat_pos) { desc_cat = ct; desc_cat_pos = p; }
                }
            }
        }
    }
    Token *ops[CODE_MAX_OPS];
    const char *stmts[CODE_MAX_OPS];
    size_t pos[CODE_MAX_OPS];
    size_t mlen[CODE_MAX_OPS];
    /* §6.721 参数化运算：运算 --[参数]--> 占位名，描述里紧跟关键词的数字填进去（支持 P/Q 两个） */
    char pbufs[CODE_MAX_OPS][2][16];
    int npv[CODE_MAX_OPS];
    int n = 0;
    for (size_t i = 0; i < ai->pool->count && n < CODE_MAX_OPS; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name || strncmp(t->name, "运算:", 7) != 0) continue;
        size_t best_pos = (size_t)-1, best_len = 0;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (!e->main || e->dyn != d_kw || !e->main->name) continue;
            const char *hit = strstr(desc, e->main->name);
            if (!hit) continue;
            size_t p = (size_t)(hit - desc);
            /* 同一位置取更长的词（"平方和"优于"平方"），不同位置取最早 */
            if (p < best_pos || (p == best_pos && strlen(e->main->name) > best_len)) {
                best_pos = p; best_len = strlen(e->main->name);
            }
        }
        if (best_pos == (size_t)-1) continue;
        if (getenv("AIP_CHAINDBG")) fprintf(stderr, "[chain] 命中 %s @%zu\n", t->name, best_pos);
        const char *st = NULL;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (e->main && e->dyn == d_stmt && e->main->name) { st = e->main->name; break; }
        }
        if (!st) continue;

        if (need_param) {
            /* §6.723 只在"参数化运算"里找：描述里带具体数字，比任务关键词更具体 */
            uint8_t rid_p0 = rel_id_of(ai, "参数", 0);
            int has_p = 0;
            if (rid_p0 >= REL_DYN_BASE) {
                uint8_t d_p0 = (uint8_t)(rid_p0 - REL_DYN_BASE + 1);
                for (size_t j = 0; j < t->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(t)[j];
                    if (e->main && e->dyn == d_p0) { has_p = 1; break; }
                }
            }
            if (!has_p) continue;
        }
        /* §6.726b 每个运算都要和描述大类相容：标量变换（加一/减一）例外，
         * 字符串算子在数组描述里、数组算子在字符串描述里都跳过。 */
        if (desc_cat) {
            Token *oc = code_op_category(ai, t);
            if (oc && oc != desc_cat && strcmp(oc->name, "类别:标量") != 0) {
                /* 矩阵描述允许"数组"算子：矩阵行和会把 v/n 填好，再交给数组归约/逐行输出 */
                int matrix_to_array = (strcmp(desc_cat->name, "类别:矩阵") == 0 &&
                                       strcmp(oc->name, "类别:数组") == 0);
                if (!matrix_to_array) continue;
            }
        }
        /* §6.729 要求词：运算可以要求描述里**必须**出现某个词（同一关键词下消歧） */
        {
            uint8_t rid_need = rel_id_of(ai, "要求", 0);
            if (rid_need >= REL_DYN_BASE) {
                uint8_t d_need = (uint8_t)(rid_need - REL_DYN_BASE + 1);
                int unmet = 0;
                for (size_t j = 0; j < t->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(t)[j];
                    if (e->main && e->dyn == d_need && e->main->name && !strstr(desc, e->main->name)) {
                        unmet = 1; break;
                    }
                }
                if (unmet) continue;
            }
        }
        /* §6.722 运算也可以有排除词：描述里出现就说明"不是这个运算"（不硬答） */
        {
            uint8_t rid_ex = rel_id_of(ai, "排除", 0);
            if (rid_ex >= REL_DYN_BASE) {
                uint8_t d_ex = (uint8_t)(rid_ex - REL_DYN_BASE + 1);
                int excluded = 0;
                for (size_t j = 0; j < t->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(t)[j];
                    if (e->main && e->dyn == d_ex && e->main->name && strstr(desc, e->main->name)) {
                        excluded = 1; break;
                    }
                }
                if (excluded) continue;
            }
        }
        npv[n] = 0;
        {
            uint8_t rid_p = rel_id_of(ai, "参数", 0);
            if (rid_p >= REL_DYN_BASE) {
                uint8_t d_p = (uint8_t)(rid_p - REL_DYN_BASE + 1);
                int has_param = 0;
                for (size_t j = 0; j < t->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(t)[j];
                    if (e->main && e->dyn == d_p) has_param++;
                }
                if (has_param > 2) has_param = 2;
                if (has_param > 0) {
                    pbufs[n][0][0] = 0;
                    pbufs[n][1][0] = 0;
                    /* §6.728 只有"语句里带引号的占位符"（'<P>'）才按字符解析；其余一律数字 */
                    int char0 = (strstr(st, "'<P>'") != NULL);
                    int char1 = (strstr(st, "'<Q>'") != NULL);
                    const char *base = desc + best_pos + best_len;
                    const char *p = base;
                    int got = 0;
                    while (got < has_param && (size_t)(p - base) < 24 && *p) {
                        while (*p && !((*p >= '0' && *p <= '9') || *p == '-')) {
                            if ((size_t)(p - base) >= 24) break;
                            p++;
                        }
                        if (!*p || (size_t)(p - base) >= 24) {
                            /* §6.728 后面没有数字 ⇒ 试单个 ASCII 字符（"换成 b"） */
                            const char *c = base;
                            while (*c == ' ') c++;
                            if (got == 0 && char0 && ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) &&
                                !((c[1] >= 'a' && c[1] <= 'z') || (c[1] >= 'A' && c[1] <= 'Z') ||
                                  (c[1] >= '0' && c[1] <= '9'))) {
                                pbufs[n][got][0] = *c;
                                pbufs[n][got][1] = 0;
                                got++;
                                p = c + 1;
                                continue;
                            }
                            break;
                        }
                        int neg = 0;
                        if (*p == '-') { neg = 1; p++; }
                        size_t k = 0;
                        while (*p >= '0' && *p <= '9' && k < sizeof pbufs[0][0] - 2) pbufs[n][got][k++] = *p++;
                        if (k == 0) {
                            /* §6.728 单个 ASCII 字符也可以当参数（"把每个 a 换成 b"） */
                            const char *c = base;
                            while (*c == ' ') c++;
                            if (got == 0 && char0 && ((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z')) &&
                                !((c[1] >= 'a' && c[1] <= 'z') || (c[1] >= 'A' && c[1] <= 'Z') || (c[1] >= '0' && c[1] <= '9'))) {
                                pbufs[n][got][0] = *c;
                                pbufs[n][got][1] = 0;
                                p = c + 1;
                                got++;
                                continue;
                            }
                            break;
                        }
                        if (neg) { memmove(pbufs[n][got] + 1, pbufs[n][got], k); pbufs[n][got][0] = '-'; k++; }
                        pbufs[n][got][k] = 0;
                        got++;
                    }
                    /* §6.727 关键词前面也能取参数（"1 到 6 的阶乘"里 6 在"的阶乘"前；
                     * "1 到 100 里能被 3 整除"里 100 在"能被"前）。 */
                    const char *scan = desc + best_pos;   /* 从关键词起点往前，逐个参数取 */
                    while (got < has_param) {
                        const char *q = scan;
                        size_t back = 0;
                        if (q > desc) q--;
                        while (q > desc && back < 48 && !(*q >= '0' && *q <= '9') &&
                               !(*q >= 'a' && *q <= 'z') && !(*q >= 'A' && *q <= 'Z')) { q--; back++; }
                        if (q <= desc) break;
                        if (*q >= '0' && *q <= '9') {
                            const char *e = q;
                            while (e > desc && e[-1] >= '0' && e[-1] <= '9') e--;
                            q = e;                       /* 从数字串开头复制，别只取最后一位 */
                            size_t k = 0;
                            while (*q >= '0' && *q <= '9' && k < sizeof pbufs[0][0] - 2)
                                pbufs[n][got][k++] = *q++;
                            pbufs[n][got][k] = 0;
                            /* "1 到 N"里的 1 不算参数（后面跟着"到"） */
                            const char *nx = q;
                            while (*nx == ' ') nx++;
                            if (k > 0 && !(nx[0] == (char)0xE5 && nx[1] == (char)0x88 && nx[2] == (char)0xB0)) {
                                got++; scan = e; continue;     /* 继续往前取下一个参数 */
                            }
                            /* 是"1 到"里的 1：跳过它继续往前找 */
                            scan = e;
                            if (scan <= desc) break;
                            continue;
                        }
                        /* §6.728 向前取单个 ASCII 字符（"a 换成 b" 的 a；只在引号占位符时） */
                        if (((got == 0 && char0) || (got == 1 && char1)) &&
                            ((*q >= 'a' && *q <= 'z') || (*q >= 'A' && *q <= 'Z'))) {
                            if (!((q[1] >= 'a' && q[1] <= 'z') || (q[1] >= 'A' && q[1] <= 'Z') ||
                                  (q[1] >= '0' && q[1] <= '9'))) {
                                pbufs[n][got][0] = *q;
                                pbufs[n][got][1] = 0;
                                got++;
                                scan = q;
                                continue;
                            }
                        }
                        break;
                    }
                    /* §6.812 中文数字：关键词**正前面**的“七/八/十”也算参数值（"七进制"）。 */
                    if (got < has_param && got == 0 && best_pos >= 3) {
                        int cv = code_cn_num(desc + best_pos - 3);
                        if (cv > 0) {
                            snprintf(pbufs[n][0], sizeof pbufs[0][0], "%d", cv);
                            got++;
                        }
                    }
                    if (got < has_param) continue;     /* 数字不够 ⇒ 这条运算不算 */
                    npv[n] = got;
                }
            }
        }
        ops[n] = t; stmts[n] = st; pos[n] = best_pos; mlen[n] = best_len; n++;
    }
    if (n < min_ops) return -1;
    /* §6.714 关键词互相包含时只留更长的那个（"每个数的平方和" 不能再算一次"平方"） */
    {
        int keep = 0;
        for (int i = 0; i < n; ++i) {
            int covered = 0;
            for (int j = 0; j < n && !covered; ++j) {
                if (j == i || mlen[j] <= mlen[i]) continue;
                if (pos[j] <= pos[i] && pos[j] + mlen[j] >= pos[i] + mlen[i]) covered = 1;
            }
            if (covered) continue;
            ops[keep] = ops[i]; stmts[keep] = stmts[i];
            pos[keep] = pos[i]; mlen[keep] = mlen[i];
            memcpy(pbufs[keep], pbufs[i], sizeof pbufs[0]);
            npv[keep] = npv[i]; keep++;
        }
        n = keep;
    }
    /* §6.815 链也要过闸门：只用关键词判覆盖率，缺的内容词（负数/下标/之差）就不该硬答 */
    if (n >= min_ops && n > 0) {
        char miss[512] = {0};
        double cov = code_desc_cover(ai, ops[0], desc, miss, sizeof miss);
        double minc = 0.0;
        const char *th = getenv("AIP_COVER_CHAIN_MIN");
        if (th && th[0]) minc = atof(th);
        if (getenv("AIP_CHAINDBG"))
            fprintf(stderr, "[gate:chain] %s 覆盖=%.2f 缺=[%s]\n", ops[0]->name, cov, miss);
        if (cov < minc) return -1;
    }
    if (getenv("AIP_CHAINDBG")) fprintf(stderr, "[chain] 覆盖后 n=%d (min=%d)\n", n, min_ops);
    if (n < min_ops) return -1;
    for (int i = 1; i < n; ++i) {
        for (int j = i; j > 0 && pos[j] < pos[j - 1]; --j) {
            Token *to = ops[j]; ops[j] = ops[j - 1]; ops[j - 1] = to;
            const char *ts = stmts[j]; stmts[j] = stmts[j - 1]; stmts[j - 1] = ts;
            size_t tp = pos[j]; pos[j] = pos[j - 1]; pos[j - 1] = tp;
            size_t tl = mlen[j]; mlen[j] = mlen[j - 1]; mlen[j - 1] = tl;
            char tbuf[2][16];
            memcpy(tbuf, pbufs[j], sizeof tbuf);
            memcpy(pbufs[j], pbufs[j - 1], sizeof tbuf);
            memcpy(pbufs[j - 1], tbuf, sizeof tbuf);
            int tnpv = npv[j]; npv[j] = npv[j - 1]; npv[j - 1] = tnpv;
        }
    }
    /* §6.726 类别一致性：描述点名"数组/字符串/矩阵"时，**决定输入形状的第一个运算**
     * 必须属于同一大类；否则拒绝（后面的标量变换不受限）。 */
    if (desc_cat && desc_cat_pos < pos[0]) {
        Token *c0 = code_op_category(ai, ops[0]);
        if (getenv("AIP_CHAINDBG"))
            fprintf(stderr, "[chain] 类别过滤：desc=%s op=%s\n", desc_cat->name, c0 ? c0->name : "(无)");
        if (c0 && c0 != desc_cat) return -1;
    }
    Token *tmpl = NULL;
    for (size_t j = 0; j < ops[0]->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(ops[0])[j];
        if (e->main && e->dyn == d_tp) { tmpl = e->main; break; }
    }
    if (getenv("AIP_CHAINDBG")) fprintf(stderr, "[chain] 模板=%s\n", tmpl ? tmpl->name : "(无)");
    if (!tmpl) return -1;
    tmpl = code_pick_template(ai, tmpl, desc);
    /* §6.730 标量输入不能跑数组/字符串/矩阵算子（"判断它是不是小于零"里的 1 曾被当成参数，
     * 让数组算子挂到标量模板上，编译直接炸）。按模板种类的大类把不相容的算子剔掉。 */
    {
        Token *tkind = NULL;
        uint8_t rid_kind2 = rel_id_of(ai, "种类", 0);
        if (rid_kind2 >= REL_DYN_BASE) {
            uint8_t d_k2 = (uint8_t)(rid_kind2 - REL_DYN_BASE + 1);
            for (size_t j = 0; j < tmpl->count; ++j) {
                const AssocEntry *e = &token_assocs_ro(tmpl)[j];
                if (e->main && e->dyn == d_k2) { tkind = e->main; break; }
            }
        }
        Token *tcat = tkind ? code_op_category(ai, tkind) : NULL;
        (void)tcat;
        Token *tcat2 = NULL;
        if (tkind) {
            uint8_t rid_cls2 = rel_id_of(ai, "类别", 0);
            if (rid_cls2 >= REL_DYN_BASE) {
                uint8_t d_c2 = (uint8_t)(rid_cls2 - REL_DYN_BASE + 1);
                for (size_t j = 0; j < tkind->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(tkind)[j];
                    if (e->main && e->dyn == d_c2) { tcat2 = e->main; break; }
                }
            }
        }
        if (getenv("AIP_CHAINDBG"))
            fprintf(stderr, "[chain] 模板类别=%s\n", tcat2 ? tcat2->name : "(无)");
        if (tcat2 && strcmp(tcat2->name, "类别:标量") == 0) {
            int keep = 0;
            for (int i = 0; i < n; ++i) {
                Token *oc = code_op_category(ai, ops[i]);
                if (oc && strcmp(oc->name, "类别:标量") != 0) continue;
                ops[keep] = ops[i]; stmts[keep] = stmts[i];
                pos[keep] = pos[i]; mlen[keep] = mlen[i];
                memcpy(pbufs[keep], pbufs[i], sizeof pbufs[0]);
                npv[keep] = npv[i]; keep++;
            }
            n = keep;
            if (n < min_ops) return -1;
        }
    }
    /* §6.713 链条的起点变量也来自数据：模板 --[种类]--> 种类 --[起始]--> 变量名。
     * 没有 起始 边的种类（小数/字符串/数组）不走运算链，回退配方。 */
    const char *startvar = NULL;
    Token *ktok = NULL;
    {
        uint8_t rid_kind = rel_id_of(ai, "种类", 0);
        uint8_t rid_start = rel_id_of(ai, "起始", 0);
        if (rid_kind >= REL_DYN_BASE && rid_start >= REL_DYN_BASE) {
            uint8_t d_kind = (uint8_t)(rid_kind - REL_DYN_BASE + 1);
            uint8_t d_start = (uint8_t)(rid_start - REL_DYN_BASE + 1);
            for (size_t j = 0; j < tmpl->count; ++j) {
                const AssocEntry *e = &token_assocs_ro(tmpl)[j];
                if (e->main && e->dyn == d_kind) { ktok = e->main; break; }
            }
            if (ktok) for (size_t j = 0; j < ktok->count; ++j) {
                const AssocEntry *e = &token_assocs_ro(ktok)[j];
                if (e->main && e->dyn == d_start && e->main->name) { startvar = e->main->name; break; }
            }
        }
    }
    if (!startvar) return -1;
    /* §6.724 形状一致性：描述点名的形状（一串数 / 两个数 / 一行字）必须和模板种类相符，
     * 否则宁可拒绝（"一串数里最大的"不能被"两个命令行整数取大"硬答）。 */
    {
        uint8_t rid_shape = rel_id_of(ai, "形状", 0);
        if (rid_shape >= REL_DYN_BASE && ktok) {
            uint8_t d_shape = (uint8_t)(rid_shape - REL_DYN_BASE + 1);
            int other_kind = 0;
            for (size_t i = 0; i < ai->pool->count && !other_kind; ++i) {
                Token *kt = ai->pool->tokens[i];
                if (!kt || !kt->name || strncmp(kt->name, "种类:", 7) != 0 || kt == ktok) continue;
                for (size_t j = 0; j < kt->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(kt)[j];
                    if (!e->main || e->dyn != d_shape || !e->main->name) continue;
                    const char *hit = strstr(desc, e->main->name);
                    if (!hit) continue;
                    /* §6.814 只有**出现在运算词之前**的形状词才算"输入形状"。 */
                    if ((size_t)(hit - desc) < pos[0]) { other_kind = 1; break; }
                }
            }
            if (other_kind) {
                int mine = 0;
                for (size_t j = 0; j < ktok->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(ktok)[j];
                    if (!e->main || e->dyn != d_shape || !e->main->name) continue;
                    const char *hit = strstr(desc, e->main->name);
                    if (hit && (size_t)(hit - desc) < pos[0]) { mine = 1; break; }
                }
                if (!mine) return -1;
            }
        }
    }
    /* §6.717 单运算链：只有标了 单链 的种类才允许（整数种类不标，避免抢走"平方和"这类配方）。
     * §6.723 例外：参数化运算（描述里带具体数字）本身够具体，单条就允许。 */
    if (n == 1 && !need_param) {
        uint8_t rid_single = rel_id_of(ai, "单链", 0);
        if (getenv("AIP_CHAINDBG"))
            fprintf(stderr, "[chain] 单链检查 mlen=%zu rid_single=%u ktok=%s\n", mlen[0], rid_single, ktok ? ktok->name : "(无)");
        if (rid_single < REL_DYN_BASE || !ktok) return -1;
        uint8_t d_single = (uint8_t)(rid_single - REL_DYN_BASE + 1);
        int ok_single = 0;
        for (size_t j = 0; j < ktok->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(ktok)[j];
            if (e->main && e->dyn == d_single) { ok_single = 1; break; }
        }
        /* §6.810 关键词**足够长**（≥3 个汉字 = 9 字节）时，单条也算具体：
         * "平方根"不该被 §6.717 的"整数种类不标单链"挡死（那是为了不让"平方"这类短词抢走配方）。 */
        if (!ok_single && mlen[0] >= 9) ok_single = 1;
        if (!ok_single) return -1;
    }
    /* §6.718 额外的局部变量也来自数据：运算 --[声明]--> 变量名（如两个结果的 y） */
    const char *decls[4];
    int nd = 0;
    {
        uint8_t rid_decl = rel_id_of(ai, "声明", 0);
        if (rid_decl >= REL_DYN_BASE) {
            uint8_t d_decl = (uint8_t)(rid_decl - REL_DYN_BASE + 1);
            for (int i = 0; i < n; ++i) {
                for (size_t j = 0; j < ops[i]->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(ops[i])[j];
                    if (!e->main || e->dyn != d_decl || !e->main->name) continue;
                    int dup = 0;
                    for (int k = 0; k < nd; ++k) if (strcmp(decls[k], e->main->name) == 0) dup = 1;
                    if (!dup && nd < 4) decls[nd++] = e->main->name;
                }
            }
        }
    }
    /* §6.718b 原始声明语句（数组等）：运算 --[声明行]--> 一行 C 代码，原样摆进 body */
    const char *decllines[4];
    int ndl = 0;
    {
        uint8_t rid_dl = rel_id_of(ai, "声明行", 0);
        if (rid_dl >= REL_DYN_BASE) {
            uint8_t d_dl = (uint8_t)(rid_dl - REL_DYN_BASE + 1);
            for (int i = 0; i < n; ++i) {
                for (size_t j = 0; j < ops[i]->count; ++j) {
                    const AssocEntry *e = &token_assocs_ro(ops[i])[j];
                    if (!e->main || e->dyn != d_dl || !e->main->name) continue;
                    int dup = 0;
                    for (int k = 0; k < ndl; ++k) if (strcmp(decllines[k], e->main->name) == 0) dup = 1;
                    if (!dup && ndl < 4) decllines[ndl++] = e->main->name;
                }
            }
        }
    }
    /* §6.720 运算可以复用 片段:*（函数）：用 边 → 片段 --[代码]--> 函数体，排到 main 前面 */
    const char *fcodes[CODE_MAX_PIECES];
    const char *fnames[CODE_MAX_PIECES];
    int nf = 0;
    {
        uint8_t rid_use = rel_id_of(ai, "用", 0);
        uint8_t rid_fc = rel_id_of(ai, "代码", 0);
        if (rid_use >= REL_DYN_BASE && rid_fc >= REL_DYN_BASE) {
            uint8_t d_use = (uint8_t)(rid_use - REL_DYN_BASE + 1);
            uint8_t d_fc = (uint8_t)(rid_fc - REL_DYN_BASE + 1);
            for (int i = 0; i < n && nf < CODE_MAX_PIECES; ++i) {
                for (size_t j = 0; j < ops[i]->count && nf < CODE_MAX_PIECES; ++j) {
                    const AssocEntry *e = &token_assocs_ro(ops[i])[j];
                    if (!e->main || e->dyn != d_use || !e->main->name) continue;
                    const char *fc = NULL;
                    for (size_t k = 0; k < e->main->count; ++k) {
                        const AssocEntry *f = &token_assocs_ro(e->main)[k];
                        if (f->main && f->dyn == d_fc && f->main->name) { fc = f->main->name; break; }
                    }
                    if (!fc) continue;
                    int dup = 0;
                    for (int k = 0; k < nf; ++k) if (strcmp(fnames[k], e->main->name) == 0) dup = 1;
                    if (dup) continue;
                    fcodes[nf] = fc; fnames[nf] = e->main->name; nf++;
                }
            }
        }
    }
    char body[8192];   /* §6.811 语句可以很长（大数乘法这类），2 KB 会被静默截断 */
    int bl = snprintf(body, sizeof body, "    long x = %s;\n", startvar);
    if (bl < 0) return -2;
    for (int i = 0; i < nd; ++i) {
        int k = snprintf(body + bl, sizeof body - (size_t)bl, "    long %s = 0;\n", decls[i]);
        if (k < 0 || (size_t)k >= sizeof body - (size_t)bl) break;
        bl += k;
    }
    for (int i = 0; i < ndl; ++i) {
        int k = snprintf(body + bl, sizeof body - (size_t)bl, "    %s\n", decllines[i]);
        if (k < 0 || (size_t)k >= sizeof body - (size_t)bl) break;
        bl += k;
    }
    for (int i = 0; i < n; ++i) {
        char stmt[2048], stmt2[2048];
        snprintf(stmt, sizeof stmt, "%s", stmts[i]);
        if (npv[i] > 0) {
            for (int guard = 0; guard < 8; ++guard) {     /* 同一个占位符可能出现多次 */
                const char *ph = strstr(stmt, "<P>");
                if (!ph) break;
                snprintf(stmt2, sizeof stmt2, "%.*s%s%s",
                         (int)(ph - stmt), stmt, pbufs[i][0], ph + 3);
                snprintf(stmt, sizeof stmt, "%s", stmt2);
            }
        }
        if (npv[i] > 1) {
            const char *ph = strstr(stmt, "<Q>");
            if (ph) snprintf(stmt2, sizeof stmt2, "%.*s%s%s",
                             (int)(ph - stmt), stmt, pbufs[i][1], ph + 3);
            else snprintf(stmt2, sizeof stmt2, "%s", stmt);
            snprintf(stmt, sizeof stmt, "%s", stmt2);
        }
        int k = snprintf(body + bl, sizeof body - (size_t)bl, "    %s\n", stmt);
        if (k < 0 || (size_t)k >= sizeof body - (size_t)bl) break;
        bl += k;
    }
    /* §6.715/§6.716 输出的类型由**最后一个运算**的 输出 边决定：目标是 `打印:*` 节点，
     * 打印语句本身存在它的 代码 边上（字符串/整数/逐行数组都只是数据）。 */
    {
        Token *outtok = NULL;
        uint8_t rid_out = rel_id_of(ai, "输出", 0);
        if (rid_out >= REL_DYN_BASE) {
            uint8_t d_out = (uint8_t)(rid_out - REL_DYN_BASE + 1);
            /* 只认**最后一个算子**的 输出（不许向后继承：试过继承，"转大写→长度"会沿用
             * 转大写的"打印:字符串"，把长度当字符串打出来；矩阵减少一条也同理）。 */
            for (size_t j = 0; j < ops[n - 1]->count; ++j) {
                const AssocEntry *e = &token_assocs_ro(ops[n - 1])[j];
                if (e->main && e->dyn == d_out && e->main->name) { outtok = e->main; break; }
            }
        }
        const char *pcode = NULL;
        uint8_t rid_cd = rel_id_of(ai, "代码", 0);
        if (outtok && rid_cd >= REL_DYN_BASE) {
            uint8_t d_cd = (uint8_t)(rid_cd - REL_DYN_BASE + 1);
            for (size_t j = 0; j < outtok->count; ++j) {
                const AssocEntry *e = &token_assocs_ro(outtok)[j];
                if (e->main && e->dyn == d_cd && e->main->name) { pcode = e->main->name; break; }
            }
        }
        int k = snprintf(body + bl, sizeof body - (size_t)bl, "    %s\n",
                         pcode ? pcode : "printf(\"%ld\\n\", x);");
        if (k > 0) bl += k;
    }
    char outbase[96];
    if (n == 1) snprintf(outbase, sizeof outbase, "%.60s", ops[0]->name + 7);
    else snprintf(outbase, sizeof outbase, "%.40s_%.40s", ops[0]->name + 7, ops[n - 1]->name + 7);
    char hd[224];
    if (n == 1)
        snprintf(hd, sizeof hd, "链：1 个运算（%.40s）+ 模板 %.60s → ",
                 ops[0]->name + 7, tmpl->name ? tmpl->name : "?");
    else
        snprintf(hd, sizeof hd, "链：%d 个运算（%.40s → %.40s）+ 模板 %.60s → ",
                 n, ops[0]->name + 7, ops[n - 1]->name + 7, tmpl->name ? tmpl->name : "?");
    return code_emit(ai, tmpl, body, fcodes, fnames, nf, outbase, hd, note, cap);
}
static inline int code_synth(AI *ai, const char *desc, char *note, size_t cap) {
    if (note && cap) note[0] = 0;
    if (!ai || !ai->pool || !desc || !desc[0]) return -2;
    uint8_t rid_kw = rel_id_of(ai, "关键词", 0);
    uint8_t rid_sk = rel_id_of(ai, "骨架", 0);
    uint8_t rid_cd = rel_id_of(ai, "代码", 0);
    if (rid_kw < REL_DYN_BASE || rid_sk < REL_DYN_BASE || rid_cd < REL_DYN_BASE) {
        if (note && cap) snprintf(note, cap, "图上没有 关键词/骨架/代码 这三种关系 —— 没有代码知识");
        return -2;
    }
    uint8_t dyn_kw = (uint8_t)(rid_kw - REL_DYN_BASE + 1);
    uint8_t dyn_sk = (uint8_t)(rid_sk - REL_DYN_BASE + 1);
    uint8_t dyn_cd = (uint8_t)(rid_cd - REL_DYN_BASE + 1);
    /* 任务可以有**排除词**：描述里出现就说明"不是这个任务"。
     * 这条是数据（任务 --排除--> 词），C 里不点任何任务名。 */
    uint8_t rid_ex = rel_id_of(ai, "排除", 0);
    uint8_t dyn_ex = (rid_ex >= REL_DYN_BASE) ? (uint8_t)(rid_ex - REL_DYN_BASE + 1) : 0;
    /* §6.725 匹配用"去掉虚词"的副本（参数解析也用副本里的位置） */
    char ndbuf[1024];
    code_norm_desc(ai, desc, ndbuf, sizeof ndbuf);
    const char *nd = ndbuf;
    /* §6.719 运算链先于任务检索：**两个以上**运算词时，描述讲的是"一串操作"，
     * 不该被某个任务的关键词（如"矩阵""行数""最大公约数"）抢走。
     * 单运算仍留在任务之后（否则"求两个数的最大公约数"会被链条从骨架手里抢走）。 */
    {
        /* 先按"多个运算"拼（描述讲一串操作）；只有拼不出来时，才允许**单条参数化运算**
         * （描述里带具体数字，比任务关键词更具体，比如"1 到 100 的和"）。 */
        int crc_chain = code_chain(ai, nd, 2, 0, note, cap);
        if (crc_chain == -1) crc_chain = code_chain(ai, nd, 1, 1, note, cap);
        if (crc_chain != -1) return crc_chain;
    }

    /* ① 找最匹配的任务 */
    Token *best = NULL;
    int best_hits = 0;
    size_t best_len = 0;      /* 命中关键词里**最长**的那个的长度（打平手时更具体的胜出）*/
    for (size_t i = 0; i < ai->pool->count; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name || strncmp(t->name, "任务:", 7) != 0) continue;
        /* 排除词先判：命中一个就整条任务作废（「平方和」不能拿「1 到 N 的和」去答） */
        int excluded = 0;
        if (dyn_ex) for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (e->main && e->dyn == dyn_ex && e->main->name && strstr(nd, e->main->name)) {
                excluded = 1; break;
            }
        }
        if (excluded) continue;
        int hits = 0;
        size_t longest = 0;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (!e->main || e->dyn != dyn_kw || !e->main->name) continue;
            if (strstr(nd, e->main->name)) {
                hits++;
                size_t l = strlen(e->main->name);
                if (l > longest) longest = l;
            }
        }
        /* §6.703 打平手时**关键词更长的胜出**：否则"统计素数的个数"会命中更泛的 任务:素数，
         * 而不是更具体的 任务:素数统计（两边都只命中 1 个词）。 */
        if (hits > best_hits || (hits == best_hits && hits > 0 && longest > best_len)) {
            best = t; best_hits = hits; best_len = longest;
        }
    }
    if (!best) {
        /* §6.719 单运算链（数据允许的种类）排在任务之后、配方之前 */
        {
            int crc_chain = code_chain(ai, nd, 1, 0, note, cap);
            if (crc_chain != -1) return crc_chain;
        }
        /* ①c 配方回退：没有 任务 命中时，按 配方:* 的关键词/排除词找一条配方。
         * 配方和任务共用 模板/用/主体 的组合器；C 里没有按任务名分支。 */
        Token *recipe = NULL;
        int rbest = 0;
        size_t rlong = 0;
        uint8_t rkw = rel_id_of(ai, "关键词", 0);
        uint8_t rex = rel_id_of(ai, "排除", 0);
        uint8_t rdkw = (rkw >= REL_DYN_BASE) ? (uint8_t)(rkw - REL_DYN_BASE + 1) : 0;
        uint8_t rdex = (rex >= REL_DYN_BASE) ? (uint8_t)(rex - REL_DYN_BASE + 1) : 0;
        if (rdkw) for (size_t i = 0; i < ai->pool->count; ++i) {
            Token *t = ai->pool->tokens[i];
            if (!t || !t->name || strncmp(t->name, "配方:", 7) != 0) continue;
            int excluded = 0;
            if (rdex) for (size_t j = 0; j < t->count; ++j) {
                const AssocEntry *e = &token_assocs_ro(t)[j];
                if (e->main && e->dyn == rdex && e->main->name && strstr(nd, e->main->name)) {
                    excluded = 1; break;
                }
            }
            if (excluded) continue;
            int hits = 0;
            size_t longest = 0;
            for (size_t j = 0; j < t->count; ++j) {
                const AssocEntry *e = &token_assocs_ro(t)[j];
                if (!e->main || e->dyn != rdkw || !e->main->name) continue;
                if (strstr(nd, e->main->name)) {
                    hits++;
                    size_t l = strlen(e->main->name);
                    if (l > longest) longest = l;
                }
            }
            if (hits > rbest || (hits == rbest && hits > 0 && longest > rlong)) {
                recipe = t; rbest = hits; rlong = longest;
            }
        }
        if (recipe) {
            char aw2[256] = {0};
            if (code_gate_artifacts(ai, recipe, nd, aw2, sizeof aw2) > 0) {
                if (note && cap) snprintf(note, cap, "描述要 %s，配方 %s 里没有 ⇒ 不硬答", aw2, recipe->name);
                return -1;
            }
            char miss2[512] = {0};
            double cov2 = code_desc_cover(ai, recipe, nd, miss2, sizeof miss2);
            double minc2 = 0.0;
            const char *th2 = getenv("AIP_COVER_MIN");
            if (th2 && th2[0]) minc2 = atof(th2);
            if (getenv("AIP_CHAINDBG"))
                fprintf(stderr, "[gate:配方] %s 覆盖=%.2f 缺=[%s]\n", recipe->name, cov2, miss2);
            if (cov2 < minc2) {
                if (note && cap)
                    snprintf(note, cap, "配方 %s 只覆盖描述的一部分（缺 %s）⇒ 不硬答",
                             recipe->name, miss2[0] ? miss2 : "内容词");
                return -1;
            }
            int rrc = code_compose(ai, recipe, desc, note, cap, "配方");
            if (rrc != -3) return rrc;
        }
        if (note && cap) snprintf(note, cap, "没有匹配的任务/配方（描述里的关键词在图里一个都没命中）");
        return -1;
    }
    /* §6.816 窄口径需求词闸门（默认开）：命中单元的知识里没有“下标/之差/包含/交替”这类输出形态 ⇒ 拒绝 */
    {
        char aw[256] = {0};
        if (code_gate_artifacts(ai, best, nd, aw, sizeof aw) > 0) {
            if (note && cap) snprintf(note, cap, "描述要 %s，命中的知识里没有 ⇒ 不硬答", aw);
            return -1;
        }
    }
    /* §6.815 语义闸门：描述里的内容词在命中单元的知识里覆盖率太低 ⇒ 宁可拒绝 */
    {
        char miss[512] = {0};
        double cov = code_desc_cover(ai, best, nd, miss, sizeof miss);
        const char *th = getenv("AIP_COVER_MIN");
        double minc = th && th[0] ? atof(th) : 0.0;   /* §6.815 默认关：实测会误拒隐藏组，见文档 */
        if (getenv("AIP_CHAINDBG"))
            fprintf(stderr, "[gate] 单元=%s 覆盖=%.2f 缺=[%s]\n", best->name, cov, miss);
        if (cov < minc) {
            if (note && cap)
                snprintf(note, cap, "描述里有知识没覆盖的内容（%s 覆盖 %.0f%%）⇒ 不硬答",
                         miss[0] ? miss : "内容词", cov * 100.0);
            return -1;
        }
    }
    /* ①b 组合/配方：挂 模板/用/主体 的，交给同一个组合器（任务和配方走同一份） */
    {
        int crc = code_compose(ai, best, desc, note, cap, "组合");
        if (crc != -3) return crc;
    }
    /* ①b 多文件工程：任务 --文件--> 文件:X（每个 X 有 代码 边）⇒ 写多文件一起编 */
    {
        uint8_t rid_f = rel_id_of(ai, "文件", 0);
        if (rid_f >= REL_DYN_BASE) {
            uint8_t d_f = (uint8_t)(rid_f - REL_DYN_BASE + 1);
            const char *fn[8]; const char *fc[8]; int nf = 0;
            for (size_t j = 0; j < best->count && nf < 8; ++j) {
                const AssocEntry *e = &token_assocs_ro(best)[j];
                if (!e->main || e->dyn != d_f || !e->main->name) continue;
                const char *code = NULL;
                for (size_t k = 0; k < e->main->count; ++k) {
                    const AssocEntry *c = &token_assocs_ro(e->main)[k];
                    if (c->main && c->dyn == dyn_cd && c->main->name) { code = c->main->name; break; }
                }
                if (!code) continue;
                const char *fnm = e->main->name;
                if (!strncmp(fnm, "文件:", 7)) fnm += 7;   /* 文件名 = 符号名去掉 "文件:" */
                if (!fnm[0]) continue;
                fn[nf] = fnm; fc[nf] = code; nf++;
            }
            if (nf > 0) return code_emit_multi(ai, fn, fc, nf, best->name + 7, note, cap);
        }
    }
    /* ② 骨架 → 代码（两步都必须在图上**查得到**） */
    Token *skel = NULL;
    for (size_t j = 0; j < best->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(best)[j];
        if (e->main && e->dyn == dyn_sk) { skel = e->main; break; }
    }
    if (!skel) {
        if (note && cap) snprintf(note, cap, "任务 %s 命中，但图上没有它的 骨架 边", best->name);
        return -1;
    }
    const char *src = NULL;
    for (size_t j = 0; j < skel->count; ++j) {
        const AssocEntry *e = &token_assocs_ro(skel)[j];
        if (e->main && e->dyn == dyn_cd && e->main->name) { src = e->main->name; break; }
    }
    if (!src) {
        if (note && cap) snprintf(note, cap, "骨架 %s 命中，但图上没有它的 代码 边（消融时就是这样）", skel->name);
        return -1;
    }
    /* ③ 落地 + 闭环 */
    const char *nm = best->name + 7;              /* 去掉 "任务:" */
    char rel[256];
    snprintf(rel, sizeof rel, "synth_%s.c", nm);
    if (action_write_file(ai, rel, src) != 0) {
        if (note && cap) snprintf(note, cap, "写文件失败：%s", g_act.last_reason);
        return -2;
    }
    char nb[1024] = {0};         /* 要给 code_test_file 留够：它内部会拼 rel + 说明 */
    int rc = code_test_file(ai, rel, nb, sizeof nb);
    if (note && cap) {
        char head[192];
        snprintf(head, sizeof head, "命中任务 %s（%d 个关键词）→ 骨架 %s → ", best->name, best_hits, skel->name);
        snprintf(note, cap, "%s%.120s", head, nb);
    }
    return rc;
}

/* §6.807 **多文件工程**：任务 --文件--> 文件:X，每个 X 有 代码 边（一个文件的内容）。
 * 写多个文件、`gcc` 一起编、跑产物。为什么值得单开一条：单文件闭环只能验"一个翻译单元"，
 * 多文件才碰得到头文件/外部符号/链接错误这些真问题。 */
static inline int code_emit_multi(AI *ai, const char *const *names, const char *const *codes,
                                  int n, const char *outbase, char *note, size_t cap) {
    if (!ai || n <= 0 || !outbase) return -3;
    char outs[256];
    snprintf(outs, sizeof outs, "synth_%s.out", outbase);
    char cmd[1024];
    int cl = snprintf(cmd, sizeof cmd, "gcc -std=c11 -Wall -Wextra -o %s", outs);
    for (int i = 0; i < n; ++i) {
        if (action_write_file(ai, names[i], codes[i]) != 0) {
            if (note && cap) snprintf(note, cap, "写文件 %s 失败：%s", names[i], g_act.last_reason);
            return -2;
        }
        int k = snprintf(cmd + cl, sizeof cmd - (size_t)cl, " %s", names[i]);
        if (k < 0 || (size_t)k >= sizeof cmd - (size_t)cl) break;
        cl += k;
    }
    int crc = action_run(ai, cmd);
    if (crc != 0) {
        const char *o = action_book()->last_out;
        size_t ol = o ? strlen(o) : 0;
        if (ol > 240) o += ol - 240;
        if (note && cap) snprintf(note, cap, "%d 个文件：**没编过**（rc=%d）：%.160s", n, crc, o ? o : "");
        return -2;
    }
    int rc = action_run_product(ai, outs);
    size_t nok = 0, nbad = 0;
    code_count_asserts(action_book()->last_out, &nok, &nbad);
    if (note && cap) {
        if (rc == 0 && nbad == 0)
            snprintf(note, cap, "%d 个文件：编过、**测试全绿**（%s rc=0；断言 %zu ok / %zu FAIL）", n, outs, nok, nbad);
        else {
            const char *o = action_book()->last_out;
            size_t ol = o ? strlen(o) : 0;
            if (ol > 200) o += ol - 200;
            snprintf(note, cap, "%d 个文件：编过但**测试不过**（rc=%d；断言 %zu ok / %zu FAIL）：%.100s",
                     n, rc, nok, nbad, o ? o : "");
        }
    }
    return rc;
}
/* §6.710 **片段检索**（`::code frag [关键词]`）：把图里 `片段:*` 的知识查出来。
 *
 * 为什么要有：§6.710 给图里灌了几千个**真实函数体**（`tools/c2triples.sh` 抽 + 过编译门），
 * 但它们以前只有"被任务边点中"这一种用法 —— 人查不到、也没法复用。
 * 这个口**只读**：不改图、不写文件；关键词按**子串**比（片段名 + 签名），最多列 5 个。
 * 不带关键词 ⇒ 报总量（多少片段 / 多少带签名）。 */
static inline void code_frag_search(AI *ai, const char *kw) {
    if (!ai || !ai->pool) { printf("[片段] 没有脑子\n"); return; }
    uint8_t rid_cd = rel_id_of(ai, "代码", 0);
    if (rid_cd < REL_DYN_BASE) { printf("[片段] 图上没有 `代码` 这种关系 —— 没有片段知识\n"); return; }
    uint8_t d_cd = (uint8_t)(rid_cd - REL_DYN_BASE + 1);
    uint8_t rid_sig = rel_id_of(ai, "签名", 0);
    uint8_t d_sig = (rid_sig >= REL_DYN_BASE) ? (uint8_t)(rid_sig - REL_DYN_BASE + 1) : 0;
    size_t n_frag = 0, n_sig = 0, n_hit = 0;
    for (size_t i = 0; i < ai->pool->count; ++i) {
        Token *t = ai->pool->tokens[i];
        if (!t || !t->name || t->is_garbage || strncmp(t->name, "片段:", 7) != 0) continue;
        const char *code = NULL, *sig = NULL;
        for (size_t j = 0; j < t->count; ++j) {
            const AssocEntry *e = &token_assocs_ro(t)[j];
            if (!e->main || !e->main->name) continue;
            if (e->dyn == d_cd) code = e->main->name;
            else if (d_sig && e->dyn == d_sig) sig = e->main->name;
        }
        if (!code) continue;                       /* 没有函数体的不算片段（比如只登记了名字的） */
        n_frag++;
        if (sig) n_sig++;
        if (kw && kw[0] && !strstr(t->name, kw) && !(sig && strstr(sig, kw))) continue;
        n_hit++;
        if (n_hit > 5) continue;
        printf("[片段] %s\n", t->name);
        if (sig) printf("       签名：%.160s\n", sig);
        /* 代码只打前 3 行（够看清是什么）：全文用 `::code synth` 拼出来，或者直接看 triples */
        const char *p = code;
        size_t total = 1, shown = 0;
        for (const char *q = code; *q; ++q) if (*q == '\n') total++;
        while (p && *p && shown < 3) {
            const char *nl = strchr(p, '\n');
            size_t l = nl ? (size_t)(nl - p) : strlen(p);
            printf("       %.*s\n", (int)(l > 120 ? 120 : l), p);
            shown++;
            p = nl ? nl + 1 : NULL;
        }
        printf("       （共 %zu 行）\n", total);
    }
    if (kw && kw[0]) {
        if (n_hit == 0) printf("[片段] 没有匹配「%s」的片段（图里共 %zu 个）\n", kw, n_frag);
        else printf("[片段] 命中 %zu 个（列出前 %zu 个）；图里共 %zu 个片段，其中 %zu 个带签名\n",
                    n_hit, n_hit > 5 ? (size_t)5 : n_hit, n_frag, n_sig);
    } else {
        printf("[片段] 图里共 %zu 个片段（带签名 %zu 个）。查法：::code frag <关键词>\n", n_frag, n_sig);
    }
}

/* §6.645 **测试驱动**：编 + **跑**（`_t/work/<dir>/a.out`）。
 * 判据是**产物退出码**：0 = 绿；非 0 = 红（把输出尾巴带出来）。
 * 只跑 `.out` 产物（`action_run_product` 那条窄入口），不碰别的命令。 */
static inline int code_test_dir(AI *ai, const char *dir, char *note, size_t cap) {
    if (!ai || !dir || !dir[0]) return -2;
    int rc = code_build_dir(ai, dir, note, cap);
    if (rc != 0) return rc;
    char prog[ACTION_PATH_CAP + 32];
    snprintf(prog, sizeof prog, "%s/a.out", dir);
    int trc = action_run_product(ai, prog);
    size_t nok = 0, nbad = 0;
    code_count_asserts(action_book()->last_out, &nok, &nbad);
    int strict_red = (ai->code_strict && trc == 0 && nbad > 0);   /* §6.661 严格：FAIL 行也算红 */
    if (note && cap) {
        if (trc == 0 && !strict_red)
            snprintf(note, cap, "%s：编过、**测试全绿**（a.out rc=0；断言 %zu ok / %zu FAIL）",
                     dir, nok, nbad);
        else if (strict_red)
            snprintf(note, cap, "%s：产物 rc=0，但打了 %zu 条 FAIL（严格模式）⇒ **测试不过**",
                     dir, nbad);
        else {
            char tail[256] = {0};
            const char *o = action_book()->last_out;
            size_t ol = o ? strlen(o) : 0;
            if (ol > 200) o += ol - 200;
            snprintf(tail, sizeof tail, "%.200s", o ? o : "");
            snprintf(note, cap, "%s：编过但**测试不过**（rc=%d）：%s", dir, trc, tail);
        }
    }
    return trc;
}

static inline int code_round(AI *ai, const char *rel) {
    CodeRun *cr = &g_code;
    cr->attempts = 0; cr->first_rc = cr->final_rc = -1; cr->rule = -1; cr->rule_line = 0;
    cr->where[0] = cr->err[0] = cr->note[0] = cr->lex[0] = 0;
    snprintf(cr->path, sizeof cr->path, "%s", rel ? rel : "");
    if (!ai || !rel || !rel[0]) { snprintf(cr->note, sizeof cr->note, "没给源文件"); return -2; }
    if (!ai->act_on) {
        snprintf(cr->note, sizeof cr->note,
                 "外部执行层关着（::act on / AIP_ACT=1）—— 闭环不走它就没有'受审'可言");
        return -2;
    }
    char full[ACTION_PATH_CAP + 48];
    snprintf(full, sizeof full, "%s/%s", ACTION_WORK_DIR, rel);
    FILE *f = fopen(full, "rb");
    if (!f) { code_cat(cr->note, sizeof cr->note, "读不开 ", full, "（先 ::act write 或让它写出来）"); return -2; }
    char *src = (char *)malloc(CODE_SRC_CAP);
    if (!src) { fclose(f); snprintf(cr->note, sizeof cr->note, "内存不够"); return -2; }
    size_t got = fread(src, 1, CODE_SRC_CAP - 1, f);
    fclose(f);
    src[got] = 0;
    /* §6.641 **回滚用的原件**：在**任何修改之前**留一份（第一版放晚了：拷贝的是已经改过的 src，
     * 于是"还原"还原成了改后的样子 —— 实测文件里分号还在）。 */
    char *orig = (char *)malloc(CODE_SRC_CAP);
    if (orig) memcpy(orig, src, got + 1);

    /* 代码词法（`::ascii on` 才走）：源码先过一遍 **ASCII 逐字保留** 判据 ——
     * "我写出去的字节，词法层一个都没改" 是写代码这件事的前置条件（线 A ③）。
     * 判据只有一处实现：lex.h 的 lex_roundtrip()。默认关 → 这一行不算、输出逐字节同今天。 */
    if (ai->lex_ascii) {
        char why[160];
        if (lex_roundtrip(src, why, sizeof why))
            snprintf(cr->lex, sizeof cr->lex, "ASCII 逐字保留 ✓（canonical 幂等）");
        else
            snprintf(cr->lex, sizeof cr->lex, "ASCII 逐字保留 ✗：%s", why);
    }

    char cmd[ACTION_CMD_CAP];
    snprintf(cmd, sizeof cmd,
             "gcc -std=c11 -Wall -Werror=implicit-function-declaration -o %s%s %s",
             rel, CODE_OUT_NAME, rel);
    int rc = action_run(ai, cmd);
    cr->attempts = 1;
    cr->first_rc = rc;
    if (rc == 0) {
        snprintf(cr->note, sizeof cr->note, "一次就编过（%s%s 出来了）", rel, CODE_OUT_NAME);
        cr->final_rc = 0;
        (void)code_observe(ai, 0);
        free(src);
        return 0;
    }
    /* 看错：第一条 error + 位置 */
    int line = 0;
    code_first_error(action_book()->last_out, cr->where, sizeof cr->where,
                     cr->err, sizeof cr->err, &line);
    /* 改：按表认规则。规则①要**真的在说分号**，而且不能是"文件到头了"那种形状
     * （`expected declaration or statement at end of input` 也含 "expected"，
     *  但补分号救不了它 —— 认错形状就会瞎改，实测踩过）。 */
    const CodeFixRule *rule = code_rule_for(cr->err);   /* 判据在 helper 里（可单测） */
    if (!rule) {
        snprintf(cr->note, sizeof cr->note,
                 "没有已知的机械修法（降级版只认表里那三类错）—— 停在原地，不猜");
        cr->final_rc = rc;
        (void)code_observe(ai, rc);
        free(src);
        return rc;
    }
    cr->rule = (int)(rule - CODE_FIXES);
    int fixed = -1;
    if (cr->rule == 0) {
        /* 「expected … ;」→ 补在**报错那一行**；那一行本来就以 `;`/`{`/`}` 结尾时再试上一行。
         * ⚠ 原来固定补"上一行"：`printf("%d\n", x)` 缺分号时 gcc 报的是**下一行**的列
         * （`4:22: expected ‘;’ before ‘return’`），上一行是 `int x = 3;` —— 补不进去，
         * 于是最常见的错（缺分号）**一次都修不成功**（§6.698 实测）。 */
        int l0 = (line > 0) ? line : 1;
        fixed = code_fix_semicolon(src, CODE_SRC_CAP, l0);
        int used = l0;
        if (fixed != 0 && l0 > 1) { fixed = code_fix_semicolon(src, CODE_SRC_CAP, l0 - 1); used = l0 - 1; }
        if (fixed != 0) snprintf(cr->note, sizeof cr->note, "认出了「补分号」但第 %d / %d 行都补不进去", l0, l0 > 1 ? l0 - 1 : l0);
        else cr->rule_line = used;
    } else if (cr->rule == 2) {
        /* §6.97 第三条修法：**补声明**（在出错行前插 `int <名字>;`）—— 与另两条同构：
         * 只改 `src`，后面统一写回/再编；改不动就把 `fixed` 留成非 0，走"停在这"那条。 */
        fixed = code_fix_declare(src, CODE_SRC_CAP, cr->err, line > 1 ? line : 1);
        if (fixed != 0) snprintf(cr->note, sizeof cr->note, "认出了「补声明」但名字抠不出来 —— 停在这");
        else snprintf(cr->note, sizeof cr->note, "按规则补了声明（类型只能猜 `int`；错了下一轮再修）");
    } else {
        const char *hdr = code_header_for(cr->err);
        if (!hdr) snprintf(cr->note, sizeof cr->note, "认出了「补 #include」但函数名不在表里 —— 不猜");
        else {
            fixed = code_fix_include(src, CODE_SRC_CAP, hdr);
            if (fixed != 0) snprintf(cr->note, sizeof cr->note, "`#include <%s>` 已经有了或者加不进去", hdr);
            else snprintf(cr->note, sizeof cr->note, "按规则补了 `#include <%s>`", hdr);
        }
    }
    if (fixed != 0) {
        cr->final_rc = rc;
        (void)code_observe(ai, rc);
        free(src);
        return rc;
    }
    if (cr->rule == 0)
        snprintf(cr->note, sizeof cr->note, "按规则补了一个 `;`（第 %d 行）", cr->rule_line);
    /* 写回去（还是走受审的写文件通道）→ 再跑一次 */
    if (action_write_file(ai, rel, src) != 0) {
        code_cat(cr->note, sizeof cr->note, "改完写不回去：", action_book()->last_reason, NULL);
        cr->final_rc = rc;
        (void)code_observe(ai, rc);
        free(src);
        return rc;
    }
    int rc2 = action_run(ai, cmd);
    cr->attempts = 2;
    cr->final_rc = rc2;
    /* §6.641 **还编不过 ⇒ 回滚**（只在"不再多轮"的默认路径上做；多轮开着时交给多轮） */
    if (rc2 != 0 && !ai->code_multi && orig) {
        if (action_write_file(ai, rel, orig) == 0) {
            code_cat(cr->note, sizeof cr->note,
                     "；**改完还编不过 ⇒ 已把文件还原**（不给你留半坏的源码）", NULL, NULL);
        }
    }
    free(orig);
    /* **C③ 第二件：多轮自我修正**（默认关，`::code multi on` / `AIP_CODEMULTI=1`）：
     * 第二次还编不过时，**用新的错误再认一遍规则**、再改一处、再编一次（总次数 ≤ 3）。
     * 老行为 = 只改一次（默认关 ⇒ 逐字节相同）；这里只用多出来的那一轮，不改已有那一轮。 */
    if (rc2 != 0 && ai->code_multi && cr->attempts < 3) {
        char where2[CODE_ERR_CAP];
        int line2 = 0;
        code_first_error(action_book()->last_out, where2, sizeof where2, cr->err, sizeof cr->err, &line2);
        const CodeFixRule *r2 = code_rule_for(cr->err);
        if (r2) {
            int idx2 = (int)(r2 - CODE_FIXES);
            char *src2 = (char *)malloc(CODE_SRC_CAP);
            FILE *f2 = fopen(full, "rb");
            size_t got2 = 0;
            if (src2 && f2) { got2 = fread(src2, 1, CODE_SRC_CAP - 1, f2); }
            if (f2) fclose(f2);
            if (src2) {
                src2[got2] = 0;
                int fx2 = -1;
                if (idx2 == 0) fx2 = code_fix_semicolon(src2, CODE_SRC_CAP, line2 > 1 ? line2 - 1 : line2);
                else if (idx2 == 2) fx2 = code_fix_declare(src2, CODE_SRC_CAP, cr->err, line2 > 1 ? line2 : 1);
                else {
                    const char *h2 = code_header_for(cr->err);
                    if (h2) fx2 = code_fix_include(src2, CODE_SRC_CAP, h2);
                }
                if (fx2 == 0) {
                    FILE *w2 = fopen(full, "wb");
                    if (w2) { (void)fwrite(src2, 1, strlen(src2), w2); fclose(w2); }
                    cr->rule = idx2;
                    int rc3 = action_run(ai, cmd);
                    cr->attempts = 3;
                    cr->final_rc = rc3;
                    char keep3[CODE_ERR_CAP];
                    snprintf(keep3, sizeof keep3, "%s", cr->note);
                    code_cat(cr->note, sizeof cr->note, keep3,
                             rc3 == 0 ? " —— **第三轮改过之后编过了**（多轮修正生效）"
                                      : " —— 第三轮还是没过（够三轮了，停）", NULL);
                    if (rc3 == 0) (void)code_observe(ai, 0);
                }
                free(src2);
            }
        }
    }
    if (rc2 == 0) {
        char keep[CODE_ERR_CAP];
        snprintf(keep, sizeof keep, "%s", cr->note);
        code_cat(cr->note, sizeof cr->note, keep, " —— 再编**过了**", NULL);
    } else {
        char keep[CODE_ERR_CAP];
        snprintf(keep, sizeof keep, "%s", cr->note);
        code_cat(cr->note, sizeof cr->note, keep,
                 " —— 再编**还是没过**（降级版最多改一次，停在这里）", NULL);
    }
    (void)code_observe(ai, rc2);
    free(src);
    return rc2;
}

/* `::code` 的痕（就这一处拼） */
static inline void code_print(void) {
    CodeRun *cr = &g_code;
    if (cr->attempts == 0 && !cr->note[0]) {
        printf("[写代码闭环] 还没走过。用法：::code try <相对路径>（文件在 " ACTION_WORK_DIR "/ 里）\n");
        printf("  修法表（一行一条）：\n");
        for (size_t i = 0; i < CODE_FIX_N; ++i)
            printf("    %-22s %-10s %s\n", CODE_FIXES[i].err, CODE_FIXES[i].fix, CODE_FIXES[i].why);
        printf("  降级版边界：只这两类修法、最多改一次、**不生成代码**（源文件是外面写的）\n");
        return;
    }
    printf("[写代码闭环] %s：编译 %d 次，首次 rc=%d → 最终 rc=%d\n",
           cr->path[0] ? cr->path : "?", cr->attempts, cr->first_rc, cr->final_rc);
    if (cr->where[0]) printf("  第一条错误：%s：%s\n", cr->where, cr->err);
    if (cr->lex[0])   printf("  词法：%s\n", cr->lex);
    if (cr->note[0])  printf("  改了什么：%s\n", cr->note);
    if (cr->attempts >= 2 || cr->first_rc == 0)
        printf("  产物：%s/%s%s（在 " ACTION_WORK_DIR "/ 里）\n", ACTION_WORK_DIR, cr->path, CODE_OUT_NAME);
}

/* 代理循环那条路：`AIP_CODE=<相对路径>` 时**自己**走一遍（默认关；一个进程只走一次） */
static inline int code_auto_step(AI *ai) {
    if (g_code_auto_done) return 0;
    const char *rel = getenv("AIP_CODE");
    if (!rel || !rel[0]) return 0;
    g_code_auto_done = 1;
    (void)code_round(ai, rel);
    /* 把痕打出来：这条路是**测量/诊断钩子**（只在设了 AIP_CODE 时活），
     * 不打出来的话"跑过了"在日志里看不见（实测踩过：文件改了、产物有了、日志是空的）。 */
    printf("[代理循环自己走了一遍写代码闭环：AIP_CODE=%s]\n", rel);
    code_print();
    return 1;
}

#endif /* CODE_H */
