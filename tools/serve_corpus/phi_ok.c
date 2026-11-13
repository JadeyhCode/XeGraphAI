/* 欧拉函数 φ(n) */
long euler_phi(long n) {
    long r = n;
    for (long p = 2; p * p <= n; p++) {
        if (n % p == 0) {
            while (n % p == 0) n /= p;
            r -= r / p;
        }
    }
    if (n > 1) r -= r / n;
    return r;
}
