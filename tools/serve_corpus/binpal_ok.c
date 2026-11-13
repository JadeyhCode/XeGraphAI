/* 二进制表示是否回文 */
const char *bin_pal(long n) {
    if (n < 0) return "no";
    int bits[64], k = 0;
    if (n == 0) return "yes";
    while (n > 0) { bits[k++] = (int)(n & 1); n >>= 1; }
    for (int i = 0, j = k - 1; i < j; i++, j--) if (bits[i] != bits[j]) return "no";
    return "yes";
}
