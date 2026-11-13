/* 符号和：正 1 / 负 -1 / 零 0 */
long sign_sum(const long *v, int n) {
    long s = 0;
    for (int i = 0; i < n; i++) s += (v[i] > 0) - (v[i] < 0);
    return s;
}
