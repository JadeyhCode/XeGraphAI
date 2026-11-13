/* 绝对值之和 */
long abs_sum(const long *v, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) t += v[i] < 0 ? -v[i] : v[i];
    return t;
}
