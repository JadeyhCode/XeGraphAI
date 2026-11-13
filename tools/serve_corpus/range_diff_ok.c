/* 极差：最大值 − 最小值 */
long range_diff(const long *v, int n) {
    long mn = v[0], mx = v[0];
    for (int i = 1; i < n; i++) { if (v[i] < mn) mn = v[i]; if (v[i] > mx) mx = v[i]; }
    return mx - mn;
}
