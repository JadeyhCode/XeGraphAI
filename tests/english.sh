#!/bin/sh
# tests/english.sh — **读英语**（读得懂，不等于会写）：ASCII 词 → 中文节点，走同一条推理
#
# 机制是数据：`patterns.txt` 里的 `[syn] cat=猫`（分词器本来就把 ASCII 串当一个词）。
# 所以判据只有两条：
#   ① 同一件事，用英文问和用中文问，**答复一致**（不是"另写一套英文逻辑"）；
#   ② **没教过的英文词不许瞎认**（`zebra` 必须说"不知道"，不能因为像 animal 就答"对"）。
#
# 用法：sh tests/english.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=${BIN:-./build_aip}
MODEL=${1:-model.bin}
[ -x "$BIN" ] || { echo "FAIL: 缺可执行文件 $BIN"; exit 1; }
[ -f "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }

W="$ROOT/_t/english"; rm -rf "$W"; mkdir -p "$W"
cp "$BIN" "$W/build_aip"; cp "$MODEL" "$W/model.bin"
for f in relations.txt frames.txt patterns.txt lexicon.txt relprops.txt; do
    [ -f "$ROOT/$f" ] && cp "$ROOT/$f" "$W/$f"
done
ask() { ( cd "$W" && printf '%s\n::quit\n' "$1" | ./build_aip --model model.bin 2>&1 ) | sed -n 's/.*AI > //p' | tail -1; }

pass=0; fail=0
ok()  { echo "  [OK]   $1"; pass=$((pass + 1)); }
bad() { echo "  [FAIL] $1"; echo "         证据: $2"; fail=$((fail + 1)); }
head_of() { printf '%s' "$1" | sed 's/^ *//' | cut -c1-1; }

# ① 英文问 vs 中文问：答复的首字必须一致（对/不/大/估/我……）
while IFS='|' read -r en zh; do
    case "$en" in '#'*|'') continue ;; esac
    a=$(ask "$en")
    b=$(ask "$zh")
    [ -n "$a" ] && [ -n "$b" ] || { bad "$en" "空答复"; continue; }
    case "$a" in
    *"不太懂"*|*"不 太 懂"*|*"不知道"*|*"不 知道"*|*"还没学过"*)
        bad "$en" "英文问句掉兜底：$a" ;;
    *)
        if [ "$(head_of "$a")" = "$(head_of "$b")" ]; then ok "$en ≡ $zh（$a）"
        else bad "$en ≢ $zh" "$a  ≠  $b"; fi ;;
    esac
done <<'PAIRS'
cat 属于 动物 吗|猫 属于 动物 吗
dog 属于 动物 吗|狗 属于 动物 吗
fish 属于 动物 吗|鱼 属于 动物 吗
bird 属于 动物 吗|鸟 属于 动物 吗
tea 属于 饮料 吗|茶 属于 饮料 吗
student 属于 人 吗|学生 属于 人 吗
cat 有 毛 吗|猫 有 毛 吗
bird 会 飞 吗|鸟 会 飞 吗
cat 属于 植物 吗|猫 属于 植物 吗
PAIRS

# ② 英语**答**：只做是非问（Yes/No/I don't know + 一句陈述）
echo ""
echo "== 英语问 ⇒ 英语答（只做是非问；答案仍来自图）=="
chk() {
    a=$(ask "$1")
    case "$a" in
    $2) ok "$1 ⇒ $a" ;;
    *)  bad "$1" "期望：$2　实际：$a" ;;
    esac
}
chk 'Is a cat an animal?' 'Yes. Cat is an animal.'
chk 'Can a bird fly?'      'Yes. Bird can fly.'
chk 'Is a cat a plant?'    'No. Cat is not a plant.'
chk 'Can a fish fly?'      'No. Fish cannot fly.'
chk 'Does a cat eat fish?' 'Yes. Cat eats fish.'
chk 'Does a zebra eat meat?' "I don't know whether Zebra eats meat."

chk 'What is a cat?' 'Sorry, I only answer yes/no questions about what is in my graph.*'
chk 'Who is a cat?'   'Sorry, I only answer yes/no questions about what is in my graph.*'

# ③ 没教过的英文词：不许瞎认（可以"不知道"，但不许答"对"）
for w in zebra giraffe; do
    a=$(ask "$w 属于 动物 吗")
    case "$a" in
    *"不知道"*|*"不 知道"*|*"不太懂"*|*"不 太 懂"*|*"还没学过"*)
        ok "没教过的 $w 老实说不知道" ;;
    *) bad "没教过的 $w 乱答" "$a" ;;
    esac
done

# ③b 语言镜像：英文进 ⇒ 英文出；中文进 ⇒ 中文出（用户明确要求的一条规则）
echo ""
echo "== 语言镜像 =="
for q in 'hello' 'cat is an animal' 'What is a cat?'; do
    a=$(ask "$q")
    printf '%s' "$a" | LC_ALL=C grep -q '[^ -~]' && bad "英文进却答了非英文" "$q ⇒ $a" \
        || ok "英文进 ⇒ 英文出（$q）"
done
for q in '你好' '猫 属于 动物 吗'; do
    a=$(ask "$q")
    printf '%s' "$a" | LC_ALL=C grep -q '[^ -~]' && ok "中文进 ⇒ 中文出（$q）" \
        || bad "中文进却答了英文" "$q ⇒ $a"
done

# ④ **消融**：关系符号的绑定是数据（`[alias] en=can=会,…`），不是代码。
#    把那行摘掉，英文是非问必须**答不出来**（否则说明绑定写死在 english.h 里）。
echo ""
echo "== 消融：摘掉 [alias] 那行，英文问答必须退化 =="
D="$W/noalias"; rm -rf "$D"; mkdir -p "$D"
cp "$W/build_aip" "$D/"; cp "$W/model.bin" "$D/"; cp "$W/relations.txt" "$D/" 2>/dev/null
cp "$W/frames.txt" "$D/" 2>/dev/null; cp "$W/lexicon.txt" "$D/" 2>/dev/null
cp "$W/relprops.txt" "$D/" 2>/dev/null
grep -v '^\[alias\] en=' "$W/patterns.txt" > "$D/patterns.txt"
a=$( ( cd "$D" && printf 'Can a bird fly?\n::quit\n' | ./build_aip --model model.bin 2>&1 ) | sed -n 's/.*AI > //p' | tail -1 )
case "$a" in
Yes.*) bad "摘掉 [alias] 还能答" "说明绑定写死在代码里：$a" ;;
*)     ok "摘掉 [alias] 就答不出（绑定确实是数据）" ;;
esac

echo ""
if [ "$fail" -ne 0 ]; then echo "FAIL english（$pass 过 / $fail 红）"; exit 1; fi
echo "PASS english（$pass 过 / 0 红）"
