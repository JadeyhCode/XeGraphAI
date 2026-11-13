#!/bin/sh
# tests/bench.sh — **基准分不准掉**（尺子是 tools/benchmark.sh，电池在 tests/fixtures/bench/）
#
# 这一套不测"新功能"，它把基准**当回归门**用：改了任何东西之后，只要这个系统
# 在自己声明支持的十类上掉分、或者图查询从 O(1) 退化成扫图，这里就红。
#
# 判据（全部是行为/数值，不是"看起来像"）：
#   ① 能力：命中率 ≥ 90%，且通过条数 ≥ 60（分母是**实跑条数**；沙箱里没有代码模型时 code 类自动跳过）
#   ② 图查询 `::rel` ≤ 60 µs（41 198 边上实测 12–13 µs；扫图的实现会是几百 µs 到 ms 级）
#   ③ 载入 ≤ 400 ms（实测 27 ms；留 15 倍余量，只拦数量级退化）
#
# 为什么不卡精确值：同一台机器重复跑，一问一答在 5.1–6.3 ms、灌图 ±20% 抖动。
# 卡精确值只会让套件变成"看运气"。要精确画像就跑 `sh tools/benchmark.sh`（出报告，不进回归）。
#
# 用法：sh tests/bench.sh [模型]   默认 $ROOT/model.bin
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }

[ -x "$ROOT/build_aip" ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x "$ROOT/build_aip" ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
[ -s "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }

# 只跑能力（--fast 跳过规模扫描，省 ~15 s）
# ⚠ stdout 不能重定向进 _bench/：benchmark.sh 开头 `rm -rf _bench` 会把正在写的日志一起删掉
LOG=$(mktemp); trap 'rm -f "$LOG"' EXIT
sh "$ROOT/tools/benchmark.sh" "$MODEL" --fast > "$LOG" 2>&1
rc=$?
grep -a 'A 速度' "$LOG" | head -1
sed -n '/^  \[FAIL\]/p' "$LOG" | head -6
tail -2 "$LOG"

[ "$rc" -eq 0 ] || { bad "benchmark.sh 没跑通（rc=$rc）" "$(tail -3 "$LOG" | tr '\n' ' ')"; }
S="$ROOT/_bench/score.txt"
if [ -s "$S" ]; then
    P=$(sed -n 's/^P=//p' "$S"); N=$(sed -n 's/^N=//p' "$S")
    PCT=$(sed -n 's/^PCT=//p' "$S"); REL=$(sed -n 's/^REL_US=//p' "$S")
    LOAD=$(sed -n 's/^LOAD_MS=//p' "$S"); SKIP=$(sed -n 's/^SKIP=//p' "$S")
else
    bad "没有 _bench/score.txt" "benchmark.sh 没写出机器可读结果"
    P=0; N=0; PCT=0; REL=999; LOAD=9999; SKIP=0
fi

printf '  能力 %s/%s（%s%%）｜ ::rel %s µs ｜ 载入 %s ms ｜ 跳过 %s 条\n' \
    "${P:-?}" "${N:-?}" "${PCT:-?}" "${REL:-?}" "${LOAD:-?}" "${SKIP:-?}"

awk -v p="${PCT:-0}" 'BEGIN{exit !(p >= 90)}' \
    && ok "能力命中率 ≥ 90%（$PCT%）" \
    || bad "能力命中率 < 90%（$PCT%）" "$(sed -n '/^  \[FAIL\]/p' "$LOG" | head -4 | tr '\n' ' ')"
[ "${P:-0}" -ge 60 ] && ok "通过条数 ≥ 60（$P）" || bad "通过条数 < 60（$P）" "见 $LOG"
awk -v r="${REL:-999}" 'BEGIN{exit !(r <= 60)}' \
    && ok "图查询 ≤ 60 µs（$REL µs）" || bad "图查询 > 60 µs（$REL µs）" "查图退化成扫图了？"
awk -v l="${LOAD:-9999}" 'BEGIN{exit !(l <= 400)}' \
    && ok "载入 ≤ 400 ms（$LOAD ms）" || bad "载入 > 400 ms（$LOAD ms）" "冷启动退化了？"

printf '\n基准：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
