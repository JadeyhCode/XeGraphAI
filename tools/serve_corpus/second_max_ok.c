/* 数组中第二大的数（语义：**去重后**第二大的；由需求示例定义） */
long second_max(const long *v, int n) {
    long m1 = v[0], m2 = v[0];
    int have2 = 0;
    for (int i = 1; i < n; i++) {
        if (v[i] > m1) { m2 = m1; have2 = 1; m1 = v[i]; }
        else if (v[i] < m1 && (!have2 || v[i] > m2)) { m2 = v[i]; have2 = 1; }
    }
    return have2 ? m2 : m1;
}
