#!/bin/sh
# tests/codegeneral.sh — **代码知识的双实例反统一**（§6.825）：
#   教两条只差一个常数的实例 ⇒ 归纳出参数化概念 ⇒ **没教过的参数也能用**。
#
# 两个族各测一遍（迁移必须不止在一个族上成立）：
#   ① 第二小 / 第三小 ⇒ 第<P>小，试 第五小（输入 1 2 3 → 3）
#   ② 每个数乘以二 / 乘以三 ⇒ 每个数乘以<P>，试 乘以五（1 2 3 → 5 10 15）
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p _t/gen
[ -f code/code_model.bin ] || sh code/build_code_model.sh > _t/gen/build.log 2>&1
[ -f code/code_model.bin ] || { echo "FAIL: 缺 code/code_model.bin"; exit 1; }
BEFORE=$(sha256sum code/code_model.bin | cut -d" " -f1)
fails=0

run_case() {
    NAME=$1; INST=$2; PROBE=$3; WANT=$4
    cp code/code_model.bin _t/gen/m.bin
    printf "::ingest %s\n::save _t/gen/m.bin\n::quit\n" "$INST" \
        | ./build_aip --model _t/gen/m.bin > _t/gen/ingest.log 2>&1
    OUT=$ROOT/_t/gen/run_$NAME.log
    GEN_TRIPLES="$INST" AIP_PARAM_INSIDE=1 \
        python3 tools/generalize.py --model _t/gen/m.bin > "$OUT" 2>&1
    grep -qa "归纳：" "$OUT" || { printf "  [FAIL:%s] 没找到可归纳的实例对\n" "$NAME"; fails=1; return; }
    grep -qa "教过的说法复现：OK" "$OUT" || { printf "  [FAIL:%s] 教过的说法没复现\n" "$NAME"; fails=1; return; }
    P=$(sed -n "s/.*PROBE\t\(.*\)/\1/p" "$OUT" | tail -1)
    GOT=$(sed -n "s/.*PROBE_IO\t[^\t]*\t\(.*\)/\1/p" "$OUT" | tail -1)
    [ "$P" = "$PROBE" ] || { printf "  [FAIL:%s] 迁移探针变成 %s（期望 %s）\n" "$NAME" "$P" "$PROBE"; fails=1; return; }
    [ "$GOT" = "$WANT" ] || { printf "  [FAIL:%s] 没教过的 %s 得到 %s（期望 %s）\n" "$NAME" "$PROBE" "$GOT" "$WANT"; fails=1; return; }
    printf "  [OK:%s] 没教过的 %s ⇒ %s\n" "$NAME" "$PROBE" "$GOT"
}

KT=_t/gen/inst_k.triples
cat > "$KT" <<EOF
运算:第2小	关键词	第二小
运算:第2小	语句	{ qsort(v, (size_t)n, sizeof v[0], cmp_long); long u[64]; int m = 0; for (int i = 0; i < n; i++) if (i == 0 || v[i] != v[i - 1]) u[m++] = v[i]; x = (m >= 2) ? u[1] : u[m > 0 ? m - 1 : 0]; }
运算:第2小	用	片段:cmp_long
运算:第2小	模板	模板:标准输入整数数组
运算:第3小	关键词	第三小
运算:第3小	语句	{ qsort(v, (size_t)n, sizeof v[0], cmp_long); long u[64]; int m = 0; for (int i = 0; i < n; i++) if (i == 0 || v[i] != v[i - 1]) u[m++] = v[i]; x = (m >= 3) ? u[2] : u[m > 0 ? m - 1 : 0]; }
运算:第3小	用	片段:cmp_long
运算:第3小	模板	模板:标准输入整数数组
EOF
run_case kth "$KT" "第五小" "3"

MT=_t/gen/inst_m.triples
cat > "$MT" <<EOF
运算:每数乘2	关键词	每个数乘以二
运算:每数乘2	语句	{ for (int i = 0; i < n; i++) v[i] *= 2; }
运算:每数乘2	输出	打印:空格数组
运算:每数乘2	模板	模板:标准输入整数数组
运算:每数乘3	关键词	每个数乘以三
运算:每数乘3	语句	{ for (int i = 0; i < n; i++) v[i] *= 3; }
运算:每数乘3	输出	打印:空格数组
运算:每数乘3	模板	模板:标准输入整数数组
EOF
run_case mul "$MT" "每个数乘以五" "5 10 15"

AFTER=$(sha256sum code/code_model.bin | cut -d" " -f1)
[ "$BEFORE" = "$AFTER" ] || { printf "  [FAIL] 仓库模型被改了\n"; fails=1; }
[ "$fails" -eq 0 ] && printf "PASS codegeneral（两个族都从两条实例归纳出参数化概念，没教过的参数都能用）\n" || printf "FAIL codegeneral\n"
[ "$fails" -eq 0 ]
