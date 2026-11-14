#!/bin/sh
# tests/codegeneral.sh — **代码知识的双实例反统一**（§6.825）：
#   教两条只差一个常数的实例（第二小 / 第三小）⇒ 归纳出参数化概念（第<P>小）
#   ⇒ **没教过的参数（第五小）也能用**。这是本项目第一次测到非零迁移。
#
# 自带实例（不依赖未跟踪文件）；仓库模型不动，实例灌进副本 _t/gen/m.bin。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p _t/gen
[ -f code/code_model.bin ] || sh code/build_code_model.sh > _t/gen/build.log 2>&1
[ -f code/code_model.bin ] || { echo "FAIL: 缺 code/code_model.bin"; exit 1; }
BEFORE=$(sha256sum code/code_model.bin | cut -d" " -f1)
INST=_t/gen/instances.triples
cat > "$INST" <<EOF
运算:第2小	关键词	第二小
运算:第2小	语句	{ qsort(v, (size_t)n, sizeof v[0], cmp_long); long u[64]; int m = 0; for (int i = 0; i < n; i++) if (i == 0 || v[i] != v[i - 1]) u[m++] = v[i]; x = (m >= 2) ? u[1] : u[m > 0 ? m - 1 : 0]; }
运算:第2小	用	片段:cmp_long
运算:第2小	模板	模板:标准输入整数数组
运算:第3小	关键词	第三小
运算:第3小	语句	{ qsort(v, (size_t)n, sizeof v[0], cmp_long); long u[64]; int m = 0; for (int i = 0; i < n; i++) if (i == 0 || v[i] != v[i - 1]) u[m++] = v[i]; x = (m >= 3) ? u[2] : u[m > 0 ? m - 1 : 0]; }
运算:第3小	用	片段:cmp_long
运算:第3小	模板	模板:标准输入整数数组
EOF
cp code/code_model.bin _t/gen/m.bin
printf "::ingest %s\n::save _t/gen/m.bin\n::quit\n" "$INST" \
    | ./build_aip --model _t/gen/m.bin > _t/gen/ingest.log 2>&1
OUT=$ROOT/_t/gen/run.log
GEN_TRIPLES="$INST" AIP_PARAM_INSIDE=1 \
    python3 tools/generalize.py --model _t/gen/m.bin > "$OUT" 2>&1
printf "  （generalize rc=%s）\n" "$?"
fails=0
grep -qa "归纳：" "$OUT" || { printf "  [FAIL] 没找到可归纳的实例对\n"; fails=1; }
grep -qa "教过的 k=2/3 复现：OK" "$OUT" || { printf "  [FAIL] 教过的 k 没复现\n"; fails=1; }
TR=$(sed -n "s/.*迁移（没教过 第五小）：\([0-9]*\)\/6.*/\1/p" "$OUT" | tail -1)
TR=${TR:-0}
printf "  迁移（没教过 第五小）：%s/6\n" "$TR"
[ "$TR" -ge 5 ] || { printf "  [FAIL] 迁移 < 5/6\n"; fails=1; }
AFTER=$(sha256sum code/code_model.bin | cut -d" " -f1)
[ "$BEFORE" = "$AFTER" ] || { printf "  [FAIL] 仓库模型被改了\n"; fails=1; }
[ "$fails" -eq 0 ] && printf "PASS codegeneral（归纳成功；教过 k=2/3 复现；没教过 k=5 迁移 %s/6）\n" "$TR" || printf "FAIL codegeneral\n"
[ "$fails" -eq 0 ]
