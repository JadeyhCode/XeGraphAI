/* 字符串长度乘二 / 加十 */
long len_double(const char *s) {
    long n = 0;
    while (s[n] && s[n] != 10 && s[n] != 13) n++;
    return n * 2;
}
long len_plus_ten(const char *s) {
    long n = 0;
    while (s[n] && s[n] != 10 && s[n] != 13) n++;
    return n + 10;
}
