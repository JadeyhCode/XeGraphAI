/* 整数立方根（向下取整） */
long icbrt(long n) {
    if (n < 0) return 0;
    long x = 0;
    while ((x + 1) * (x + 1) * (x + 1) <= n) x++;
    return x;
}
