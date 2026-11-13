#!/bin/sh
# tools/scorecard.sh — **一张表跑完所有基准**（§6.792）
#
# 为什么要有：分数散在五个地方（自写电池 / ARC-AGI-1 train·eval / ARC-AGI-2 / HLE / SWE-bench），
# 每次报告都要翻五个文件。这条命令跑一遍、写 `_bench/scorecard.md`、并在终端打一张表。
#
# 口径：每一行都复用对应工具自己的判据（不在这里另写一套），所以数不会两处不一致。
#
# 用法：sh tools/scorecard.sh [--fast]    # --fast 跳过 HLE/SWE（各要 1~3 分钟）
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
FAST=0
[ "${1:-}" = "--fast" ] && FAST=1
OUT="$ROOT/_bench"; mkdir -p "$OUT"
AGENT="$ROOT/build_aip"; MODEL="$ROOT/model.bin"
[ -x "$AGENT" ] || { echo "缺 build_aip：先编译"; exit 1; }
[ -s "$MODEL" ] || { echo "缺 model.bin"; exit 1; }

say() { printf '[scorecard] %s\n' "$*" >&2; }
line_self=""; line_a1t=""; line_a1e=""; line_a2=""; line_hle=""; line_swe=""

say "① 自写电池（70 条，十类）…"
sh "$ROOT/tools/benchmark.sh" "$MODEL" --fast > "$OUT/sc_bench.log" 2>&1 || true
if [ -s "$OUT/score.txt" ]; then
    P=$(sed -n 's/^P=//p' "$OUT/score.txt"); N=$(sed -n 's/^N=//p' "$OUT/score.txt")
    PCT=$(sed -n 's/^PCT=//p' "$OUT/score.txt")
    line_self="| 自写电池（十类） | $P / $N | **$PCT%** |"
else
    line_self="| 自写电池（十类） | — | 跑失败 |"
fi

say "② 引擎内 ARC（train / eval / ARC-AGI-2）…"
for s in arc1-train arc1-eval arc2-eval; do
    python3 "$ROOT/tools/arc_engine_bench.py" --set "$s" > "$OUT/sc_$s.log" 2>&1 || true
done
grab_arc() { sed -n '3p' "$OUT/arc_engine_$1.md" 2>/dev/null | sed 's/.*网格逐格相等 \*\*\([0-9]*\)\*\*（\([0-9.]*\)%）.*/\1 \2/p'; }
set -- $(grab_arc arc1-train); line_a1t="| ARC-AGI-1 train | $1 / 400 | **${2:-?}%** |"
set -- $(grab_arc arc1-eval);  line_a1e="| ARC-AGI-1 eval | $1 / 400 | **${2:-?}%** |"
set -- $(grab_arc arc2-eval);  line_a2="| ARC-AGI-2 eval | $1 / 120 | **${2:-?}%** |"

if [ "$FAST" -eq 0 ]; then
    say "③ HLE（2158 道文本题，约 2.5 分钟）…"
    python3 "$ROOT/tools/hle_bench.py" > "$OUT/sc_hle.log" 2>&1 || true
    H=$(sed -n 's/.*答对（严格）\([0-9]*\)\/\([0-9]*\)（\([0-9.]*\)%）.*/\1 \2 \3/p' "$OUT/hle_summary.md" | head -1)
    line_hle="| HLE（文本题） | $(printf '%s' "$H" | cut -d' ' -f1) / $(printf '%s' "$H" | cut -d' ' -f2) | **$(printf '%s' "$H" | cut -d' ' -f3)%** |"
    say "④ SWE-bench Lite（300 题，约 1 分钟）…"
    python3 "$ROOT/tools/swebench_bench.py" > "$OUT/sc_swe.log" 2>&1 || true
    SW=$(sed -n 's/.*产出可应用补丁（unified diff） | \*\*\([0-9]*\)\/\([0-9]*\)\*\*.*/\1 \2/p' "$OUT/swebench_summary.md" | head -1)
    line_swe="| SWE-bench Lite（补丁） | $(printf '%s' "$SW" | cut -d' ' -f1) / $(printf '%s' "$SW" | cut -d' ' -f2) | **0%**（resolved 定义） |"
fi

{
    echo "# 基准成绩单（$(date '+%Y-%m-%d %H:%M')）"
    echo
    echo "| 基准 | 通过 / 总数 | 命中率 |"
    echo "|---|---|---|"
    printf '%s\n' "$line_self" "$line_a1t" "$line_a1e" "$line_a2" "$line_hle" "$line_swe" | grep -v '^$'
    echo
    echo "- 模型：\`$(basename "$MODEL")\`｜二进制 \`build_aip\`"
    echo "- 每行都由对应工具自己的判据产出（自写电池 = tools/benchmark.sh；ARC = tools/arc_engine_bench.py；"
    echo "  HLE = tools/hle_bench.py；SWE = tools/swebench_bench.py）"
    echo "- **不能把这些百分比相加或平均**：分母与判据都不同（见 docs/benchmark-*.md 的口径说明）"
    [ "$FAST" -eq 1 ] && echo "- 本次是 \`--fast\`：HLE / SWE 两行没跑"
} > "$OUT/scorecard.md"

cat "$OUT/scorecard.md"
