/* 偶数元素之和 */
long even_sum(const long *v, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) if (v[i] % 2 == 0) t += v[i];
    return t;
}
