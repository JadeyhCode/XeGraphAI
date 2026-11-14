/* 整数平方根之和 */
long sqrt_sum(const long *v, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) {
        long x = v[i] < 0 ? -v[i] : v[i];
        long r = 0;
        while ((r + 1) * (r + 1) <= x) r++;
        t += r;
    }
    return t;
}
