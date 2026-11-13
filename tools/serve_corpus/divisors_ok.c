/* 正约数个数（正确实现） */
long divisor_count(long n) {
    if (n <= 0) return 0;
    long c = 0;
    for (long i = 1; i * i <= n; i++) {
        if (n % i == 0) c += (i * i == n) ? 1 : 2;
    }
    return c;
}
