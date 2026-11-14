/* 整数转八进制 */
const char *to_octal(long n) {
    static char o[64];
    if (n == 0) return "0";
    int k = 0;
    unsigned long u = (n < 0) ? (unsigned long)(-n) : (unsigned long)n;
    while (u) { o[k++] = (char)('0' + u % 8); u /= 8; }
    static char r[64];
    int w = 0;
    if (n < 0) r[w++] = '-';
    for (int i = k - 1; i >= 0; i--) r[w++] = o[i];
    r[w] = 0;
    return r;
}
