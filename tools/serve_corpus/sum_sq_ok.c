/* 元素平方和 */
long sum_sq(const long *v, int n) {
    long t = 0;
    for (int i = 0; i < n; i++) t += v[i] * v[i];
    return t;
}
