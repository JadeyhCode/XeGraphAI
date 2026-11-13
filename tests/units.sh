#!/bin/sh
# tests/units.sh — **单位换算/百分比**：算得对、量纲不符就拒绝、表能下沉到图（§6.800）
#
# 判据：
#   ① 常见换算（长度/时间/质量/温度）逐字正确；
#   ② 量纲不符（米↔千克）**拒绝换算**，不猜；
#   ③ 百分比两种语序都对；
#   ④ `::unit sync` 把表写进图后，`AIP_UNITS=graph`（**不用内置表**）答复与内置模式逐字相同；
#   ⑤ 改图上的系数 ⇒ 换算结果跟着变（证明读的是图，不是硬编码）；
#   ⑥ `::` 命令不许被抢答路径截走（踩过：`::teach 厘米 倍数 1/2` 被当成算式算掉）；
#   ⑦ 长英文句不抢答（和 §6.799 同一条纪律）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/units"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
pass=0; fail=0
ok()  { printf '  [OK]   %-30s %s\n' "$1" "$2"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %-30s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
ask() { printf '%b\n::quit\n' "$1" | env ${2:-} ./build_aip --model "${3:-$W/m.bin}" 2>&1; }
ans() { printf '%b\n::quit\n' "$1" | env ${2:-} ./build_aip --model "${3:-$W/m.bin}" 2>&1 | grep -a 'AI > ' | head -1 | sed 's/.*AI > //'; }
chk() { # $1=问题 $2=期望片段 $3=说明
    r=$(ans "$1")
    printf '%s' "$r" | grep -qF -- "$2" && ok "$3" "$r" || bad "$3" "$r"
}

chk '3 米 等于 多少 厘米'      '300 厘米'   '长度：米→厘米'
chk '100 厘米 是 几 米'        '1 米'       '长度：厘米→米'
chk '1 小时 转 分钟'            '60 分钟'    '时间：小时→分钟'
chk '5 千克 换算成 克'          '5000 克'    '质量：千克→克'
chk '100 摄氏度 等于 多少 华氏度' '212 华氏度' '温度：摄氏→华氏'
chk '200 的 15%'                '= 30'       '百分比：A 的 B%'
chk '15% 的 200'                '= 30'       '百分比：B% 的 A'

echo "== §6.801 带量纲的比较 / 相加 =="
chk '3 米 和 200 厘米 哪个长'   '第一个大'  '比较：米 vs 厘米'
chk '200 厘米 和 3 米 谁更长'   '第二个大'  '比较：反过来问'
chk '3 米 和 300 厘米 哪个长'   '一样'      '比较：相等'
chk '3 米 + 20 厘米 等于 多少'  '16/5'      '相加：不同单位'
chk '1 小时 加 30 分钟'         '3/2'       '相加：时间'
r=$(ans '3 米 和 2 千克 哪个大')
printf '%s' "$r" | grep -q '不是同一种量' && ok "跨量纲比较拒绝" "$r" || bad "跨量纲比较没拒绝" "$r"
r=$(ans '3 米 加 2 千克')
printf '%s' "$r" | grep -q '不是同一种量' && ok "跨量纲相加拒绝" "$r" || bad "跨量纲相加没拒绝" "$r"

echo "== 量纲不符：必须拒绝 =="
r=$(ans '3 米 等于 多少 千克')
printf '%s' "$r" | grep -q '不是同一种量' && ok "米↔千克 拒绝换算" "$r" || bad "越界换算没拒绝" "$r"

echo "== §6.802 日常数学：折扣 / 平均 / 比…多几 % =="
chk '打八折'                '80%'      '折扣：八折=80%'
chk '打八五折'              '85%'      '折扣：八五折=85%'
chk '100 元 打八折'         '= 80'     '折扣：算原价'
chk '200 打 9 折'           '= 180'    '折扣：阿拉伯数字'
chk '平均 3 4 5'            '= 4'      '平均：整数'
chk '1 2 3 4 平均值'        '5/2'      '平均：分数'
chk '100 比 80 多 多少%'    '多 25%'   '百分比差：多（"多少"里的少不算少）'
chk '80 比 100 少 百分之几' '少 20%'   '百分比差：少'

echo "== §6.803 量纲的乘/除：速率与单价 =="
chk '每秒 3 米 走 20 秒'    '= 60 米'     '速度×时间（每X A Y 写法）'
chk '3 米每秒 走 20 秒'     '= 60 米'     '速度×时间（A 米每秒 写法）'
chk '每小时 60 千米 开 2 小时' '= 120 千米' '速度×时间（千米）'
chk '100 米 用了 20 秒'     '5 米/秒'     '路程÷时间'
chk '5 米 用了 2 秒'        '5/2'         '路程÷时间（分数）'
chk '3 元 一个 买 5 个'     '= 15 元'     '单价×数量'
chk '每个 3 元 买 5 个'     '= 15 元'     '单价×数量（换个说法）'

echo "== §6.814 百分比链（连乘，不是加总）=="
chk '100 涨 10% 再 降 10%'      '= 99'    '涨10%再降10% = 99'
chk '200 降 20% 再 涨 50%'      '= 240'   '降20%再涨50% = 240'
chk '80 降 10% 再 降 10% 再 涨 20%' '77.76' '三步连乘'
r=$(ans '100 涨 10%')
printf '%s' "$r" | grep -q '连乘' && bad "单步也走连乘（抢答）" "$r" || ok "单步不走连乘" "（交给折扣/百分比）"

echo "== 表下沉到图 =="
o=$(ask "::unit sync\n::save $W/m2.bin")
[ -s "$W/m2.bin" ] || bad "sync 后没存出模型" "$W/m2.bin"
printf '%s' "$o" | grep -q '写进图' && ok "::unit sync 写入" "$(printf '%s' "$o" | grep -a '单位表' | head -1 | sed 's/.*\[单位表\] //')" \
    || bad "sync 没写" "$(printf '%s' "$o" | grep -a '单位表' | head -2 | tr '\n' ' ')"
a=$(ans '3 米 等于 多少 厘米' '' "$W/m.bin")
b=$(ans '3 米 等于 多少 厘米' 'AIP_UNITS=graph' "$W/m2.bin")
# 纯图模式的表**必须**来自图：list 要标明来源
o2=$(ask '::unit list' 'AIP_UNITS=graph' "$W/m2.bin")
printf '%s' "$o2" | grep -q '只从图里读' && ok "纯图模式自报来源" "只从图里读" || bad "纯图模式没自报来源" "$(printf '%s' "$o2" | head -2 | tr '\n' ' ')"
[ -n "$a" ] && [ "$a" = "$b" ] && ok "内置 / 纯图 答复相同" "$a" || bad "两条来源不一致" "内置=$a 图=$b"

echo "== 改图上的系数 ⇒ 结果跟着变（证明读图） =="
r=$(ans '::teach 厘米 倍数 1/2\n3 米 等于 多少 厘米' 'AIP_UNITS=graph' "$W/m2.bin")
printf '%s' "$r" | grep -q '6 厘米' && ok "厘米=1/2米 ⇒ 3米=6厘米" "$r" || bad "改图没生效" "$r"

echo "== :: 命令不许被抢答截走 =="
o=$(ask '::teach 厘米 倍数 1/2')
printf '%s' "$o" | grep -q '教 会 了' && ok "::teach 正常执行" "教会了" || bad "命令被截走" "$(printf '%s' "$o" | head -3 | tr '\n' ' ')"

echo "== 长英文句不抢答 =="
r=$(ans 'The distance is 3 m and the mass is 5 kg in this problem, what do they mean')
printf '%s' "$r" | grep -q '= ' && bad "长英文句被抢答" "$r" || ok "长英文句不抢答" "（走对话）"

printf '\n单位/百分比：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
