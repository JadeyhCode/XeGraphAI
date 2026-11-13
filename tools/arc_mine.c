/* tools/arc_mine.c — 从 ARC 训练集里挖"两步组合"（§6.791）
 *
 * 问题：eval 上 7/400 停在 1.8%，而 DFS 深度 3 已经是 62^3 的搜索空间。
 * 假设：eval 的题要 3~4 步，但其中常有两步是**固定搭配**（训练集里反复出现的组合）。
 * 做法：在训练集上枚举所有"两步组合 a→b"能解出来的题，统计频次；高频组合可以做成**宏**，
 *       求解时当一个原语用（等效把搜索深度推到 4~5）。
 *
 * 用法：gcc -std=c11 -O2 -o /tmp/arc_mine tools/arc_mine.c && /tmp/arc_mine <任务目录> [topN]
 * 输出：每行 `频次\topA\topB`（按频次降序）
 */
#include <dirent.h>
#include <stdlib.h>
#include <string.h>
#include "../arc.h"

#define MAXP (ARC_N_OPS * ARC_N_OPS)

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "用法: %s <任务目录> [topN]\n", argv[0]); return 2; }
    DIR *d = opendir(argv[1]);
    if (!d) { perror("opendir"); return 2; }
    static int cnt[MAXP];
    memset(cnt, 0, sizeof cnt);
    int tasks = 0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[1024];
        snprintf(path, sizeof path, "%s/%s", argv[1], e->d_name);
        ArcTask t;
        if (arc_task_load(path, &t)) continue;
        tasks++;
        for (int a = 0; a < ARC_N_OPS; ++a) {
            for (int b = 0; b < ARC_N_OPS; ++b) {
                if (a == b) continue;
                int ok = 1;
                for (int k = 0; k < t.n_train && ok; ++k) {
                    ArcGrid m, o;
                    if (ARC_OPS[a].fn(&t.tr_in[k], &m)) { ok = 0; break; }
                    if (ARC_OPS[b].fn(&m, &o)) { ok = 0; break; }
                    if (!arc_grid_eq(&o, &t.tr_out[k])) ok = 0;
                }
                if (ok) cnt[a * ARC_N_OPS + b]++;
            }
        }
    }
    closedir(d);
    int topN = argc >= 3 ? atoi(argv[2]) : 20;
    int order[MAXP];
    for (int i = 0; i < MAXP; ++i) order[i] = i;
    for (int i = 1; i < MAXP; ++i) {
        int x = order[i], j = i - 1;
        while (j >= 0 && cnt[order[j]] < cnt[x]) { order[j + 1] = order[j]; j--; }
        order[j + 1] = x;
    }
    fprintf(stderr, "扫描 %d 题（%d 个两步组合）\n", tasks, MAXP);
    for (int i = 0; i < topN && cnt[order[i]] > 0; ++i) {
        int idx = order[i];
        printf("%d\t%s\t%s\n", cnt[idx], ARC_OPS[idx / ARC_N_OPS].name, ARC_OPS[idx % ARC_N_OPS].name);
    }
    return 0;
}
