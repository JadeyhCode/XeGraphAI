/* tools/arc_reach.c — **这个家族的题到底有多少**（§6.787）
 *
 * 回答一个具体问题：ARC 里有多少题能被"对象谓词 → 一个动作（刷色/删掉）"这条模型**解释**，
 * 其中又能学出几条规则。实测（ARC-AGI-1 eval 400 题）：
 *   同尺寸 270｜背景没被改且对象变化一致（可判标签）**18**｜动作一致 18｜学出规则 **0**
 * 这条数是"为什么加规则没用"的定量依据，别再用"再加个谓词试试"。
 *
 * 用法：gcc -std=c11 -O2 -I. -o /tmp/arc_reach tools/arc_reach.c && /tmp/arc_reach <任务目录>
 */
#include <dirent.h>
#include "../arc.h"
int main(int argc, char **argv) {
    (void)argc;
    DIR *d = opendir(argv[1]); if (!d) return 2;
    int n=0, same=0, labelok=0, actok=0, solved=0;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0]=='.') continue;
        char path[1024]; snprintf(path,sizeof path,"%s/%s",argv[1],e->d_name);
        ArcTask t; if (arc_task_load(path,&t)) continue;
        n++;
        int allsame=1;
        for (int k=0;k<t.n_train;++k)
            if (t.tr_in[k].rows!=t.tr_out[k].rows || t.tr_in[k].cols!=t.tr_out[k].cols) allsame=0;
        if (!allsame) continue;
        same++;
        int ok=1; int act_kind=-1, act_color=-1;
        for (int k=0;k<t.n_train && ok;++k) {
            int lab[ARC_MAX][ARC_MAX]; ArcObj objs[ARC_MAX*ARC_MAX];
            int m=arc_label_objects(&t.tr_in[k],lab,objs,ARC_MAX*ARC_MAX);
            if (m<1) { ok=0; break; }
            ArcObjLabel lb[ARC_MAX*ARC_MAX];
            if (arc_label_changes(&t.tr_in[k],&t.tr_out[k],objs,m,lb)) { ok=0; break; }
            for (int i=0;i<m;++i) {
                if (lb[i].kind==0) continue;
                if (act_kind<0) { act_kind=lb[i].kind; act_color=lb[i].color; }
                else if (act_kind!=lb[i].kind || (act_kind==1 && act_color!=lb[i].color)) ok=0;
            }
        }
        if (!ok) continue;
        labelok++;
        if (act_kind<0) continue;
        actok++;
        ArcSolution s; memset(&s,0,sizeof s);
        if (arc_stage_learn(&t,&s)) { solved++; continue; }
    }
    closedir(d);
    printf("题 %d｜同尺寸 %d｜标签可判 %d｜动作一致 %d｜学出规则 %d\n", n, same, labelok, actok, solved);
    return 0;
}
