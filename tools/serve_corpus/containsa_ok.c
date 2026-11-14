/* 字符串是否含字母 a */
const char *contains_a(const char *s) {
    for (int i = 0; s[i]; i++) if (s[i] == 'a') return "yes";
    return "no";
}
/* 字符串是否含字母 z */
const char *contains_z(const char *s) {
    for (int i = 0; s[i]; i++) if (s[i] == 'z') return "yes";
    return "no";
}
