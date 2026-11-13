/* 连续重复字符压缩：aaabb -> a3b2（单次不写 1） */
const char *rle(const char *s) {
    static char o[512];
    int w = 0;
    for (int i = 0; s[i] && s[i] != 10 && s[i] != 13 && w < 500; ) {
        int j = i;
        while (s[j] == s[i]) j++;
        o[w++] = s[i];
        int c = j - i;
        if (c > 1) { char d[16]; int dl = snprintf(d, sizeof d, "%d", c); for (int q = 0; q < dl && w < 500; q++) o[w++] = d[q]; }
        i = j;
    }
    o[w] = 0;
    return o;
}
