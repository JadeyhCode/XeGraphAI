/* tool.h — **把本地大模型当外部工具**（默认关）：`::tool ask <问题>`
 *
 * 为什么要它，边界在哪：
 *   · 这个内核**不生成文本**（没有神经解码），所以"不会答"是常态。
 *   · 用户要的是"拿工具问本地部署的 AI"—— 那就把它当**工具**，不是当大脑：
 *     问回来的话**只显示、只记账、绝不进图**（不进事实层、不进说法层）。
 *   · 每一条答复都带出处 `[外部工具 <模型名>]`，并且**原样**给用户看（不改写、不摘要）。
 *   · 记一行账到 `tool.log`（时间 / 端点 / 模型 / 问题 / 字节 / 成败）——
 *     这是"我到底问了谁、它说了什么"的唯一凭据（和 net.h 的 _net.log 一个纪律）。
 *
 * 纪律（和 net.h 一样窄）：
 *   ① 默认**关**：不开 `::tool on`（或 `AIP_TOOL=1`）就一次都不往外走；
 *   ② 不用 shell：`fork` + `execv` + argv 数组；
 *   ③ 只认 http/https，**只该填本机地址**（默认空；`::tool set` 自己填）；
 *   ④ 硬上限：单次 ≤ 60 s / ≤ 1 MB 回包；到点 SIGKILL；
 *   ⑤ 失败就照实说（"工具没答上来：…"），不编、不重试。
 */
#ifndef TOOL_H
#define TOOL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>
#include <time.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <sys/types.h>

#define TOOL_URL_LEN   256
#define TOOL_KEY_LEN   128
#define TOOL_MODEL_LEN 64
#define TOOL_MAX_BYTES (1u * 1024u * 1024u)
#define TOOL_TIMEOUT_S 60
#define TOOL_CFG       "tool.txt"
#define TOOL_LOG       "tool.log"

typedef struct {
    int  on;                       /* 默认关 */
    char url[TOOL_URL_LEN];
    char key[TOOL_KEY_LEN];
    char model[TOOL_MODEL_LEN];
    unsigned long n_ask, n_ok, n_fail;
} ToolCfg;

static inline long tool_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

/* 读 `tool.txt`（`url=…` / `key=…` / `model=…` / `on=0|1`，`#` 注释）。缺文件就空配置。 */
static inline void tool_load(ToolCfg *c) {
    if (!c) return;
    memset(c, 0, sizeof *c);
    const char *e = getenv("AIP_TOOL");
    if (e && e[0] == '1') c->on = 1;
    const char *eu = getenv("AIP_TOOL_URL");
    if (eu) snprintf(c->url, sizeof c->url, "%s", eu);
    const char *ek = getenv("AIP_TOOL_KEY");
    if (ek) snprintf(c->key, sizeof c->key, "%s", ek);
    const char *em = getenv("AIP_TOOL_MODEL");
    if (em) snprintf(c->model, sizeof c->model, "%s", em);
    FILE *f = fopen(TOOL_CFG, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (!*p || *p == '#') continue;
        char *h = strchr(p, '#');
        if (h) *h = 0;
        char *eq = strchr(p, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = p, *v = eq + 1;
        size_t kl = strlen(k);
        while (kl && (k[kl - 1] == ' ' || k[kl - 1] == '\t')) k[--kl] = 0;
        while (*v == ' ' || *v == '\t') v++;
        size_t vl = strlen(v);
        while (vl && (v[vl - 1] == '\n' || v[vl - 1] == '\r' || v[vl - 1] == ' ')) v[--vl] = 0;
        if (!strcmp(k, "url"))        snprintf(c->url, sizeof c->url, "%s", v);
        else if (!strcmp(k, "key"))   snprintf(c->key, sizeof c->key, "%s", v);
        else if (!strcmp(k, "model")) snprintf(c->model, sizeof c->model, "%s", v);
        else if (!strcmp(k, "on"))    c->on = (v[0] == '1');
    }
    fclose(f);
}

static inline int tool_save(const ToolCfg *c) {
    FILE *f = fopen(TOOL_CFG, "w");
    if (!f) return 0;
    fprintf(f, "# tool.txt — 外部工具（本地大模型）端点。::tool set <url> <key> <model>\n");
    fprintf(f, "# 这个文件只被 ::tool 用；答复一律标 [外部工具]，不进图。\n");
    fprintf(f, "on=%d\nurl=%s\nkey=%s\nmodel=%s\n", c->on, c->url, c->key, c->model);
    fclose(f);
    return 1;
}

static inline void tool_log_line(const ToolCfg *c, const char *q, size_t bytes, int ok, const char *why) {
    FILE *f = fopen(TOOL_LOG, "a");
    if (!f) return;
    time_t now = time(NULL);
    struct tm tmv;
    localtime_r(&now, &tmv);
    fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d\t%s\t%s\t%zu\t%s\t%s\n",
            tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
            c->model[0] ? c->model : "?", q ? q : "?",
            bytes, ok ? "ok" : "fail", why ? why : "");
    fclose(f);
}

/* JSON 字符串转义（只处理必须的：反斜杠/引号/控制字符）。
 * 中文是 UTF-8 原始字节，直接放进去即可（JSON 允许）。 */
static inline size_t tool_json_esc(char *dst, size_t cap, const char *src) {
    size_t n = 0;
    if (!dst || !cap) return 0;
    for (const unsigned char *p = (const unsigned char *)src; p && *p; ++p) {
        const char *rep = NULL;
        char one[8];
        if (*p == '"')       rep = "\\\"";
        else if (*p == '\\') rep = "\\\\";
        else if (*p == '\n') rep = "\\n";
        else if (*p == '\r') rep = "\\r";
        else if (*p == '\t') rep = "\\t";
        else if (*p < 0x20) { snprintf(one, sizeof one, "\\u%04x", *p); rep = one; }
        if (rep) {
            size_t l = strlen(rep);
            if (n + l + 1 >= cap) break;
            memcpy(dst + n, rep, l);
            n += l;
        } else {
            if (n + 2 >= cap) break;
            dst[n++] = (char)*p;
        }
    }
    dst[n] = 0;
    return n;
}

/* 从 OpenAI 兼容回包里取 `"content":"…"`（**只取一条**，不猜结构）。
 * 返回 1 = 取到。转义只解 \" \\ \n \r \t 与 \u00XX（其余 \u 原样留成 '?'，不装懂）。 */
static inline int tool_pick_content(const char *json, char *out, size_t cap) {
    if (!json || !out || cap < 2) return 0;
    const char *p = strstr(json, "\"content\"");
    if (!p) return 0;
    p = strchr(p, ':');
    if (!p) return 0;
    p++;
    while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
    if (*p != '"') return 0;
    p++;
    size_t n = 0;
    while (*p && n + 2 < cap) {
        if (*p == '\\') {
            p++;
            if (!*p) break;
            switch (*p) {
            case 'n': out[n++] = '\n'; break;
            case 'r': out[n++] = '\r'; break;
            case 't': out[n++] = '\t'; break;
            case '"': out[n++] = '"'; break;
            case '\\': out[n++] = '\\'; break;
            case '/': out[n++] = '/'; break;
            case 'u': {
                /* \uXXXX：只认 4 位十六进制；解到 UTF-8（够显示，不追求完整代理对） */
                if (p[1] && p[2] && p[3] && p[4]) {
                    unsigned v = 0;
                    int okd = 1;
                    for (int i = 1; i <= 4; ++i) {
                        char ch = p[i];
                        v <<= 4;
                        if (ch >= '0' && ch <= '9') v |= (unsigned)(ch - '0');
                        else if (ch >= 'a' && ch <= 'f') v |= (unsigned)(ch - 'a' + 10);
                        else if (ch >= 'A' && ch <= 'F') v |= (unsigned)(ch - 'A' + 10);
                        else { okd = 0; break; }
                    }
                    if (okd) {
                        if (v < 0x80) out[n++] = (char)v;
                        else if (v < 0x800) {
                            out[n++] = (char)(0xC0 | (v >> 6));
                            out[n++] = (char)(0x80 | (v & 0x3F));
                        } else {
                            out[n++] = (char)(0xE0 | (v >> 12));
                            out[n++] = (char)(0x80 | ((v >> 6) & 0x3F));
                            out[n++] = (char)(0x80 | (v & 0x3F));
                        }
                        p += 4;
                    } else out[n++] = '?';
                }
                break;
            }
            default: out[n++] = *p; break;
            }
            p++;
            continue;
        }
        if (*p == '"') { out[n] = 0; return 1; }
        out[n++] = *p++;
    }
    out[n] = 0;
    return 1;
}

/* 问一次工具。返回 1 = 成功（out 里是答复原文）；0 = 失败（err 里是原因）。 */
static inline int tool_ask(ToolCfg *c, const char *question, char *out, size_t cap,
                           char *err, size_t errcap) {
    if (out && cap) out[0] = 0;
    if (err && errcap) err[0] = 0;
    if (!c || !question || !out) return 0;
    if (!c->on) { snprintf(err, errcap, "工具没打开（::tool on）"); return 0; }
    if (!c->url[0]) { snprintf(err, errcap, "没配端点（::tool set <url> <key> <model>）"); return 0; }

    char body_path[72], out_path[64];
    /* ⚠ curl 的 `--data-binary` 要的是 `@文件`（带 @ 才是"读文件内容"）：
     * 漏了这个 @ 就会把**路径字符串**当请求体发出去 —— 服务端报 "parse error at line 1,
     * column 1"，而本地看代码完全看不出来（这个坑当场踩了）。 */
    snprintf(body_path, sizeof body_path, "@/tmp/aip_tool_body.%ld", (long)getpid());
    snprintf(out_path, sizeof out_path, "/tmp/aip_tool_out.%ld", (long)getpid());

    char esc[4096];
    tool_json_esc(esc, sizeof esc, question);
    FILE *bf = fopen(body_path + 1, "w");
    if (!bf) { snprintf(err, errcap, "写不了临时文件"); return 0; }
    fprintf(bf, "{\"model\":\"%s\",\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
                "\"temperature\":0,\"max_tokens\":512,\"stream\":false}",
            c->model[0] ? c->model : "local", esc);
    fclose(bf);

    char auth[TOOL_KEY_LEN + 32];
    snprintf(auth, sizeof auth, "Authorization: Bearer %s", c->key);
    char full[TOOL_URL_LEN + 32];
    snprintf(full, sizeof full, "%s/chat/completions", c->url);

    const char *curl = "/usr/bin/curl";
    if (access(curl, X_OK) != 0) curl = "/usr/local/bin/curl";
    if (access(curl, X_OK) != 0) { snprintf(err, errcap, "找不到 curl"); return 0; }

    int pfd[2];
    if (pipe(pfd) != 0) { snprintf(err, errcap, "pipe() 失败"); return 0; }
    long t0 = tool_now_ms();
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); snprintf(err, errcap, "fork() 失败"); return 0; }
    if (pid == 0) {
        int dn = open("/dev/null", O_RDONLY);
        if (dn >= 0) { (void)dup2(dn, 0); close(dn); }
        (void)dup2(pfd[1], 1);
        (void)dup2(pfd[1], 2);
        close(pfd[0]); close(pfd[1]);
        char maxt[16], maxb[24];
        snprintf(maxt, sizeof maxt, "%d", TOOL_TIMEOUT_S);
        snprintf(maxb, sizeof maxb, "%u", TOOL_MAX_BYTES);
        char *av[24];
        int n = 0;
        av[n++] = (char *)curl;
        av[n++] = "-sS"; av[n++] = "-q";
        av[n++] = "--max-time";      av[n++] = maxt;
        av[n++] = "--max-filesize";  av[n++] = maxb;
        av[n++] = "-X"; av[n++] = "POST";
        av[n++] = "-H"; av[n++] = "Content-Type: application/json";
        av[n++] = "-H"; av[n++] = auth;
        av[n++] = "--data-binary"; av[n++] = body_path;
        av[n++] = "-o"; av[n++] = out_path;
        av[n++] = "-w"; av[n++] = "%{http_code} %{size_download}";
        av[n++] = full;
        av[n] = NULL;
        execv(curl, av);
        _exit(127);
    }
    close(pfd[1]);
    int reaped = 0, status = 0, killed = 0;
    char diag[256] = {0};
    size_t used = 0;
    while (!reaped) {
        if (!killed && tool_now_ms() - t0 > (long)TOOL_TIMEOUT_S * 1000L + 2000L) {
            killed = 1;
            (void)kill(pid, SIGKILL);
        }
        struct pollfd pp = { pfd[0], POLLIN, 0 };
        (void)poll(&pp, 1, 20);
        for (;;) {
            char tmp[256];
            ssize_t got = read(pfd[0], tmp, sizeof tmp);
            if (got <= 0) break;
            if (used + (size_t)got < sizeof diag - 1) {
                memcpy(diag + used, tmp, (size_t)got);
                used += (size_t)got;
                diag[used] = 0;
            }
        }
        int ws = 0;
        pid_t r = waitpid(pid, &ws, WNOHANG);
        if (r == pid) { reaped = 1; status = ws; }
    }
    close(pfd[0]);

    char raw[TOOL_MAX_BYTES + 1];
    size_t got = 0;
    FILE *of = fopen(out_path, "r");
    if (of) {
        got = fread(raw, 1, TOOL_MAX_BYTES, of);
        raw[got] = 0;
        fclose(of);
    }
    unlink(body_path + 1);
    unlink(out_path);

    int code = 0;
    if (diag[0]) sscanf(diag, "%d", &code);
    int ok = 0;
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0 && got > 0 && code >= 200 && code < 300) {
        ok = tool_pick_content(raw, out, cap);
        if (!ok) snprintf(err, errcap, "回包里没有 content 字段");
    } else if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        snprintf(err, errcap, "curl 退出码 %d%s%s", WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                 diag[0] ? "：" : "", diag[0] ? diag : "");
    } else {
        /* 非 2xx：把**回包前缀**带出来（服务端的错误说明比 HTTP 码有用得多） */
        char head[160];
        size_t hn = got < sizeof head - 1 ? got : sizeof head - 1;
        memcpy(head, raw, hn);
        head[hn] = 0;
        for (size_t i = 0; i < hn; ++i) if (head[i] == '\n' || head[i] == '\r') head[i] = ' ';
        snprintf(err, errcap, "HTTP %d（%zu 字节）：%.120s", code, got, head);
    }
    tool_log_line(c, question, got, ok, ok ? "" : err);
    if (ok) c->n_ok++; else c->n_fail++;
    c->n_ask++;
    return ok;
}

#endif /* TOOL_H */
