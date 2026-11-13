/* 单词个数（按空白切分） */
long word_count(const char *s) {
    long c = 0;
    int in = 0;
    for (int i = 0; s[i]; i++) {
        if (s[i] == 32 || s[i] == 9 || s[i] == 10 || s[i] == 13) in = 0;
        else if (!in) { in = 1; c++; }
    }
    return c;
}
