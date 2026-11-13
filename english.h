/* english.h — **英语问 ⇒ 英语答**（"读"已经由 `[syn]` 数据解决，这里只补"写"）
 *
 * 分工：
 *   · 读：`patterns.txt` 的 `[syn] cat=猫` 把英文词换成中文节点（数据，不改代码）；
 *   · 写：本文件把**关系是非问**的答复换成英文（Yes/No/I don't know + 一句陈述）。
 *
 * 为什么只做"是非问"：这个内核不生成文本，能说的只有**图里查得到的结论**。
 * 与其硬造自由英语，不如把"问得清、答得准"的那一类做对：`Is a cat an animal?`
 *
 * 支持的句式（够用就好，认不出就**不动**原答复 —— 绝不猜）：
 *   Is <A> a/an <B>?            ⇒ 属于
 *   <A> is a/an <B>?            ⇒ 属于
 *   Can <A> <动词短语>?          ⇒ 会
 *   Does <A> eat/have/like/need <B>?  ⇒ 吃/有/喜欢/需要
 *   Does <A> live in <B>?       ⇒ 在
 *
 * 判据：`tests/english.sh` 第二段（英文问 → 英文答；否定与"不知道"都要说清）。
 */
#ifndef ENGLISH_H
#define ENGLISH_H

#include <string.h>
#include <stdio.h>

/* 只含 ASCII 可打印字符才算"英语问"（中文一个字都不许混进来） */
static inline int en_ascii_only(const char *s) {
    if (!s || !*s) return 0;
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
        if (*p >= 0x80) return 0;
    return 1;
}

static inline int en_word_eq(const char *w, size_t n, const char *lit) {
    size_t l = strlen(lit);
    if (n != l) return 0;
    for (size_t i = 0; i < n; ++i) {
        char a = w[i], b = lit[i];
        if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
        if (a != b) return 0;
    }
    return 1;
}

/* 英文主语/宾语的"冠词剥掉 + 首字母大写"（只用于输出，不改判据） */
static inline void en_pretty(const char *in, char *out, size_t cap, int cap_first) {
    if (!out || !cap) return;
    const char *p = in;
    while (*p == ' ') p++;
    const char *arts[] = {"a ", "an ", "the ", NULL};
    for (size_t i = 0; arts[i]; ++i) {
        size_t l = strlen(arts[i]);
        if (strncmp(p, arts[i], l) == 0) { p += l; break; }
        /* 句首大写也要剥（`A cat` / `The sea`）*/
        if ((p[0] == 'A' || p[0] == 'T') && strncasecmp(p, arts[i], l) == 0) { p += l; break; }
    }
    size_t n = 0;
    for (; p && *p && n + 1 < cap; ++p) out[n++] = *p;
    while (n && out[n - 1] == ' ') n--;
    out[n] = 0;
    if (cap_first && n) {
        if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');
    }
}

static inline int en_starts_vowel(const char *s) {
    char c = s ? s[0] : 0;
    if (c >= 'A' && c <= 'Z') c = (char)(c - 'A' + 'a');
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

/* 关系 → 英文短语（输出用）。返回 1 = 有对应说法。 */
static inline int en_phrase(uint8_t rid, const char *obj, char *out, size_t cap) {
    switch (rid) {
    case REL_IS_A: snprintf(out, cap, "%s", en_starts_vowel(obj) ? "is an" : "is a"); return 1;
    case REL_CAN:  snprintf(out, cap, "can"); return 1;
    case REL_HAS:  snprintf(out, cap, "has"); return 1;
    case REL_FEAR: snprintf(out, cap, "fears"); return 1;
    case REL_LIKE: snprintf(out, cap, "likes"); return 1;
    case REL_EAT:  snprintf(out, cap, "eats"); return 1;
    case REL_NEED: snprintf(out, cap, "needs"); return 1;
    case REL_AT:   snprintf(out, cap, "is in"); return 1;
    default: return 0;
    }
}

/* 把 words[from..to) 拼起来（不剥冠词；剥冠词交给 en_pretty） */
static inline void en_join(const char **w, const size_t *wl, int from, int to, char *out, size_t cap) {
    size_t o = 0;
    for (int z = from; z < to && o + 1 < cap; ++z) {
        if (o) out[o++] = ' ';
        for (size_t i = 0; i < wl[z] && o + 1 < cap; ++i) out[o++] = w[z][i];
    }
    out[o] = 0;
}

static inline int en_is(const char **w, const size_t *wl, int i, const char *lit) {
    return i >= 0 && en_word_eq(w[i], wl[i], lit);
}

/* 解析英语是非问。成功返回 1，并给出 A / B / 关系。 */
static inline int en_parse(AI *ai, const char *q, char *A, size_t acap, char *B, size_t bcap, uint8_t *rid) {
    if (!q || !A || !B || !rid || !en_ascii_only(q) || !ai) return 0;
    char buf[512];
    size_t n = 0;
    for (const char *p = q; *p && n + 1 < sizeof buf; ++p)
        buf[n++] = (*p == '?' || *p == '"' || *p == '.') ? ' ' : *p;
    buf[n] = 0;
    const char *w[64];
    size_t wl[64];
    int nw = 0;
    char *p = buf;
    while (*p && nw < 64) {
        while (*p == ' ' || *p == '\t') p++;
        if (!*p) break;
        char *st = p;
        while (*p && *p != ' ' && *p != '\t') p++;
        w[nw] = st;
        wl[nw] = (size_t)(p - st);
        nw++;
    }
    if (nw < 3) return 0;
    /* 带疑问词的**不是是非问**（`What is a cat?`）：直接不认 ⇒ 交给 english_hint 给英文指引 */
    {
        const char *qw[] = {"what", "which", "who", "whom", "whose", "where",
                            "when", "why", "how", "how many", NULL};
        for (size_t i = 0; qw[i]; ++i)
            if (en_is(w, wl, 0, qw[i])) return 0;
    }

    if (en_is(w, wl, 0, "does") || en_is(w, wl, 0, "do")) {
        /* 动词 = 句子里**第一个绑到核心关系**的词（绑定在 `[alias]` 数据里，不在这里写死） */
        uint8_t r = 0;
        int v = -1;
        for (int i = 1; i < nw - 1; ++i) {
            char wd[32];
            size_t l = wl[i] < sizeof wd - 1 ? wl[i] : sizeof wd - 1;
            memcpy(wd, w[i], l);
            wd[l] = 0;
            uint8_t d = rel_id_of(ai, wd, 0);
            if (getenv("AIP_ENDBG")) fprintf(stderr, "[en] does-scan i=%d w=%s rel=%u alias=%u\n", i, wd, (unsigned)d, (unsigned)rel_alias_id(wd));
            /* 名词（a/an/the/cat…）也可能被登记成**动态关系**（英文问句被中文流水线当陈述学过），
             * 所以这里只认**核心关系**（`[alias]` 绑的那几个），动态 id 一律跳过。 */
            if (d && d < REL_DYN_BASE && d != REL_IS_A) { r = d; v = i; break; }
        }
        if (v < 0) return 0;
        en_join(w, wl, 1, v, A, acap);
        en_join(w, wl, v + 1, nw, B, bcap);
        if (!A[0] || !B[0]) return 0;
        *rid = r;
        return 1;
    }
    if (en_is(w, wl, 0, "can")) {
        /* Can <主语> <动词短语>? —— 动词短语取最后一段（多词短语也接得住） */
        if (nw < 4) return 0;
        uint8_t d = rel_id_of(ai, "can", 0);
        if (!d) return 0;                     /* `[alias] can=会` 没配 ⇒ 不认（可消融） */
        en_join(w, wl, 1, nw - 1, A, acap);
        en_join(w, wl, nw - 1, nw, B, bcap);
        if (!A[0] || !B[0]) return 0;
        *rid = d;
        return 1;
    }
    if (en_is(w, wl, 0, "is") || en_is(w, wl, 0, "are")) {
        /* Is <主语> a/an <宾语>? —— 取**最后**一个 a/an 当宾语冠词 */
        int k = -1;
        for (int i = nw - 1; i >= 1; --i)
            if (en_is(w, wl, i, "a") || en_is(w, wl, i, "an")) { k = i; break; }
        if (k < 1) return 0;
        uint8_t d = rel_id_of(ai, en_is(w, wl, 0, "are") ? "are" : "is", 0);
        if (!d) return 0;
        en_join(w, wl, 1, k, A, acap);
        en_join(w, wl, k + 1, nw, B, bcap);
        if (!A[0] || !B[0]) return 0;
        *rid = d;
        return 1;
    }
    /* <主语> is a/an <宾语>?（陈述语序）*/
    {
        int k = -1;
        for (int i = 1; i < nw; ++i)
            if (en_is(w, wl, i, "is") || en_is(w, wl, i, "are")) { k = i; break; }
        if (k < 1) return 0;
        en_join(w, wl, 0, k, A, acap);
        int st = k + 1;
        if (st < nw && (en_is(w, wl, st, "a") || en_is(w, wl, st, "an") || en_is(w, wl, st, "the"))) st++;
        uint8_t d = rel_id_of(ai, en_is(w, wl, k, "are") ? "are" : "is", 0);
        if (!d) return 0;
        en_join(w, wl, st, nw, B, bcap);
        if (!A[0] || !B[0]) return 0;
        *rid = d;
        return 1;
    }
}

/* 把中文答复的**极性**读出来：+1 真 / -1 假 / 0 不知道。只看开头（答复模板稳定）。 */
static inline int en_truth_of_answer(const char *a) {
    if (!a) return 0;
    while (*a == ' ') a++;
    if (strncmp(a, "不 对", 9) == 0) return -1;          /* 「不 对」= 断言为假 */
    if (strncmp(a, "对", 3) == 0) return 1;
    return 0;                                            /* 不知道/大概/不太懂 一律当"不知道" */
}

/* 英语词 → 中文节点（走 `[syn]` 那张数据表，和"读"用的是同一条路） */
static inline const Token *en_to_token(AI *ai, const char *w) {
    if (!ai || !w || !*w) return NULL;
    /* ① 剥冠词（a/an/the，含句首大写）—— `a cat` 要当 `cat` 查 */
    const char *p = w;
    while (*p == ' ') p++;
    const char *arts[] = {"a ", "an ", "the ", NULL};
    for (size_t i = 0; arts[i]; ++i) {
        size_t l = strlen(arts[i]);
        if (strncasecmp(p, arts[i], l) == 0) { p += l; break; }
    }
    /* ② 走 `[syn]`（和"读"同一张数据表） */
    char zh[96];
    if (!aip_apply_syn(ai, p, zh, sizeof zh))
        snprintf(zh, sizeof zh, "%.*s", (int)sizeof zh - 1, p);
    const Token *t = pool_find(ai->pool, zh);
    if (t) return t;
    /* ③ 多词短语：逐词试（`the red car` 里认 `car`） */
    char tmp[96];
    size_t n = 0;
    for (const char *q = p; ; ++q) {
        if (*q == ' ' || *q == 0) {
            if (n) {
                tmp[n] = 0;
                char z2[96];
                if (!aip_apply_syn(ai, tmp, z2, sizeof z2))
                    snprintf(z2, sizeof z2, "%s", tmp);
                if ((t = pool_find(ai->pool, z2)) != NULL) return t;
            }
            n = 0;
            if (!*q) break;
        } else if (n + 1 < sizeof tmp) {
            tmp[n++] = *q;
        }
    }
    return NULL;
}

/* 英语问 ⇒ 英语答：成功返回 1（out 被改写），否则 0（out 原样不动）。
 * 判据是 `rel_solve`（不是嗅中文答复的字符串）—— 英文和中文走的是**同一条推理**。 */
static inline int english_rewrite(AI *ai, const char *q, char *out, size_t cap) {
    char A[128], B[200];
    uint8_t rid = 0;
    if (!en_parse(ai, q, A, sizeof A, B, sizeof B, &rid)) return 0;
    if (!out || !cap) return 0;
    const Token *ta = en_to_token(ai, A);
    const Token *tb = en_to_token(ai, B);
    int truth = 0;
    if (ta && tb && rid) {
        Proof pf;
        rel_solve(ai, ta, rid, tb, 0, &pf);
        truth = pf.status;            /* +1 真 / -1 假 / 0 不知道 */
        if (truth == 0) {             /* 肯定事实没有 ⇒ 再问一次否定（`鱼 不 会 飞` 这种） */
            rel_solve(ai, ta, rid, tb, 1, &pf);
            if (pf.status != 0) truth = -1;   /* 推得出"不 R" ⇒ 这句话是假的 */
        }
    }
    char a[128], b[200], ph[24];
    en_pretty(A, a, sizeof a, 1);
    en_pretty(B, b, sizeof b, 0);
    if (!en_phrase(rid, b, ph, sizeof ph)) return 0;
    char res[420];
    if (truth > 0) {
        snprintf(res, sizeof res, "Yes. %s %s %s.", a, ph, b);
    } else if (truth < 0) {
        if (rid == REL_IS_A)
            snprintf(res, sizeof res, "No. %s is not %s %s.", a, en_starts_vowel(b) ? "an" : "a", b);
        else if (rid == REL_CAN)
            snprintf(res, sizeof res, "No. %s cannot %s.", a, b);   /* 情态动词的否定是 cannot，不是 "does not can" */
        else
            snprintf(res, sizeof res, "No. %s does not %s %s.", a, ph, b);
    } else {
        snprintf(res, sizeof res, "I don't know whether %s %s %s.", a, ph, b);
    }
    snprintf(out, cap, "%s", res);
    return 1;
}

/* 英语问但**不是是非问**（`What is a cat?`）：别掉中文兜底，给一句英文指引。
 * 只在原答复是"兜底"时才替换 —— 中文能答出来就不动（宁可给答案，不给模板）。 */
static inline int english_hint(AI *ai, const char *q, char *out, size_t cap) {
    if (!en_ascii_only(q) || !out || !cap) return 0;
    int alpha = 0, qmark = 0, digit = 0;
    for (const char *p = q; *p; ++p) {
        if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z')) alpha = 1;
        else if (*p == '?') qmark = 1;
        else if (*p >= '0' && *p <= '9') digit = 1;
    }
    if (!alpha || !qmark || digit) return 0;
    char A[128], B[200];
    uint8_t rid = 0;
    if (en_parse(ai, q, A, sizeof A, B, sizeof B, &rid)) return 0;   /* 是非问：上面已经答了 */
    /* 纯英文问句不是是非问 ⇒ 一律给英文指引：中文流水线对英文开放问句只会瞎答
     * （实测 `What is a cat?` 被检索答成"我也想养一只"），那比明说"我只答是非问"更糟。 */
    snprintf(out, cap,
             "Sorry, I only answer yes/no questions about what is in my graph. "
             "Try: \"Is a cat an animal?\" or \"Can a bird fly?\"");
    return 1;
}

#endif /* ENGLISH_H */
