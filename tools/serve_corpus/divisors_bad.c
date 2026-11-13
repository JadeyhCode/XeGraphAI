/* 正约数个数（**错误**实现：漏掉自己，验证层应拒它） */
long divisor_count_bad(long n) {
    long c = 0;
    for (long i = 1; i < n; i++) if (n % i == 0) c++;
    return c;
}
