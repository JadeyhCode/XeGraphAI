/* xe_meta.h — 元认知承接奖励，并用它辅助泛化。
 *
 * 老做法：奖励是写死的表。这里：
 *   奖励 = 元认知按权重打的绝对分（误差扣分、一致性/新颖度加分），
 *   权重 w_* 由**反馈**学出来（不再是人写的常量表）；
 *   泛化的接受线由元认知**自己采纳过的泛化**统计出来（均值 + k*标准差），
 *   没经验时先接受一次，之后就得跟自己的历史比。
 */
#ifndef XE_META_H
#define XE_META_H

#include <math.h>

typedef struct XeMeta {
    double mu_err, var_err;
    double mu_cons, var_cons;
    double mu_nov, var_nov;
    double w_err, w_cons, w_nov;
    double k_accept;
    double mu_reward, var_reward;
    double mu_gen, var_gen;      /* 自己采纳过的泛化的强度统计 */
    size_t n, n_gen;
} XeMeta;

static inline void xe_meta_init(XeMeta *m) {
    memset(m, 0, sizeof *m);
    m->w_err = 0.5; m->w_cons = 0.3; m->w_nov = 0.2;
    m->k_accept = 0.5;
}

static inline void xe_ema(double *mu, double *var, double x, double a) {
    double d = x - *mu;
    *mu += a * d;
    *var = (1 - a) * (*var + a * d * d);
}

/* 奖励：0..1。误差扣分，一致性/新颖度加分（权重是学出来的）*/
static inline double xe_meta_reward(XeMeta *m, double err, double cons, double nov) {
    double r = 1.0 - m->w_err * err + m->w_cons * cons + m->w_nov * nov;
    if (r < 0) r = 0;
    if (r > 1) r = 1;
    xe_ema(&m->mu_err, &m->var_err, err, 0.1);
    xe_ema(&m->mu_cons, &m->var_cons, cons, 0.1);
    xe_ema(&m->mu_nov, &m->var_nov, nov, 0.1);
    xe_ema(&m->mu_reward, &m->var_reward, r, 0.1);
    m->n++;
    return r;
}

/* 反馈学权重：正反馈奖励“当时误差小/一致/新颖”的特征 */
static inline void xe_meta_feedback(XeMeta *m, double err, double cons, double nov, int good) {
    double lr = 0.05;
    double sign = good ? 1.0 : -1.0;
    m->w_err  += lr * sign * (1.0 - err);
    m->w_cons += lr * sign * cons;
    m->w_nov  += lr * sign * nov;
    if (m->w_err < 0.05) m->w_err = 0.05;
    if (m->w_cons < 0.05) m->w_cons = 0.05;
    if (m->w_nov < 0.05) m->w_nov = 0.05;
    double s = m->w_err + m->w_cons + m->w_nov;
    if (s > 1e-6) { m->w_err /= s; m->w_cons /= s; m->w_nov /= s; }
}

static inline int xe_meta_accept(const XeMeta *m, double score) {
    double line = m->mu_reward + m->k_accept * sqrt(m->var_reward > 1e-9 ? m->var_reward : 1e-9);
    return score >= line;
}

/* 泛化：把 b 的某个关系沿“属于”继承到 val（写空→空）。
 * 支持度 support 先跟自己采纳过的泛化比；没历史就先采纳一次。
 * 返回 1 = 采纳并写入；0 = 拒绝。*/
static inline int xe_meta_generalize(AI *ai, XeMeta *m, const char *b_term,
                                     const char *rel, const char *val, double support) {
    if (m->n_gen > 0) {
        double line = m->mu_gen + m->k_accept * sqrt(m->var_gen > 1e-9 ? m->var_gen : 1e-9);
        if (support < line) return 0;
    }
    Token *hb = xe_hole(ai, xe_tokf(ai, "词:%s", b_term, NULL), "默认");
    Token *hv = xe_hole(ai, xe_tokf(ai, "词:%s", val, NULL), "默认");
    uint8_t d = xe_rid(ai, rel);
    if (!hb || !hv || !d) return 0;
    if (xe_link(hb, hv, d) != 0) return 0;
    xe_ema(&m->mu_gen, &m->var_gen, support, 0.5);
    m->n_gen++;
    if (m->k_accept > 0.15) m->k_accept *= 0.99;   /* 采纳成功，接受线略收紧 */
    return 1;
}

#endif /* XE_META_H */
