#!/bin/sh
# tests/calc.sh — **算术能力**：精确、通用、算不了就明说（§6.798）
#
# 为什么单独一套：`patterns.txt` 里原来写着「算术 我 不 会」—— 那是**声明**不是能力。
# 这里把求值器当第一等能力测：整数、分数、小数、幂、括号、中文数字、中文算子、等差求和，
# 以及三条"不许瞎给"的边界（除以零 / 认不出 / 不是算式）。
#
# 判据：每条命令的输出必须**逐字**含期望结果（用 ::calc，走的是对话里同一个求值器）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/calc"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
pass=0; fail=0
ok()  { printf '  [OK]   %-28s %s\n' "$1" "$2"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %-28s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
calc() { printf '::calc %s\n::quit\n' "$1" | ./build_aip --model "$W/m.bin" 2>&1 | grep -a '\[算\]' | head -1; }
want() {   # $1=输入 $2=期望片段 $3=说明
    o=$(calc "$1")
    printf '%s' "$o" | grep -qF -- "$2" && ok "$3" "$(printf '%s' "$o" | sed 's/.*\[算\] //')" \
        || bad "$3" "$(printf '%s' "$o" | sed 's/.*\[算\] //')"
}

want '3+4'            '= 7'      '整数加法'
want '12*13'          '= 156'    '整数乘法'
want '100-37'         '= 63'     '整数减法'
want '100/4'          '= 25'     '整除'
want '1/3+1/6'        '= 1/2'    '分数精确（1/3+1/6=1/2）'
want '0.5+0.25'       '= 3/4'    '小数转分数'
want '(1+2)*3'        '= 9'      '括号'
want '2^10'           '= 1024'   '幂'
want '2 的 10 次方'   '= 1024'   '中文"的N次方"'
want '三 加 四'       '= 7'      '中文数字 + 中文算子'
want '二十三 减 三'   '= 20'     '中文两位数'
want '从 1 加到 100'  '= 5050'   '等差求和（1…100）'
want '从 3 加到 7'    '= 25'     '等差求和（3…7）'
want '-5+3'           '= -2'     '负号'

echo "== 边界：不许瞎给 =="
o=$(calc '10/0')
printf '%s' "$o" | grep -qE '认不出|算不了' && ok "除以零" "$(printf '%s' "$o" | sed 's/.*\[算\] //')" \
    || bad "除以零没拒绝" "$o"
o=$(calc '你好')
printf '%s' "$o" | grep -q '认不出算式' && ok "非算式" "$(printf '%s' "$o" | sed 's/.*\[算\] //')" \
    || bad "非算式没拒绝" "$o"
o=$(printf '猫 有 4 条腿\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1 | grep -a 'AI >' | head -1)
printf '%s' "$o" | grep -q '= 4' && bad "把'4条腿'当算式算了" "$o" || ok "含数字但不是算式" "（走对话，没被算）"

echo "== §6.799 长句里的算式不许抢答（HLE 回归判据）=="
o=$(printf 'The concept of depth in the passage is like the expression 2+2 in a longer paragraph, what does it mean\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1 | grep -a 'AI >' | head -1)
printf '%s' "$o" | grep -qE '= 4$|= 4 ' && bad "长英文句里抢答了 2+2" "$o" || ok "长英文句不抢答" "（走对话）"
o=$(printf '3+4等于几\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1 | grep -a 'AI >' | head -1)
printf '%s' "$o" | grep -q '3+4 = 7' && ok "短句仍然算" "$(printf '%s' "$o" | sed 's/.*AI > //')" || bad "短句反而不算了" "$o"

echo "== 对话里同一条路（不是两套实现） =="
o=$(printf '3+4等于几\n::quit\n' | ./build_aip --model "$W/m.bin" 2>&1 | grep -a 'AI >' | head -1)
printf '%s' "$o" | grep -q '3+4 = 7' && ok "3+4等于几 → 3+4 = 7" "$(printf '%s' "$o" | sed 's/.*AI > //')" \
    || bad "对话里没算" "$o"

printf '\n算术：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
