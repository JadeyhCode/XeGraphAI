/* xe_session.h — 自由图符号 = 按会话隔离的记忆。
 *
 * 每个会话一个锚点符号 自由图:<sid>，会话里的记忆都挂在它自己的边上；
 * 磁盘上每会话一个补丁文件 _sessions/<sid>.patch（只存增量，见 xe_patch.h），
 * 外加一份 _sessions/_base.fp 记初始图指纹（装载时校验，防止把补丁打到别的图上）。
 *
 * 隔离靠两点：
 *   1) 边挂在 自由图:<sid> 上，另一个会话的锚点是**另一个符号**，天然看不到；
 *   2) 每个会话只装载自己的补丁文件。
 */
#ifndef XE_SESSION_H
#define XE_SESSION_H

#include <sys/stat.h>
#include <sys/types.h>

#define XE_SESS_DIR "_sessions"

static inline const char *xe_sess_dir(void) {
    const char *d = getenv("XE_SESS_DIR");
    return (d && d[0]) ? d : XE_SESS_DIR;
}

static inline int xe_sess_mkdir(void) {
    const char *d = xe_sess_dir();
    char buf[256];
    snprintf(buf, sizeof buf, "%s", d);
    for (char *p = buf + 1; *p; ++p) {
        if (*p == '/') { *p = 0; mkdir(buf, 0777); *p = '/'; }
    }
    if (mkdir(buf, 0777) != 0) {
        struct stat st;
        if (stat(buf, &st) != 0 || !S_ISDIR(st.st_mode)) return -1;
    }
    return 0;
}

static inline void xe_sess_path(const char *sid, char *out, size_t cap) {
    if (!sid || !sid[0]) sid = "default";
    snprintf(out, cap, "%s/%s.patch", xe_sess_dir(), sid);
}

static inline void xe_sess_base_path(char *out, size_t cap) {
    snprintf(out, cap, "%s/_base.fp", xe_sess_dir());
}

/* 记下初始图指纹（只在缺失时写）*/
static inline void xe_sess_base_ensure(AI *ai) {
    char path[256], fp[33];
    xe_sess_mkdir();
    xe_sess_base_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); return; }
    xe_graph_fp(ai, fp, sizeof fp);
    f = fopen(path, "w");
    if (!f) return;
    fprintf(f, "%s\n", fp);
    fclose(f);
}

static inline void xe_sess_base_read(char *out, size_t cap) {
    char path[256];
    out[0] = 0;
    xe_sess_base_path(path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return;
    if (fgets(out, (int)cap, f)) {
        size_t l = strlen(out);
        while (l && (out[l - 1] == '\n' || out[l - 1] == '\r')) out[--l] = 0;
    }
    fclose(f);
}

/* 装载某会话的补丁（没有文件就当空记忆）。返回 0 成功 / -2 指纹不符 / -1 失败 */
static inline int xe_sess_load(AI *ai, const char *sid) {
    char path[256], base[33];
    xe_sess_path(sid, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    fclose(f);
    XePatch p;
    if (xe_patch_load(&p, path) != 0) return -1;
    /* 校验的是"补丁是给哪张初始图打的"——拿 _base.fp 比，**不拿活图比**：
     * 活图在装过别的会话之后必然变了（多了 自由图:* 的边），拿它比会把自家补丁拒掉。 */
    xe_sess_base_read(base, sizeof base);
    if (base[0] && p.base_fp[0] && strcmp(base, p.base_fp) != 0) { xe_patch_free(&p); return -2; }
    int rc = xe_patch_apply(ai, &p, 1);
    xe_patch_free(&p);
    return rc;
}

/* 会话记一条：先确保锚点，改图，同时把增量追加进该会话补丁文件 */
static inline int xe_sess_remember(AI *ai, const char *sid, const char *rel, const char *obj) {
    if (!ai || !sid || !sid[0] || !rel || !obj) return -1;
    char path[256], base[33];
    xe_sess_mkdir();
    xe_sess_base_ensure(ai);
    xe_sess_base_read(base, sizeof base);

    XePatch p;
    xe_sess_path(sid, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); if (xe_patch_load(&p, path) != 0) return -1; }
    else xe_patch_init(&p, sid, base);
    if (!p.base_fp[0]) snprintf(p.base_fp, sizeof p.base_fp, "%s", base);

    char from[96];
    snprintf(from, sizeof from, "自由图:%s", sid);
    int rc = xe_patch_record_edge(&p, ai, from, rel, obj, 1);
    if (rc == 0) xe_patch_save(&p, path);
    xe_patch_free(&p);
    return rc;
}

static inline int xe_sess_forget(AI *ai, const char *sid, const char *rel, const char *obj) {
    if (!ai || !sid || !rel || !obj) return -1;
    char path[256], base[33], from[96];
    xe_sess_mkdir();
    xe_sess_base_ensure(ai);
    xe_sess_base_read(base, sizeof base);
    XePatch p;
    xe_sess_path(sid, path, sizeof path);
    FILE *f = fopen(path, "r");
    if (f) { fclose(f); if (xe_patch_load(&p, path) != 0) return -1; }
    else xe_patch_init(&p, sid, base);
    snprintf(from, sizeof from, "自由图:%s", sid);
    int rc = xe_patch_record_edge(&p, ai, from, rel, obj, 0);
    if (rc == 0) xe_patch_save(&p, path);
    xe_patch_free(&p);
    return rc;
}

/* 回忆：该会话在某个关系下的所有宾语（返回条数，名字写进 out）*/
static inline int xe_sess_recall(AI *ai, const char *sid, const char *rel,
                                 char out[][96], int max) {
    Token *s = xe_session(ai, sid);
    uint8_t d = xe_rid(ai, rel);
    if (!s || !d) return 0;
    int n = 0;
    for (size_t i = 0; i < s->count && n < max; ++i) {
        const AssocEntry *e = &token_assocs_ro(s)[i];
        if (!e->main || !e->main->name || !xe_edge_is(e, d)) continue;
        snprintf(out[n], 96, "%s", e->main->name);
        n++;
    }
    return n;
}

#endif /* XE_SESSION_H */
