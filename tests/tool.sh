#!/bin/sh
# tests/tool.sh — **外部工具**（本地大模型）：问得到、标出处、不进图、失败不装懂
#
# 为什么要有这一套：`::tool` 会往网络发请求，最容易出三类错 ——
#   ① 默默把工具的话当成自己的知识（污染）；
#   ② 请求体发错（curl 的 `--data-binary` 要 `@文件`，漏了会把**路径**当 JSON 发出去，
#      服务端只回 "parse error at line 1, column 1"，本地看不出问题）；
#   ③ 失败时编一句话糊过去。
# 这三条都是可判定的，所以钉死：用 `tools/fake_openai.py` 起一个假端点，不联网。
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=${BIN:-./build_aip}
MODEL=${1:-model.bin}
[ -x "$BIN" ] || { echo "FAIL: 缺可执行文件 $BIN"; exit 1; }
[ -f "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }

W="$ROOT/_t/tool"; rm -rf "$W"; mkdir -p "$W"
cp "$BIN" "$W/build_aip"; cp "$MODEL" "$W/model.bin"
# 端口让**内核分配**（--port 0）：写死端口在并行跑时会撞上别的进程（实测撞到一个
# 要 API Key 的本地服务，答复 401，4 项判据全红）。
python3 "$ROOT/tools/fake_openai.py" --port 0 --reply "假端点的答复：北京" \
    --dump "$W/body.json" > "$W/server.log" 2>&1 &
SRV=$!
trap 'kill '"$SRV"' 2>/dev/null' EXIT
PORT=""
i=0
while [ "$i" -lt 50 ]; do
    # 取第一行里的数字（`sed 's/[^0-9]//gp'` 在"整行都是数字"时不会触发替换 ⇒ 拿不到，踩过）
    PORT=$(head -1 "$W/server.log" 2>/dev/null | tr -cd '0-9')
    [ -n "$PORT" ] && break
    i=$((i + 1)); sleep 0.1
done
[ -n "$PORT" ] || { echo "FAIL: 假端点没起来"; exit 1; }

run() { ( cd "$W" && printf '%b\n::quit\n' "$1" | ./build_aip --model model.bin 2>&1 ); }
pass=0; fail=0
ok()  { echo "  [OK]   $1"; pass=$((pass + 1)); }
bad() { echo "  [FAIL] $1"; echo "         证据: $2"; fail=$((fail + 1)); }

echo "== ① 默认关：没开就问，必须拒绝 =="
o=$(run '::tool ask 你好')
printf '%s' "$o" | grep -q '工具没打开' && ok "默认关：拒绝（工具没打开）" || bad "默认关没拒" "$(printf '%s' "$o" | grep -a '外部工具' | head -2)"

echo "== ② 配上 + 打开：问得到，且**标出处** =="
o=$(run "::tool set http://127.0.0.1:$PORT/v1 k m\n::tool on\n::tool ask 你好")
printf '%s' "$o" | grep -q '假端点的答复' && ok "答复回来了" || bad "没拿到答复" "$(printf '%s' "$o" | grep -a '外部工具' | tail -2)"
printf '%s' "$o" | grep -q '不是我的知识' && ok "标了「不是我的知识」" || bad "没标出处" "$(printf '%s' "$o" | tail -3)"

echo "== ③ 请求体是 JSON（查 curl 的 @文件 那个坑）=="
if [ -s "$W/body.json" ]; then
    head -c 1 "$W/body.json" | grep -q '{' && ok "请求体以 { 开头（是 JSON）" || bad "请求体不是 JSON" "$(head -c 80 "$W/body.json")"
else
    bad "没有请求体落盘" "body.json 不存在"
fi

echo "== ④ 账：tool.log 有一行 ok =="
[ -f "$W/tool.log" ] && grep -q 'ok' "$W/tool.log" && ok "账里记了 ok" || bad "账里没记" "$(cat "$W/tool.log" 2>/dev/null | head -2)"

echo "== ⑤ 不进图：答复内容不能变成知识 =="
o=$(run '::rel 假端点的答复：北京')
printf '%s' "$o" | grep -q '不在图里' && ok "答复没进图" || bad "答复进图了" "$(printf '%s' "$o" | head -3)"

echo "== ⑥ 失败照实说（指向死端口）=="
o=$(run '::tool set http://127.0.0.1:1/v1 k m\n::tool on\n::tool ask 你好')
printf '%s' "$o" | grep -q '没答上来' && ok "失败照实说（没答上来）" || bad "失败时不说实话" "$(printf '%s' "$o" | tail -2)"
printf '%s' "$o" | grep -q '假端点的答复' && bad "失败却给了答复（编的）" "$(printf '%s' "$o" | tail -2)" || ok "失败时不编答案"

echo ""
if [ "$fail" -ne 0 ]; then echo "FAIL tool（$pass 过 / $fail 红）"; exit 1; fi
echo "PASS tool（$pass 过 / 0 红）"
