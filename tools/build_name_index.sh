#!/bin/sh
# tools/build_name_index.sh — 给一个分片目录建**名字索引**（§6.756）
#
# 为什么：`::parts corpus find` 现在是"逐片 gunzip + 查名表"（实测 0.1 秒/片，511 片 48 秒）。
# 名字索引把 `名字 → 分片文件名` 落到一个排序文本里 ⇒ find 变成一次 grep，**秒级 → 毫秒级**。
#
# 输出：<目录>/names.idx（每行 `<分片文件名>\t<节点名>`，按分片名+名字排序）
#
# 用法：sh tools/build_name_index.sh <分片目录> [dump 工具路径]
set -u
D=${1:?用法: sh tools/build_name_index.sh <分片目录>}
DUMP=${2:-/tmp/c7b2dump}
[ -x "$DUMP" ] || { echo "FAIL: 没有 c7b2dump（先 gcc tools/c7b2dump.c）"; exit 1; }
# 允许传"语料根目录"：自动下探 shards/ 子目录（实测第一次就传错了，索引建出 0 行）
[ -d "$D/shards" ] && D="$D/shards"
OUT="$D/names.idx"
: > "$OUT.tmp"
n=0
for f in "$D"/*.gz; do
    [ -f "$f" ] || continue
    b=$(basename "$f")
    gunzip -c "$f" > /tmp/_ni.c7b2 2>/dev/null || continue
    # ⚠ `$b` 必须加引号：不加时通配符多文件展开会让 awk 收到多个参数（实测索引里的名字全空）
    "$DUMP" /tmp/_ni.c7b2 --names 2>/dev/null | awk -v b="$b" '{print b "\t" $0}' >> "$OUT.tmp"
    n=$((n + 1))
done
rm -f /tmp/_ni.c7b2
LC_ALL=C sort "$OUT.tmp" > "$OUT"
rm -f "$OUT.tmp"
echo "== $D：$n 片 ⇒ $OUT（$(wc -l < "$OUT") 行，$(du -h "$OUT" | cut -f1)）=="
