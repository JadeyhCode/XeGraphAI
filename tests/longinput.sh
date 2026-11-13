#!/bin/sh
# tests/longinput.sh — **长/杂输入不许崩**（§6.778）
#
# 来历：外部基准（HLE 87/2158、SWE-bench 13/300）里那 100 条"无回答"，追下去不是"它不说"，
# 而是**进程 SIGSEGV**（ASan 定位：frame.h `fr_generate` 往 `v[96]` 里拷整句，
# 只给"拆解"加了界、"合并"没加 ⇒ 栈上越界写）。基准里每条一进程，所以崩溃只表现成"没回答"。
#
# 这一套就是那个洞的回归：喂长英文/长中文/代码围栏/两万字符单行，要求
#   ① 退出码 0（不崩）  ② 真的有回答（"AI > " 那行且非空）。
#
# 用法：sh tests/longinput.sh [模型]
set -u
ROOT=$(cd "$(dirname "$0")/.." && pwd)
MODEL=${1:-$ROOT/model.bin}
cd "$ROOT"
[ -x ./build_aip ] || sh "$ROOT/tests/prebuilt.sh" aip "$ROOT/main.c" -std=c11 -O2 -Wall -Wextra
[ -x ./build_aip ] || { echo "FAIL: 编不出 build_aip"; exit 1; }
[ -s "$MODEL" ] || { echo "FAIL: 缺模型 $MODEL"; exit 1; }

W="$ROOT/_t/longinput"; rm -rf "$W"; mkdir -p "$W"
pass=0; fail=0
ok()  { printf '  [OK]   %s\n' "$1"; pass=$((pass + 1)); }
bad() { printf '  [FAIL] %s\n         证据: %s\n' "$1" "${2:-}"; fail=$((fail + 1)); }

# 跑一条：$1=名字 $2=输入文件
run_case() {
    name=$1; f=$2
    ./build_aip --model "$MODEL" < "$f" > "$W/out.txt" 2>&1
    rc=$?
    n=$(grep -ac '^AI > ' "$W/out.txt" || true)
    # 'AI > ' 后面必须还有内容（空回答也算没答）
    nonempty=$(grep -a 'AI > ' "$W/out.txt" | grep -vc 'AI > *$' || true)
    if [ "$rc" -ne 0 ]; then
        bad "$name：退出码 $rc（崩了）" "$(tail -2 "$W/out.txt" | tr '\n' ' ')"
    elif [ "$nonempty" -lt 1 ]; then
        bad "$name：没有非空回答（$n 条 AI 行，全是空）" "$(tail -2 "$W/out.txt" | tr '\n' ' ')"
    else
        ok "$name（rc=0，回答 $nonempty 条）"
    fi
}

# ① 真实崩过的那一句（Django 的一个 issue 前 70 个词，606 字符）
cat > "$W/en_crash.txt" <<'EOF'
Migration auth.0011_update_proxy_permissions fails for models recreated as a proxy. Description (last modified by Mariusz Felisiak) I am trying to update my project from Django 2.0.13 to 2.1.8 and I am facing a failure in the last migration. The migration auth.0011_update_proxy_permissions fails with an error because the models are recreated as proxy models. The problem is that the migration tries to update the permissions of the proxy models, but the content type of the original model was changed. This means that when the migration runs, it tries to re-create already existing entries in the auth_permission table, and the problem still occurs with other proxy models.
EOF
echo '::quit' >> "$W/en_crash.txt"
run_case "英文·真实崩过的 606 字符" "$W/en_crash.txt"

# ② 2 万字符单行（英文单词堆叠）
awk 'BEGIN{ for (i = 0; i < 4000; i++) printf "model field table error value "; print ""; print "::quit" }' > "$W/en_long.txt"
run_case "英文·2 万字符单行" "$W/en_long.txt"

# ③ 中文 200 句拼接（长行 + 重复）
awk 'BEGIN{ for (i = 0; i < 200; i++) printf "这是一个很长的中文句子用来测试帧生成是否会越界"; print ""; print "::quit" }' > "$W/zh_long.txt"
run_case "中文·6000 字符单行" "$W/zh_long.txt"

# ④ 代码围栏 + 表格（真实 issue 里的形状：``` 与 | 混合）
cat > "$W/mixed.txt" <<'EOF'
```
Traceback (most recent call last):
  File "astropy/io/ascii/qdp.py", line 71, in _line_type
    raise ValueError(f"Unrecognized QDP line: {line}")
ValueError: Unrecognized QDP line: read serr 1 2
```
| key | value |
|---|---|
| err | 1 |
::quit
EOF
run_case "代码围栏 + 表格" "$W/mixed.txt"

printf '\n长输入：%d 过 / %d 红\n' "$pass" "$fail"
[ "$fail" -eq 0 ]
