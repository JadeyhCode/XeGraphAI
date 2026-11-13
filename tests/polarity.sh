#!/bin/sh
# tests/polarity.sh — **极性一致**：矛盾要报、动态关系也要能问依据（§6.795）
#
# 三个判据（都是"账实不符"那一类）：
#   ① 关掉改口语义（`::revise off`）后教「矛盾兽 属于 动物」+「矛盾兽 不 属于 动物」，
#      `::contradict` 必须报**极性矛盾**（以前只找"同一个词属于两个互不包含的类"，这里报 0 —— 账实不符）；
#   ② 动态关系（`拿`）也要能 `::why`：肯定式答真、否定式答真、反过来的答假；
#   ③ 动态关系也要能 `::der`（现场构造推导链，不许只回"用法"）。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
W="$ROOT/_t/polarity"; rm -rf "$W"; mkdir -p "$W"
cp "$MODEL" "$W/m.bin"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }
run() { printf '%b\n::quit\n' "$1" | ./build_aip --model "$W/m.bin" 2>&1; }
has() { printf '%s' "$2" | grep -q -- "$1"; }

echo "== ① 极性矛盾要报（关掉改口语义才留得住两条）=="
o=$(run '::revise off\n::teach 矛盾兽 属于 动物\n::teach 矛盾兽 不 属于 动物\n::contradict')
has '极性矛盾' "$o" && ok "::contradict 报出极性矛盾" || bad "没报极性矛盾" "$(printf '%s' "$o" | grep -a '矛盾' | head -3 | tr '\n' ' ')"
has '矛盾兽 属于 动物 与 矛盾兽 不 属于 动物' "$o" && ok "点名了具体那一对" || bad "没点名" "$(printf '%s' "$o" | grep -a '极性矛盾' | head -1)"

echo "== ①b 改口语义开着时不该留两条（也不该报）=="
o=$(run '::teach 另一兽 属于 动物\n::teach 另一兽 不 属于 动物\n::contradict')
printf '%s' "$o" | grep -q '另一兽 属于 动物 与' && bad "改口语义开着却留下两条" "$(printf '%s' "$o" | grep -a '另一兽' | head -2)" \
    || ok "改口语义开着：旧值被替掉，没有极性矛盾"

echo "== ② 动态关系能问依据（::why）=="
o=$(run '::teach 乙 拿 红球\n::why 乙 拿 红球')
has '乙 拿 红球 ？ → 真' "$o" && ok "::why 乙 拿 红球 → 真" || bad "动态 why 失败" "$(printf '%s' "$o" | grep -a '问\]\|用法' | head -2)"
o=$(run '::teach 乙 不 拿 蓝球\n::why 乙 不 拿 蓝球\n::why 乙 拿 蓝球')
has '乙 不 拿 蓝球 ？ → 真' "$o" && ok "::why 乙 不 拿 蓝球 → 真" || bad "动态否定 why 失败" "$(printf '%s' "$o" | grep -a '问\]' | head -2)"
has '乙 拿 蓝球 ？ → 假' "$o" && ok "::why 乙 拿 蓝球 → 假" || bad "动态 why 极性" "$(printf '%s' "$o" | grep -a '问\]' | head -2)"

echo "== ③ 动态关系能现场构造推导（::der）=="
o=$(run '::teach 乙 拿 红球\n::der 乙 拿 红球')
has '真值=真' "$o" && ok "::der 乙 拿 红球 得到真值=真" || bad "::der 不支持动态关系" "$(printf '%s' "$o" | grep -a '用法\|推导' | head -2)"

echo "== ④ 否定式也能构造推导（::der 乙 不 拿 蓝球）=="
o=$(run '::teach 乙 不 拿 蓝球\n::der 乙 不 拿 蓝球\n::der 乙 拿 蓝球')
printf '%s' "$o" | grep -q '真值=真' && ok "否定式推导 → 真" || bad "否定式推导没到真" "$(printf '%s' "$o" | grep -a '推导\|真值' | head -2)"
printf '%s' "$o" | grep -q '真值=假' && ok "反过来问 → 假" || bad "反向没到假" "$(printf '%s' "$o" | grep -a '真值' | head -2)"

echo "== ⑤ 动态关系覆盖极性要说出来（不静默）=="
o=$(run '::teach 丙 拿 红球\n::teach 丙 不 拿 红球\n::rel 丙')
printf '%s' "$o" | grep -q '只存一个极性' && ok "覆盖时给出注意" || bad "覆盖静默" "$(printf '%s' "$o" | grep -a '注意\|教会' | head -3)"

printf '\n极性一致：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
