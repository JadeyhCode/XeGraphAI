/* 数组是否对称 */
long is_sym(const long *v, int n) {
    for (int i = 0, j = n - 1; i < j; i++, j--) if (v[i] != v[j]) return 0;
    return 1;
}
