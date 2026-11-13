/* model_io_v16.h — 模型存档 v16：**边打包 + 变长编码**（阶段四 M2-3）。
 *
 * 为什么改（M3 目标：1.1M 边时载入 ≤2 s / 存盘 ≤2 s / RSS ≤1 GB）：
 *   v15 每条边**定长** 58 B（含 hits/seq/两个 tick、16 B 时刻集合、role/topic/pattern
 *   各 1 B），其中绝大多数是 0；1.1M 边仅边段就 64 MB，写盘要逐字段调 22M 次 mw_*。
 *
 * v16 做三件事：
 *   1) **变长整数**（LEB128）：id/hits/tick 这类值多数很小，1–3 B 就够；
 *   2) **默认值省略**：每条边一个 flags 字，只写非默认字段；
 *   3) **边独立成段**：token 段只写 n_edges，边段统一放在 token 段之后
 *      ⇒ 读盘**单趟**（v15 要扫两趟：第一趟建词、第二趟回填 id）。
 *
 * 兼容：v15 及更早的读盘路径原样保留（出厂 model.bin 是 v15）。
 */
#ifndef MODEL_IO_V16_H
#define MODEL_IO_V16_H

/* ---- LEB128 ---- */
static inline void mw_uv(MWriter *w, uint64_t v) {
    uint8_t b[10];
    size_t n = 0;
    do {
        uint8_t byte = (uint8_t)(v & 0x7fu);
        v >>= 7;
        if (v) byte |= 0x80u;        /* 还有后续字节 ⇒ 打继续位（少了这一位 reader 会提前停）*/
        b[n++] = byte;
    } while (v && n < 10);
    mw_raw(w, b, n);
}

static inline uint64_t mr_uv(MReader *r) {
    uint64_t v = 0;
    int shift = 0;
    for (int i = 0; i < 10; ++i) {
        uint8_t c = mr_u8(r);
        if (r->err) return 0;
        v |= (uint64_t)(c & 0x7f) << shift;
        if (!(c & 0x80)) return v;
        shift += 7;
    }
    r->err = 1;
    return 0;
}

/* ---- 边 ---- */
#define MIO16_E_ACTION 0x0001u
#define MIO16_E_HITS   0x0002u
#define MIO16_E_SEQ    0x0004u
#define MIO16_E_ROLE   0x0008u
#define MIO16_E_TOPIC  0x0010u
#define MIO16_E_PAT    0x0020u
#define MIO16_E_REL    0x0040u
#define MIO16_E_DYN    0x0080u
#define MIO16_E_TSET   0x0100u
#define MIO16_E_BIRTH  0x0200u
#define MIO16_E_LAST   0x0400u
#define MIO16_E_MEM    0x0800u

/* 栈上小缓冲：一条边先拼成连续字节再一次性 mw_raw。
 * 为什么要它：1.1M 边 × 每条 6–8 次 mw_*（每次带分支 + memcpy）实测存盘 3.5 s，
 * 拼一次只掉一个 memcpy。缓冲 64 B 足够：最大情形（两个 id + 4 个时刻）也 < 40 B。 */
static inline uint8_t *mio16_put_uv(uint8_t *p, uint64_t v) {
    do {
        uint8_t byte = (uint8_t)(v & 0x7fu);
        v >>= 7;
        if (v) byte |= 0x80u;
        *p++ = byte;
    } while (v);
    return p;
}

static inline void mio16_w_edge(MWriter *w, const AssocEntry *e) {
    uint8_t buf[64];
    uint8_t *p = buf;
    uint16_t f = 0;
    if (e->action) f |= MIO16_E_ACTION;
    if (e->hits) f |= MIO16_E_HITS;
    if (e->seq) f |= MIO16_E_SEQ;
    if (e->role != ASSOC_ROLE_NONE) f |= MIO16_E_ROLE;
    if (e->topic != ASSOC_TOPIC_NONE) f |= MIO16_E_TOPIC;
    if (e->pattern != ASSOC_PAT_NONE) f |= MIO16_E_PAT;
    if (e->rel) f |= MIO16_E_REL;
    if (e->nrel || e->dyn) f |= MIO16_E_DYN;
    if (e->n_tset) f |= MIO16_E_TSET;
    if (e->birth_tick) f |= MIO16_E_BIRTH;
    if (e->last_touch_tick) f |= MIO16_E_LAST;
    if (e->mem_state != MEM_ACTIVE) f |= MIO16_E_MEM;
    *p++ = (uint8_t)f; *p++ = (uint8_t)(f >> 8);
    p = mio16_put_uv(p, e->main ? e->main->id : TOKEN_ID_NONE);
    if (f & MIO16_E_ACTION) p = mio16_put_uv(p, e->action->id);
    if (f & MIO16_E_HITS)   p = mio16_put_uv(p, e->hits);
    if (f & MIO16_E_SEQ)    p = mio16_put_uv(p, e->seq);
    if (f & MIO16_E_ROLE)   *p++ = e->role;
    if (f & MIO16_E_TOPIC)  *p++ = e->topic;
    if (f & MIO16_E_PAT)    *p++ = e->pattern;
    if (f & MIO16_E_REL)    p = mio16_put_uv(p, e->rel);
    if (f & MIO16_E_DYN)  { p = mio16_put_uv(p, e->nrel); *p++ = e->dyn; }
    if (f & MIO16_E_TSET) {
        uint8_t nt = (e->n_tset <= 4) ? e->n_tset : 0;
        *p++ = nt;
        for (uint8_t k = 0; k < nt; ++k) p = mio16_put_uv(p, e->tset[k]);
    }
    if (f & MIO16_E_BIRTH) p = mio16_put_uv(p, e->birth_tick);
    if (f & MIO16_E_LAST)  p = mio16_put_uv(p, e->last_touch_tick);
    if (f & MIO16_E_MEM)   *p++ = e->mem_state;
    mw_raw(w, buf, (size_t)(p - buf));
}

static inline int mio16_r_edge(MReader *r, AssocEntry *e) {
    uint16_t f = mr_u16(r);
    if (r->err) return -1;
    e->main = (Token *)(uintptr_t)mr_uv(r);   /* 先存 id，第二趟换指针 */
    if (r->err) return -1;
    e->action = NULL; e->hits = 0; e->seq = 0;
    e->role = ASSOC_ROLE_NONE; e->topic = ASSOC_TOPIC_NONE; e->pattern = ASSOC_PAT_NONE;
    e->rel = 0; e->nrel = 0; e->dyn = 0; e->n_tset = 0;
    e->birth_tick = 0; e->last_touch_tick = 0; e->mem_state = MEM_ACTIVE;
    if (f & MIO16_E_ACTION) e->action = (Token *)(uintptr_t)mr_uv(r);
    if (f & MIO16_E_HITS)   e->hits = (uint32_t)mr_uv(r);
    if (f & MIO16_E_SEQ)    e->seq = (uint32_t)mr_uv(r);
    if (f & MIO16_E_ROLE)   e->role = mr_u8(r);
    if (f & MIO16_E_TOPIC)  e->topic = mr_u8(r);
    if (f & MIO16_E_PAT)    e->pattern = mr_u8(r);
    if (f & MIO16_E_REL)    e->rel = (uint16_t)mr_uv(r);
    if (f & MIO16_E_DYN)  { e->nrel = (uint16_t)mr_uv(r); e->dyn = mr_u8(r); }
    if (f & MIO16_E_TSET) {
        e->n_tset = mr_u8(r);
        if (r->err || e->n_tset > 4) return -1;
        for (uint8_t k = 0; k < e->n_tset; ++k) e->tset[k] = (uint32_t)mr_uv(r);
    }
    if (f & MIO16_E_BIRTH) e->birth_tick = mr_uv(r);
    if (f & MIO16_E_LAST)  e->last_touch_tick = mr_uv(r);
    if (f & MIO16_E_MEM)   e->mem_state = mr_u8(r);
    return r->err ? -1 : 0;
}

/* ---- 词 ---- */
#define MIO16_T_MISSES 0x0001u
#define MIO16_T_RECENT 0x0002u
#define MIO16_T_PARENT 0x0004u
#define MIO16_T_NEXT   0x0008u
#define MIO16_T_PERSP  0x0010u
#define MIO16_T_CAT    0x0020u
#define MIO16_T_GARB   0x0040u
#define MIO16_T_MEM    0x0080u
#define MIO16_T_BIRTH  0x0100u
#define MIO16_T_LAST   0x0200u

static inline void mio16_w_token(MWriter *w, const Token *t, int is_temp, uint32_t n_edges) {
    uint16_t f = 0;
    if (t->misses) f |= MIO16_T_MISSES;
    if (t->recent_misses) f |= MIO16_T_RECENT;
    if (t->parent) f |= MIO16_T_PARENT;
    if (t->next_in_time) f |= MIO16_T_NEXT;
    if (t->perspective_of) f |= MIO16_T_PERSP;
    if (t->is_category) f |= MIO16_T_CAT;
    if (is_temp || t->is_garbage) f |= MIO16_T_GARB;
    if (t->mem_state != MEM_ACTIVE) f |= MIO16_T_MEM;
    if (t->birth_tick) f |= MIO16_T_BIRTH;
    if (t->last_touch_tick) f |= MIO16_T_LAST;
    mw_uv(w, t->id);
    uint32_t nlen = t->name ? (uint32_t)strlen(t->name) : 0;
    mw_uv(w, nlen);
    if (nlen) mw_raw(w, t->name, nlen);
    /* 剩下的一串（flags + 可选字段 + 边数）先拼进栈缓冲，一次写出：
     * 65 万词 × 8 次 mw_* 的调用开销实测占存盘约 0.3 s。 */
    uint8_t buf[64];
    uint8_t *p = buf;
    *p++ = (uint8_t)f; *p++ = (uint8_t)(f >> 8);
    if (f & MIO16_T_MISSES) p = mio16_put_uv(p, t->misses);
    if (f & MIO16_T_RECENT) p = mio16_put_uv(p, t->recent_misses);
    if (f & MIO16_T_PARENT) p = mio16_put_uv(p, t->parent->id);
    if (f & MIO16_T_NEXT)   p = mio16_put_uv(p, t->next_in_time->id);
    if (f & MIO16_T_PERSP)  p = mio16_put_uv(p, t->perspective_of->id);
    if (f & MIO16_T_CAT)    *p++ = t->is_category;
    if (f & MIO16_T_GARB)   *p++ = (uint8_t)(is_temp ? 1 : t->is_garbage);
    if (f & MIO16_T_MEM)    *p++ = t->mem_state;
    if (f & MIO16_T_BIRTH)  p = mio16_put_uv(p, t->birth_tick);
    if (f & MIO16_T_LAST)   p = mio16_put_uv(p, t->last_touch_tick);
    p = mio16_put_uv(p, n_edges);
    mw_raw(w, buf, (size_t)(p - buf));
}

#endif /* MODEL_IO_V16_H */
