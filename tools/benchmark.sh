#!/bin/sh
# tools/benchmark.sh — **基准测试**：这个白箱 AI 到底多快、多准、在哪儿不行
#
# 三段：
#   A 速度：载入 / 图查询 / 思维 / 一问一答 / 吞吐 / 常驻内存 / 灌图 / 存盘 / 规模扫描
#          延迟一律用**差分法**（N 条 − 0 条，扣掉进程启动的固定开销），否则 20~30 ms 的启动会盖住 µs 级信号。
#   B 能力：`tests/fixtures/bench/battery.txt` 里 69 条**计分项**，十类
#          （事实召回 / 是否判断 / 弃答 / 说法归一 / 多跳 / 教学 / 存读 / 写代码 / 约束求解 / 一致性）。
#          每条都是「输出里有没有这个判据」+「有没有出现反例」，不是「看起来像」。
#   C 边界：`tests/fixtures/bench/boundary.txt` 里 8 条**不计分**的探测（算术/常识/翻译/创作/摘要…），
#          原样打印它的回答 —— 看它会不会硬编。
#
# 用法：
#   sh tools/benchmark.sh                  # 用 model.bin，写 _bench/report.md
#   sh tools/benchmark.sh <模型> [--fast]  # --fast 跳过规模扫描（省 ~15 s）
#   CODE_MODEL=<代码模型> sh tools/benchmark.sh   # 默认 code/code_model_taught.bin
#
# 口径（写进报告，免得拿错数去比）：
#   · 一问一答 = 一次 stdin 批次里「问一句 + 拿到回答」的端到端摊薄值，含读句子/切词/查图/组织回答。
#   · 图查询 = `::rel <词>` 那条纯读路径；思维 = `::think <词>`。
#   · B 段百分比**不能**跟大模型 benchmark 的百分比对等：这里考的是这个符号系统**声明支持**的十类。
#   · 改了这个脚本或电池 ⇒ 老报告里的数不能再用（口径会变）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
AIP="$ROOT/build_aip"
MODEL=${1:-$ROOT/model.bin}
case "$MODEL" in /*) ;; *) MODEL="$PWD/$MODEL" ;; esac
CODE_MODEL=${CODE_MODEL:-$ROOT/code/code_model_taught.bin}
BAT=${BAT:-$ROOT/tests/fixtures/bench/battery.txt}
BND=${BND:-$ROOT/tests/fixtures/bench/boundary.txt}
PARA="$ROOT/tests/fixtures/bench/para.txt"
FAST=0
[ "${2:-}" = "--fast" ] && FAST=1

[ -x "$AIP" ] || { echo "缺 $AIP：先 gcc -std=c11 -O2 -Wall -Wextra -Wpedantic -Werror -o build_aip main.c -lm" >&2; exit 1; }
[ -s "$MODEL" ] || { echo "缺模型 $MODEL" >&2; exit 1; }

W="$ROOT/_bench"; rm -rf "$W"; mkdir -p "$W"
CODE_AVAIL=1
[ -s "$CODE_MODEL" ] || { CODE_AVAIL=0; echo "⚠ 缺 $CODE_MODEL：code 类跳过（不参与总分）"; }
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
REPORT="$W/report.md"
TAB=$(printf '\t')
ms() { date +%s%N | cut -c1-13; }
now=$(date '+%Y-%m-%d %H:%M')

# 去噪：去掉「你 > AI > 」前缀和所有空格（中文回答里的词间空格是排版，不是内容）
norm() { sed 's/你[[:space:]]*>[[:space:]]*AI[[:space:]]*>[[:space:]]*//g' | tr -d ' \t\r'; }
# 回答行（用于一致性判据与失败诊断）：只认这几种开头的行，避开横幅/统计尾巴
anslines() { grep -E '^(我记的是|对|不对|我还|我记|不知道|这个|记下|我还不认识|大概|推测|没有|可以)' || true; }
# 取「AI > 」之后的原文（边界探测/失败诊断用；多行回答只取最后一行）
ansline() { awk '{ i=index($0,"AI > "); if (i>0) print substr($0,i+5) }' | tr -d ' \t\r' | grep -v '^$' | tail -1 ; }

fingerprint() {   # $1=模型 ⇒ "节点 边"（读 ::stats 那一行）
    printf '::stats\n::quit\n' | "$AIP" --model "$1" 2>&1 \
        | sed -n 's/.*符号节点=\([0-9][0-9]*\).*边=\([0-9][0-9]*\).*/\1 \2/p' | head -1
}

echo "=== 基准测试（$now）==="
echo "模型：$(basename "$MODEL")"
FP=$(fingerprint "$MODEL")
NODES=$(printf '%s' "$FP" | cut -d' ' -f1); EDGES=$(printf '%s' "$FP" | cut -d' ' -f2)
echo "指纹：节点 ${NODES:-?} / 边 ${EDGES:-?}"

# ==================== A. 速度 ====================
printf '::quit\n' > "$T/e.in"
base=999999; i=0
while [ $i -lt 3 ]; do
    t0=$(ms); "$AIP" --model "$MODEL" < "$T/e.in" >/dev/null 2>&1; t1=$(ms)
    d=$((t1 - t0)); [ "$d" -lt "$base" ] && base=$d
    i=$((i + 1))
done

diff_ms() {   # $1=一行命令 $2=次数 ⇒ 摊薄到单条的毫秒（3 次取最小，负数夹到 0）
    yes "$1" | head -n "$2" > "$T/q.in"; echo '::quit' >> "$T/q.in"
    best=999999; k=0
    while [ $k -lt 3 ]; do
        t0=$(ms); "$AIP" --model "$MODEL" < "$T/q.in" >/dev/null 2>&1; t1=$(ms)
        d=$((t1 - t0)); [ "$d" -lt "$best" ] && best=$d
        k=$((k + 1))
    done
    awk -v a="$best" -v b="$base" -v n="$2" 'BEGIN{ v=(a-b)/n; if (v < 0) v = 0; printf "%.3f", v }'
}

RSS_KB=$( { /usr/bin/time -f '%M' "$AIP" --model "$MODEL" < "$T/e.in" >/dev/null; } 2>&1 | tail -1 )

# 灌图三种口径（§6.750 的教训：把「造词」和「写边」混在一个数里会差 3 个数量级）
#   ① ::ingest 新事实 4000 条（每条造一个新主语）—— 真实扩图的成本
#   ② ::ingest 重复事实 20000 条（同一个三元组）—— 纯写边/去重成本
#   ③ 一句一句教（自然说法）300 条 —— 交互式教学的单条成本（含解析/帧/思维步）
IG_N=4000; REP_N=20000; TCH_N=300
awk -v n="$IG_N" 'BEGIN{for(i=1;i<=n;i++) printf "测词%d\t属于\t动物\n", i}' > "$T/ig.txt"
awk -v n="$REP_N" 'BEGIN{for(i=1;i<=n;i++) print "猫\t吃\t老鼠"}' > "$T/rep.txt"
awk -v n="$TCH_N" 'BEGIN{for(i=1;i<=n;i++) print "猫 吃 老鼠"}' > "$T/tch.txt"
for f in ig rep tch; do echo '::quit' >> "$T/$f.txt"; done
printf '::ingest %s/ig.txt\n::quit\n' "$T" > "$T/ig.in"
printf '::ingest %s/rep.txt\n::quit\n' "$T" > "$T/rep.in"

base2=999999; k=0
while [ $k -lt 3 ]; do
    t0=$(ms); "$AIP" --model "$MODEL" < "$T/e.in" >/dev/null 2>&1; t1=$(ms)
    d=$((t1 - t0)); [ "$d" -lt "$base2" ] && base2=$d; k=$((k + 1))
done

qps_of() {   # $1=输入文件 $2=条数 ⇒ 条/秒（2 次取最小，扣 base2）
    best=999999; k=0
    while [ $k -lt 2 ]; do
        t0=$(ms); "$AIP" --model "$MODEL" < "$1" >/dev/null 2>&1; t1=$(ms)
        d=$((t1 - t0)); [ "$d" -lt "$best" ] && best=$d; k=$((k + 1))
    done
    awk -v a="$best" -v b="$base2" -v n="$2" 'BEGIN{ v=(b>=a) ? 0 : n*1000/(a-b); printf "%.0f", v }'
}
ING_QPS=$(qps_of "$T/ig.in" "$IG_N")
REP_QPS=$(qps_of "$T/rep.in" "$REP_N")
TCH_QPS=$(qps_of "$T/tch.txt" "$TCH_N")

# 存盘
printf '::save %s/s.bin\n::quit\n' "$T" > "$T/s.in"
best=999999; k=0
while [ $k -lt 2 ]; do
    t0=$(ms); "$AIP" --model "$MODEL" < "$T/s.in" >/dev/null 2>&1; t1=$(ms)
    d=$((t1 - t0)); [ "$d" -lt "$best" ] && best=$d; k=$((k + 1))
done
SAVE_MS=$((best - base2))

REL_MS=$(diff_ms '::rel 猫' 4000)
THINK_MS=$(diff_ms '::think 猫' 1000)
TURN_MS=$(diff_ms '猫是什么' 300)

REL_US=$(awk -v x="$REL_MS" 'BEGIN{printf "%.1f", x*1000}')
THINK_US=$(awk -v x="$THINK_MS" 'BEGIN{printf "%.1f", x*1000}')
QPS=$(awk -v x="$TURN_MS" 'BEGIN{printf "%.0f", (x>0 ? 1000/x : 0)}')
RSS_MB=$(awk -v x="$RSS_KB" 'BEGIN{printf "%.0f", x/1024}')
echo
echo "A 速度：载入 $base ms ｜ ::rel $REL_US µs ｜ ::think $THINK_US µs ｜ 一问一答 $(printf '%.1f' "$TURN_MS") ms ｜ 问答 $QPS 条/s"
echo "        RSS $RSS_MB MB ｜ 灌图 $ING_QPS 条/s（新事实） $REP_QPS 条/s（重复） $TCH_QPS 条/s（一句一句教） ｜ 存盘 $SAVE_MS ms"

SCALE_ROWS=""
if [ "$FAST" -eq 0 ]; then
    echo "A2 规模扫描（同一查询，图越大越慢吗）："
    for m in "$MODEL" "$ROOT/model_post3.bin" "$CODE_MODEL"; do
        [ -s "$m" ] || continue
        f=$(fingerprint "$m"); nn=$(printf '%s' "$f" | cut -d' ' -f1); ee=$(printf '%s' "$f" | cut -d' ' -f2)
        b=999999; k=0
        while [ $k -lt 2 ]; do
            t0=$(ms); "$AIP" --model "$m" < "$T/e.in" >/dev/null 2>&1; t1=$(ms)
            d=$((t1 - t0)); [ "$d" -lt "$b" ] && b=$d; k=$((k + 1))
        done
        SAVED=$MODEL; MODEL=$m
        OLD_BASE=$base; base=$b                 # 差分基线必须换成**这个模型自己**的空跑时间，否则会把载入差算进单条延迟
        r=$(diff_ms '::rel 猫' 20000)        # 列出这个词的全部知识（输出量随边数增）
        q=$(diff_ms '::rel 无此词甲' 20000)     # 查无此词：纯哈希查找，输出为空
        base=$OLD_BASE; MODEL=$SAVED
        rus=$(awk -v x="$r" 'BEGIN{printf "%.1f", x*1000}')
        qus=$(awk -v x="$q" 'BEGIN{printf "%.1f", x*1000}')
        printf '   %-28s 节点 %7s 边 %8s ｜ 载入 %4d ms ｜ ::rel(列出) %7s µs ｜ ::rel(查找) %5s µs\n' \
            "$(basename "$m")" "${nn:-?}" "${ee:-?}" "$b" "$rus" "$qus"
        SCALE_ROWS="$SCALE_ROWS| $(basename "$m") | ${nn:-?} | ${ee:-?} | $b | $rus | $qus |
"
    done
fi

# ==================== B. 能力 ====================
# 电池跑两遍：① 出厂模型 ② 扩图后的模型（同一套判据、同一份电池）—— 回答「扩图会不会掉语义」
mkscore() {  # $1=类别 $2=通过 $3=总数
    printf '| %s | %s | %s | %s |\n' "$1" "$2" "$3" \
        "$(awk -v p="$2" -v n="$3" 'BEGIN{printf "%.0f%%", (n>0 ? 100*p/n : 0)}')" >> "$SCOREFILE"
    TOT_P=$((TOT_P + $2)); TOT_N=$((TOT_N + $3))
}

run_item() {   # $1=类别 $2=名称 $3=输入 $4=必须 $5=禁止（能力模型用 $CAP_MODEL）
    cat=$1; name=$2; inp=$3; must=$4; mustnot=${5:-}
    if [ "$cat" = "code" ] && [ "$CODE_AVAIL" -eq 0 ]; then
        SKIPPED=$((SKIPPED + 1)); printf '  [SKIP] %s（缺代码模型）\n' "$name"; return
    fi
    d=$(mktemp -d "$T/it.XXXXXX")
    cp "$PARA" "$d/para.txt"
    inp=$(printf '%s' "$inp" | sed "s|%W%|$d|g")

    p2=""
    case $inp in *'~~'*) p2=${inp#*'~~'}; inp=${inp%%'~~'*} ;; esac

    out=""
    if [ -n "$p2" ]; then
        printf '%b\n::save %s/m.bin\n::quit\n' "$inp" "$d" | ( cd "$d" && "$AIP" --model "$CAP_MODEL" > o1.txt 2>&1 )
        printf '%b\n::quit\n' "$p2" | ( cd "$d" && "$AIP" --model "$d/m.bin" > o2.txt 2>&1 )
        out=$(cat "$d/o1.txt" "$d/o2.txt")
    else
        case $cat in
            code) md="$CODE_MODEL"; run="$ROOT" ;;
            *)    md="$CAP_MODEL";  run="$d" ;;
        esac
        printf '%b\n::quit\n' "$inp" | ( cd "$run" && "$AIP" --model "$md" > "$d/o.txt" 2>&1 )
        out=$(cat "$d/o.txt")
    fi

    n=$(printf '%s' "$out" | norm)
    verdict=0; why=""
    if [ "$must" = "SAME" ]; then
        lines=$(printf '%s\n' "$n" | anslines | head -3)
        cnt=$(printf '%s\n' "$lines" | sort -u | wc -l | tr -d ' ')
        nlines=$(printf '%s\n' "$lines" | grep -c . || true)
        if [ "$cnt" -eq 1 ] && [ "$nlines" -eq 3 ]; then verdict=0; else why="回答不一致（$nlines 条 / $cnt 种）"; fi
    else
        printf '%s' "$n" | grep -qE -- "$must" || { verdict=1; why="没找到 /$must/"; }
    fi
    if [ "$verdict" -eq 0 ] && [ -n "$mustnot" ]; then
        printf '%s' "$n" | grep -qE -- "$mustnot" && { verdict=1; why="出现反例 /$mustnot/"; }
    fi

    if [ "$cur" != "$cat" ]; then
        [ -n "$cur" ] && mkscore "$cur" "$cp_" "$cn_"
        cur=$cat; cp_=0; cn_=0
    fi
    cn_=$((cn_ + 1))
    if [ "$verdict" -eq 0 ]; then
        cp_=$((cp_ + 1))
        printf '  [OK]   %s\n' "$name"
    else
        ans=$(printf '%s' "$out" | grep -aE '\[合成\]|^ *· |^ *[0-9]+\. |--\[' | tail -2 | tr -s ' ' | cut -c1-150)
        [ -n "$ans" ] || ans=$(printf '%s' "$out" | ansline | cut -c1-100)
        printf '  [FAIL] %-24s %-22s 答: %s\n' "$name" "$why" "$ans"
        printf '| %s | %s | %s | `%s` |\n' "$cat" "$name" "$why" "$ans" >> "$FAILFILE"
    fi
    rm -rf "$d"
}

run_battery() {   # $1=标题 $2=能力模型 $3=分数表 $4=失败表
    CAP_MODEL=$2; SCOREFILE=$3; FAILFILE=$4
    : > "$SCOREFILE"; : > "$FAILFILE"
    TOT_P=0; TOT_N=0; cur=""; cp_=0; cn_=0; SKIPPED=0
    echo
    echo "=== $1 ==="
    IFS=$TAB
    while read -r cat name inp must mustnot; do
        case "$cat" in ''|'#'*) continue ;; esac
        run_item "$cat" "$name" "$inp" "$must" "$mustnot"
    done < "$BAT"
    [ -n "$cur" ] && mkscore "$cur" "$cp_" "$cn_"
    PCT=$(awk -v p="$TOT_P" -v n="$TOT_N" 'BEGIN{printf "%.1f", (n>0 ? 100*p/n : 0)}')
    echo "  ---- 合计 $TOT_P / $TOT_N（$PCT%）"
}

run_battery "B 能力（出厂模型 $(basename "$MODEL")）" "$MODEL" "$W/cap.txt" "$W/capfail.txt"
CAP_P=$TOT_P; CAP_N=$TOT_N; CAP_PCT=$PCT
CAP2=0
SCALE_CAP=${SCALE_CAP:-0}
if [ "$SCALE_CAP" -eq 1 ] && [ -s "$ROOT/model_post3.bin" ]; then
    run_battery "B2 扩图后（同一套电池：model_post3.bin，26 万边）" "$ROOT/model_post3.bin" "$W/cap2.txt" "$W/capfail2.txt"
    CAP2=1; CAP2_P=$TOT_P; CAP2_N=$TOT_N; CAP2_PCT=$PCT
fi

# ==================== C. 边界（不计分）====================
echo
echo "=== C 边界（不计分：它声明不支持的）==="
BND_ROWS=""
while IFS=$TAB read -r name inp; do
    case "$name" in ''|'#'*) continue ;; esac
    printf '%b\n::quit\n' "$inp" > "$T/b.in"
    o=$("$AIP" --model "$MODEL" < "$T/b.in" 2>&1 | ansline | cut -c1-96)
    printf '  %-12s %s\n' "$name" "$o"
    BND_ROWS="$BND_ROWS| $name | $inp | $o |
"
done < "$BND"

# ==================== 报告 ====================
{
    echo "# 基准测试报告（$(basename "$MODEL")）"
    echo
    echo "- 生成时间：$now"
    echo "- 模型：\`$(basename "$MODEL")\`（节点 ${NODES:-?} / 边 ${EDGES:-?}）"
    echo "- 主机：$(uname -srm) ｜ $(nproc) 核 ｜ gcc $(gcc -dumpversion)"
    echo "- 口径：延迟用差分法（N 次 − 0 次）；能力项见 \`tests/fixtures/bench/battery.txt\`"
    echo
    echo "## A 速度"
    echo
    echo "| 项 | 值 |"
    echo "|---|---|"
    echo "| 载入（冷启动） | $base ms |"
    echo "| 图查询 \`::rel\` | $REL_US µs |"
    echo "| 思维 \`::think\` | $THINK_US µs |"
    echo "| 一问一答（端到端） | $(printf '%.1f' "$TURN_MS") ms |"
    echo "| 问答吞吐 | $QPS 条/s |"
    echo "| 常驻内存 RSS | $RSS_MB MB |"
    echo "| 灌图吞吐 \`::ingest\`（新事实，含造词） | $ING_QPS 条/s |"
    echo "| 灌图吞吐 \`::ingest\`（重复事实，纯写边） | $REP_QPS 条/s |"
    echo "| 教学吞吐（自然说法，含解析/帧/思维步） | $TCH_QPS 条/s |"
    echo "| 存盘 | $SAVE_MS ms |"
    echo
    if [ -n "$SCALE_ROWS" ]; then
        echo "### 规模扫描"
        echo
        echo "| 模型 | 节点 | 边 | 载入 ms | ::rel 列出 µs | ::rel 查找 µs |"
        echo "|---|---|---|---|---|---|"
        printf '%s' "$SCALE_ROWS"
        echo
    fi
    echo "## B 能力（计分：$(basename "$MODEL")）"
    echo
    echo "> code 类用 \`code/code_model_taught.bin\`（出厂 \`model.bin\` 里没有代码知识）；其余九类用被测模型。"
    echo
    echo "| 类别 | 通过 | 总数 | 命中率 |"
    echo "|---|---|---|---|"
    cat "$W/cap.txt"
    echo "| **合计** | **$CAP_P** | **$CAP_N** | **$CAP_PCT%** |"
    echo
    if [ -s "$W/capfail.txt" ]; then
        echo "### 失败项"
        echo
        echo "| 类别 | 项 | 原因 | 实际回答（去空格，截断） |"
        echo "|---|---|---|---|"
        cat "$W/capfail.txt"
        echo
    fi
    if [ "$CAP2" -eq 1 ]; then
        echo "## B2 扩图后（同一套电池：model_post3.bin）"
        echo
        echo "| 类别 | 通过 | 总数 | 命中率 |"
        echo "|---|---|---|---|"
        cat "$W/cap2.txt"
        echo "| **合计** | **$CAP2_P** | **$CAP2_N** | **$CAP2_PCT%** |"
        echo
        if [ -s "$W/capfail2.txt" ]; then
            echo "### 扩图后失败项"
            echo
            echo "| 类别 | 项 | 原因 | 实际回答（去空格，截断） |"
            echo "|---|---|---|---|"
            cat "$W/capfail2.txt"
            echo
        fi
    fi
    echo "## C 边界（不计分）"
    echo
    echo "| 探测 | 输入 | 实际回答 |"
    echo "|---|---|---|"
    printf '%s' "$BND_ROWS"
} > "$REPORT"

echo
printf 'P=%s\nN=%s\nPCT=%s\nREL_US=%s\nLOAD_MS=%s\nSKIP=%s\n' \
    "$CAP_P" "$CAP_N" "$CAP_PCT" "$REL_US" "$base" "${SKIPPED:-0}" > "$W/score.txt"

echo "报告已写：${REPORT#$ROOT/}"
echo "合计：$CAP_P / $CAP_N（$CAP_PCT%）"
if [ "$CAP2" -eq 1 ]; then echo "扩图后：$CAP2_P / $CAP2_N（$CAP2_PCT%）"; fi
exit 0
