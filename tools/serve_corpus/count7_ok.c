/* 数字 7 出现的次数（按十进制位） */
long count7(const long *v, int n) {
    long c = 0;
    for (int i = 0; i < n; i++) {
        long x = v[i] < 0 ? -v[i] : v[i];
        if (x == 0) continue;
        while (x > 0) { if (x % 10 == 7) c++; x /= 10; }
    }
    return c;
}
