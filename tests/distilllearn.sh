#!/bin/sh
# tests/distilllearn.sh — **教师只给说法变体，学生自己裁决真假**（骨架蒸馏的判据）
#
# 背景（上一版怎么错的）：让 0.6B 直接生成"骨架 + 知识"，它产出
#   `水 属于 水`（自环）、`[问] <X> 音乐`（不是问句），靠弱门放行 ⇒ 分数是假的。
# 改法：教师只贡献**语言变体**（礼貌前缀/句尾），学法是仓库已有的 `[lead]` / `[tail]`
# 数据行；真假由"包装前后答复是否一致"来裁决 —— 这不需要信教师。
#
# 三条判据（同一份二进制、同一个模型，只有 patterns.txt 不同）：
#   ① 基线：`前缀 + 问句` 有一部分答不出（=真缺口）；
#   ② 学后：同一批全部答得出；
#   ③ 举一反三：**没教过**的问句套同一个前缀，也必须答得出。
#
# 用法：sh tests/distilllearn.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=${BIN:-./build_aip}
MODEL=${1:-model.bin}
DATA=$ROOT/tests/fixtures/distill/wrappers.txt
[ -x "$BIN" ] || { echo "FAIL: 缺可执行文件 $BIN"; exit 1; }
[ -f "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }
[ -f "$DATA" ] || { echo "FAIL: 缺数据 $DATA"; exit 1; }

W="$ROOT/_t/distilllearn"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/model.bin"
for f in relations.txt frames.txt patterns.txt lexicon.txt relprops.txt; do
    [ -f "$ROOT/$f" ] && cp "$ROOT/$f" "$W/$f"
done
cp "$BIN" "$W/build_aip"

# 基线问句（模型本来就答得出的事实）与留出问句（用来判"举一反三"）
BASE="猫 属于 动物 吗|狗 属于 动物 吗|鱼 属于 动物 吗|花 属于 植物 吗"
HOLD="猫 有 毛 吗|猫 会 抓老鼠 吗|鸟 会 飞 吗|猫 怕 狗 吗"

answered() {   # 读答复里有没有**兜底**口气
    # ⚠ 带空格的兜底说法（"不 太 懂"）必须加引号：不加会被 shell 按空格切成两个词 ⇒ 语法错误
    case "$1" in
    *不太懂*|*"不 太 懂"*|*不知道*|*"不 知道"*|*没学过*|*还没学过*|*答不上*|*不确定*|*真不会*|*"这个我"*)
        return 1 ;;
    esac
    [ -n "$1" ]
}
ask() { ( cd "$W" && printf '%s\n::quit\n' "$1" | ./build_aip --model model.bin 2>&1 ) | sed -n 's/.*AI > //p' | tail -1; }

# 取数据里的前缀（去掉注释与空行）
grep -v '^#' "$DATA" | grep '^\[lead\]' | sed 's/^\[lead\] *//' > "$W/leads.txt"
n_lead=$(wc -l < "$W/leads.txt")

probe() {   # $1 = 问句集合（| 分隔）；输出 "答得出数 总数"
    pass=0; tot=0
    printf '%s\n' "$1" | tr '|' '\n' > "$W/qs.txt"
    while IFS= read -r q; do
        [ -n "$q" ] || continue
        while IFS= read -r p; do
            [ -n "$p" ] || continue
            out=$(ask "$p $q")
            tot=$((tot + 1))
            answered "$out" && pass=$((pass + 1))
        done < "$W/leads.txt"
    done < "$W/qs.txt"
    echo "$pass $tot"
}

# 基线必须**把这些蒸馏行摘掉**，否则仓库早就学会了，判据没有缺口可测。
cp "$W/patterns.txt" "$W/patterns.all" 2>/dev/null || : > "$W/patterns.all"
grep -v '^#' "$DATA" | sed 's/^\[lead\] *//; s/^\[tail\] *//' > "$W/words.txt"
awk 'NR==FNR{w[$0]=1;next}
     { split($0,a," ");
       if ((a[1]=="[lead]"||a[1]=="[tail]") && (a[2] in w)) next;
       print }' "$W/words.txt" "$W/patterns.all" > "$W/patterns.base"
cp "$W/patterns.base" "$W/patterns.txt"
B=$(probe "$BASE"); b_pass=${B% *}; b_tot=${B#* }
echo "[基线] 前缀+问句 答得出：$b_pass/$b_tot（缺口 $((b_tot - b_pass))）"

# 学：把仓库里的 patterns.txt（含蒸馏行）放回去 —— 只差这几行
cp "$W/patterns.all" "$W/patterns.txt"

A=$(probe "$BASE"); a_pass=${A% *}; a_tot=${A#* }
echo "[学后] 前缀+问句 答得出：$a_pass/$a_tot"
H=$(probe "$HOLD"); h_pass=${H% *}; h_tot=${H#* }
echo "[举一反三] 教过的前缀 + **没教过**的问句：$h_pass/$h_tot"

echo ""
echo "== 知识：教师提议只进待证，**确认后**才进事实层 =="
cat > "$W/claims.txt" <<'CLAIMS'
# 蒸馏提议（结构门只挡自环/重复/池外；语义真假没人验过）
游泳 属于 运动
唱歌 属于 音乐
做饭 属于 食物
CLAIMS
k=0
o=$( ( cd "$W" && printf '::claim ingest claims.txt\n::claim\n::rel 游泳\n::quit\n' | ./build_aip --model model.bin 2>&1 ) )
printf '%s' "$o" | grep -q '收进 3 条' && echo "  [OK]   收进待证 3 条" || { echo "  [FAIL] 没进待证"; k=1; }
printf '%s' "$o" | grep -q '来源：教师' && echo "  [OK]   来源标「教师」" || { echo "  [FAIL] 来源没标"; k=1; }
printf '%s' "$o" | grep -q '（还没有）' && echo "  [OK]   确认前**不进事实层**" || { echo "  [FAIL] 确认前就进事实层了"; k=1; }
o=$( ( cd "$W" && printf '::claim ingest claims.txt\n::claim accept 1\n游泳 属于 运动 吗\n::quit\n' | ./build_aip --model model.bin 2>&1 ) )
printf '%s' "$o" | grep -q '按你说的算数' && echo "  [OK]   确认后升成事实" || { echo "  [FAIL] 确认没生效"; k=1; }
printf '%s' "$o" | sed -n 's/.*AI > //p' | grep -q '^对' && echo "  [OK]   升完能答「对」" || { echo "  [FAIL] 升完答不出"; k=1; }

fail=0
[ "$k" -eq 0 ] || fail=1
[ "$b_pass" -lt "$b_tot" ] || { echo "  ✗ 基线没有缺口（判据失效：这批前缀本来就认）"; fail=1; }
[ "$a_pass" -eq "$a_tot" ] || { echo "  ✗ 学后没全过（$a_pass/$a_tot）"; fail=1; }
[ "$h_pass" -eq "$h_tot" ] || { echo "  ✗ 留出问句没全过（$h_pass/$h_tot）—— 没做到举一反三"; fail=1; }
[ "$n_lead" -ge 3 ] || { echo "  ✗ 前缀太少（$n_lead）"; fail=1; }
[ "$fail" -eq 0 ] || { echo "FAIL distilllearn"; exit 1; }
printf 'PASS distilllearn（前缀 %d 个：基线 %d/%d → 学后 %d/%d；留出 %d/%d）\n' \
    "$n_lead" "$b_pass" "$b_tot" "$a_pass" "$a_tot" "$h_pass" "$h_tot"
