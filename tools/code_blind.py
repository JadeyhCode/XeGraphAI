#!/usr/bin/env python3
"""tools/code_blind.py — C 合成的**盲测**（不按图里的关键词写描述）。

为什么要另开一份：
  tools/code_hidden.py 的 heldout 虽然"写 harness 时还没教"，但描述是我按图里的关键词
  习惯写的——它量的是"这一层覆盖"，不是"换个人换个说法还认不认"。
  这里刻意避开已登记的关键词，用日常说法描述同样的任务；另加一组**负样本**
  （图里根本不会的任务），看系统是**老实说不会**，还是**硬答一个错的**。

判据：
  · PARAPHRASE：PASS = 生成并跑对；REFUSE = NO_MATCH（老实说不会）；WRONG = 答错。
  · NEGATIVE ：REFUSE = NO_MATCH（期望）；WRONG = 硬答（最坏，会误导人）。

用法：python3 tools/code_blind.py
"""
import os, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(ROOT, "tools"))
import code_hidden as ch  # 复用 run_synth / judge 的编译与运行逻辑

MODEL = os.path.join(ROOT, "code/code_model.bin")

PARAPHRASE = [
    dict(name="各位数字相加", desc="写一个程序：从命令行给它一个整数，把它每一位上的数字加起来打印出来。",
         kind="argv", data=["1234"], want="10"),
    dict(name="三个数里最小的", desc="写一个程序：给它三个整数，打印其中最小的那个。",
         kind="argv", data=["3", "9", "4"], want="3"),
    dict(name="把一行字反过来", desc="写一个程序：读一行字，把它反过来写出来。",
         kind="stdin", data=["abc\n"], want="cba"),
    dict(name="是不是质数", desc="写一个程序：给一个整数，告诉我它是不是质数。",
         kind="argv", data=["97"], want="1"),
    dict(name="一到一百的和", desc="写一个程序：打印 1 到 100 的和。",
         kind="argv", data=[], want="5050"),
    dict(name="偶数有多少个", desc="写一个程序：输入若干整数，输出其中偶数有多少个。",
         kind="stdin", data=["1 2 3 4\n"], want="2"),
    dict(name="空格换成逗号", desc="写一个程序：把一句话里的空格换成逗号。",
         kind="stdin", data=["a b\n"], want="a,b"),
    dict(name="一组数的平均值", desc="写一个程序：对一组数求平均值。",
         kind="stdin", data=["1 2 3\n"], want="2.00"),
    dict(name="两个数里大的", desc="写一个程序：输出两个数里大的那个。",
         kind="argv", data=["2", "9"], want="9"),
    dict(name="整数倒过来", desc="写一个程序：把整数倒过来写。",
         kind="argv", data=["123"], want="321"),
]

NEGATIVE = [
    dict(name="链表反转", desc="写一个程序：把单链表反转后输出。", kind="argv", data=[], want="?"),
    dict(name="二叉树层序", desc="写一个程序：对二叉树做层序遍历。", kind="argv", data=[], want="?"),
    dict(name="复制文件", desc="写一个程序：把一个文件复制成另一个文件。", kind="argv", data=[], want="?"),
    dict(name="多线程求和", desc="写一个程序：用两个线程分别求一半数组的和再合并。", kind="argv", data=[], want="?"),
    dict(name="最长公共子序列", desc="写一个程序：求两个字符串的最长公共子序列长度。", kind="argv", data=[], want="?"),
    dict(name="正则匹配", desc="写一个程序：用正则表达式匹配邮箱。", kind="argv", data=[], want="?"),
    dict(name="归并排序", desc="写一个程序：用归并排序给数组排序。", kind="argv", data=[], want="?"),
    dict(name="目录遍历", desc="写一个程序：递归列出一个目录下所有文件名。", kind="argv", data=[], want="?"),
]


def probe_negative(t):
    """负样本：只需要知道"有没有硬答"。没有产物 = 老实拒绝；有产物 = 硬答。"""
    path, out = ch.run_synth(MODEL, t["desc"])
    if not path:
        return "NO_MATCH", "", out
    return "WRONG", "(生成了程序)", out


def run_group(name, tasks, want_refuse):
    print(f"== {name} ==")
    stats = {"pass": 0, "refuse": 0, "wrong": 0, "compile": 0}
    for t in tasks:
        status, got, out = (probe_negative(t) if want_refuse else ch.judge(t, MODEL))
        if status == "NO_MATCH":
            stats["refuse"] += 1
            print(f"  [REFUSE] {t['name']}")
        elif status == "PASS":
            stats["pass"] += 1
            print(f"  [PASS]   {t['name']}")
        elif status == "COMPILE_FAIL":
            stats["compile"] += 1
            print(f"  [CFAIL]  {t['name']}  得到: {got[-80:]!r}")
        else:
            stats["wrong"] += 1
            print(f"  [WRONG]  {t['name']}  得到: {got!r} 期望: {t['want']!r}")
            line = next((l for l in out.splitlines() if "[合成]" in l), "")
            if line:
                print(f"           合成: {line.strip()[:140]}")
    print(f"  => pass {stats['pass']} / refuse {stats['refuse']} / wrong {stats['wrong']} / cfail {stats['compile']}")
    return stats


def main():
    if not os.path.exists(MODEL):
        print(f"FAIL: 缺模型 {MODEL}（先跑 sh code/build_code_model.sh）")
        return 2
    p = run_group("paraphrase（同样的任务，换一种说法）", PARAPHRASE, False)
    n = run_group("negative（图里不会的任务，期望老实说不会）", NEGATIVE, True)
    print("----")
    print(f"改写：认对 {p['pass']} / 老实拒绝 {p['refuse']} / 答错 {p['wrong']}")
    print(f"负样本：老实拒绝 {n['refuse']} / 硬答 {n['wrong']}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
