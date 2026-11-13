#!/bin/sh
# tests/generalize.sh — **泛化台**：一条示范 ⇒ 多条推论（举一反三）
#
# 为什么单独一套：holdout/conv 量的是"听不听得懂说法"，这里量的是**外推**：
#   只教一条事实，问**没教过的那一面**（反向 / 更长的链 / 同义词 / 上位）。
#   题面里的实体名全是现场新造的 —— 所以答对不是"记过"，是按属性推的。
#
# 用法：sh tests/generalize.sh [模型]
#   题目文件：tests/generalize.txt（4 列：前置陈述;问句;期望正则;族）
#   族：sym 对称 / trans 传递 / inv 逆 / syn 同义代数 / ctl 对照（必须一直绿）/
#       neg 负向对照（表里没有的谓词，**不许**乱推）
#
# **消融是这套的判据之一**（不是可选项）：同一份题再跑一遍 `AIP_NOALG=1`。
#   要求：① 有表时 sym/trans/inv/syn 全绿；② 关表后**至少一半**新族变红
#   （否则说明增益不是谓词代数带来的，是别处漏出来的）；③ ctl 两遍都绿、neg 两遍都不乱推。
#   三条同时成立才算 PASS —— 这样"分数涨了"不可能靠碰巧。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=${BIN:-./build_aip}
MODEL=${1:-model.bin}
SET=${2:-tests/generalize.txt}
[ -x "$BIN" ] || { echo "FAIL: 缺可执行文件 $BIN"; exit 1; }
[ -f "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }
[ -f "$SET" ] || { echo "FAIL: 缺题目 $SET"; exit 1; }

W="$ROOT/_t/generalize"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
TMP="$W/one.log"

# 跑一遍题集，输出：族<TAB>通过/总数，以及失败清单
run_set() {   # $1 = 标签（带表/关表）
    pass=0; fail=0
    : > "$W/fail.$1"
    echo "sym 0 0
trans 0 0
inv 0 0
syn 0 0
ctl 0 0
neg 0 0" > "$W/fam.$1"
    while IFS='	' read -r setup q want fam; do
        case "$q" in '#'*|'') continue ;; esac
        [ -n "$want" ] || continue
        [ -n "$fam" ] || fam=ctl
        [ "$setup" = "-" ] && setup=""
        {
            if [ -n "$setup" ]; then printf '%s\n' "$setup" | tr ';' '\n'; fi
            printf '%s\n::quit\n' "$q"
        } | timeout 30 "$BIN" --model "$W/m.bin" > "$TMP" 2>&1
        out=$(sed -n 's/.*AI > //p' "$TMP" | tail -1)
        ok=0
        case "$want" in
        !*) printf '%s\n' "$out" | grep -qE "${want#!}" || ok=1 ;;
        *)  printf '%s\n' "$out" | grep -qE "$want" && ok=1 ;;
        esac
        awk -v f="$fam" -v ok="$ok" 'BEGIN{FS=" "} $1==f{ $2+=(ok?1:0); $3+=1; print $1, $2, $3; next } {print}' \
            "$W/fam.$1" > "$W/fam.$1.new" && mv "$W/fam.$1.new" "$W/fam.$1"
        if [ "$ok" -eq 1 ]; then pass=$((pass + 1))
        else
            fail=$((fail + 1))
            printf '%s\t%s\t%s\n' "$fam" "$q" "$out" >> "$W/fail.$1"
        fi
    done < "$SET"
    echo "$pass $fail"
}

echo "== ① 开表（relprops.txt 生效） =="
R1=$(run_set on)
P1=$(printf '%s\n' "$R1" | tail -1 | cut -d' ' -f1)
T1=$(printf '%s\n' "$R1" | tail -1 | cut -d' ' -f2)
awk '{printf "  %-6s %d/%d\n", $1, $2, $3}' "$W/fam.on"
[ -s "$W/fail.on" ] && sed 's/^/  ✗ /' "$W/fail.on"

echo "== ② 关表（AIP_NOALG=1，同题同二进制） =="
R2=$(AIP_NOALG=1 run_set off)
P2=$(printf '%s\n' "$R2" | tail -1 | cut -d' ' -f1)
T2=$(printf '%s\n' "$R2" | tail -1 | cut -d' ' -f2)
awk '{printf "  %-6s %d/%d\n", $1, $2, $3}' "$W/fam.off"

# ---- 三条判据 ----
new_on=0; new_off=0
for f in sym trans inv syn; do
    a=$(awk -v f="$f" '$1==f{print $2 "/" $3}' "$W/fam.on")
    b=$(awk -v f="$f" '$1==f{print $2 "/" $3}' "$W/fam.off")
    new_on=$(awk -v f="$f" '$1==f{print $2}' "$W/fam.on")
    new_off=$(awk -v f="$f" '$1==f{print $2}' "$W/fam.off")
    [ -z "$new_on" ] && new_on=0
    [ -z "$new_off" ] && new_off=0
    eval "on_$f=$new_on"; eval "off_$f=$new_off"
    tot_on=$(awk -v f="$f" '$1==f{print $3}' "$W/fam.on")
    : "${tot_on:=0}"
    printf '  %-6s 开表 %s  关表 %s\n' "$f" "$a" "$b"
done

bad=""
# 判据①：开表时新族全对
for f in sym trans inv syn; do
    tot=$(awk -v f="$f" '$1==f{print $3}' "$W/fam.on")
    got=$(awk -v f="$f" '$1==f{print $2}' "$W/fam.on")
    : "${tot:=0}"; : "${got:=0}"
    [ "$tot" -gt 0 ] && [ "$got" -ne "$tot" ] && bad="$bad
  ✗ 开表时 $f 族没全对（$got/$tot）"
done
# 判据②：关表后新族明显掉分（至少一半新族的题变红）
sum_on=0; sum_off=0
for f in sym trans inv syn; do
    a=$(awk -v f="$f" '$1==f{print $2}' "$W/fam.on"); : "${a:=0}"
    b=$(awk -v f="$f" '$1==f{print $2}' "$W/fam.off"); : "${b:=0}"
    sum_on=$((sum_on + a)); sum_off=$((sum_off + b))
done
if [ "$sum_on" -gt 0 ] && [ "$sum_off" -gt $((sum_on / 2)) ]; then
    bad="$bad
  ✗ 关表后新族仍有 $sum_off/$sum_on 对 —— 增益不是谓词代数带来的（消融没通过）"
fi
# 判据③：ctl 两遍都全绿，neg 两遍都不乱推
for tag in on off; do
    tot=$(awk '$1=="ctl"{print $3}' "$W/fam.$tag"); got=$(awk '$1=="ctl"{print $2}' "$W/fam.$tag")
    : "${tot:=0}"; : "${got:=0}"
    [ "$tot" -gt 0 ] && [ "$got" -ne "$tot" ] && bad="$bad
  ✗ $tag：ctl 对照族没全绿（$got/$tot）"
    tot=$(awk '$1=="neg"{print $3}' "$W/fam.$tag"); got=$(awk '$1=="neg"{print $2}' "$W/fam.$tag")
    : "${tot:=0}"; : "${got:=0}"
    [ "$tot" -gt 0 ] && [ "$got" -ne "$tot" ] && bad="$bad
  ✗ $tag：neg 负向对照乱推了（$got/$tot）"
done

echo
if [ -n "$bad" ]; then
    printf '泛化台：开表 %s/%s，关表 %s/%s\n' "$P1" "$((P1 + T1))" "$P2" "$((P2 + T2))"
    printf '%s\n' "$bad"
    echo "FAIL generalize"
    exit 1
fi
printf '泛化台：开表 %s/%s，关表 %s/%s；消融通过（新族 %d→%d）\n' "$P1" "$((P1 + T1))" "$P2" "$((P2 + T2))" "$sum_on" "$sum_off"
echo "PASS generalize"
