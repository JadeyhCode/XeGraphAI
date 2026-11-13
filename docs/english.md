# 读英语：`[syn] cat=猫` —— 一行一个词，走同一条推理

> 一句话：**分词器本来就认 ASCII 串**（`ai.h` 里 `ascii_pair = a<128 && b<128 && isalnum(a) && isalnum(b)`），
> 所以英语**读**不需要新代码：把英文词映射到已有的中文节点，后面全是同一条关系推理。

## 实测

```
[syn] cat=猫   [syn] dog=狗   [syn] bird=鸟   [syn] tea=茶   ...
cat 属于 动物 吗      ⇒ 对 ， 猫 -[属于]-> 动物
bird 会 飞 吗         ⇒ 对 ， 鸟 -[会]-> 飞
cat 属于 植物 吗      ⇒ 不 对 ， 猫 -[不 属于·独占]-> 植物
zebra 属于 动物 吗    ⇒ 我 不 知道 ……（没教过的词不瞎认）
```

`tests/english.sh`（11 项）判两件事：
1. **同一件事，英文问和中文问答复首字一致**（9 组）—— 不是"另写一套英文逻辑"；
2. **没教过的英文词不许瞎认**（`zebra`/`giraffe` 必须老实说不知道）。

## 写：英语问 ⇒ 英语答（`english.h`）

读是数据，写是代码（但只写"是非问"——这个内核不生成文本）：

```
Is a cat an animal?   ⇒ Yes. Cat is an animal.
Can a bird fly?       ⇒ Yes. Bird can fly.
Is a cat a plant?     ⇒ No. Cat is not a plant.
Can a fish fly?       ⇒ No. Fish cannot fly.
Does a cat eat fish?  ⇒ Yes. Cat eats fish.
Does a zebra eat meat?⇒ I don't know whether Zebra eats meat.
```

**关系符号的绑定也是数据**（`patterns.txt`，和 `[alias] 拥有=有` 同一个机制）：

```txt
[alias] en=can=会,is=属于,are=属于,eat=吃,eats=吃,have=有,has=有,like=喜欢,likes=喜欢,need=需要,needs=需要,fear=怕,fears=怕
```

`english.h` 只留**英语语法**（`eat→eats`、`cannot`、冠词/倒装解析）；"哪个英文词算哪个核心关系"
一律走 `rel_id_of`（`[alias]` 是唯一判据）。消融判据：把这行摘掉，`Can a bird fly?` 立刻答不出
（`tests/english.sh` ④ 钉住）。

做法：把英语句子解析成 `<A> <关系> <B>`，A/B 经 `[syn]` 映射回中文节点，
**直接调 `rel_solve`**（和中文走同一条推理），再把结论写成英文。
肯定没有就再问一次否定（`鱼 不 会 飞` 是这么答出 `No` 的）。
`tests/english.sh` 第二段 8 项钉住 Yes/No/不知道 三种说法 + **开放问句**的英文指引：
`What is a cat?` ⇒ `Sorry, I only answer yes/no questions about what is in my graph. …`
（不这样的话，中文流水线会把英文开放问句答成"我也想养一只"——比明说更糟）。

## 语言镜像：英文进 ⇒ 英文出，中文进 ⇒ 中文出

用户明确要的一条规则，`main.c` 里一层小闸门兜底：

- 纯英文输入（含 `?` 的是非问）⇒ 英文答复；**任何**英文输入都不会掉中文答复
  （接不住时给英文说明，例如 `hello` ⇒ `Sorry, I only answer yes/no questions in English. …`）；
- 中文输入（含夹英文词，如 `cat 属于 动物 吗`）⇒ 中文答复，一条都没改；
- 英文输入**不交给中文流水线**：它会把英文句当陈述学进去（实测把 `a` 登记成动态关系，
  第三轮就答崩）。

判据：`tests/english.sh` ③b（英文进/中文进各两条，按"答复里有没有非 ASCII"判）。

## 边界（别把"读"当"写"）

- **写只做是非问**：`What is a cat?` 这类开放问句仍走中文流水线（答中文）；不硬造自由英语。
- `[syn]` 是**子串替换**：`cat` 会命中 `cattle`。所以只收不嵌套的词；
  真要扩词表，得先按词边界匹配（那是分词器的活）。
- 词表是**数据**（`patterns.txt`），一行一个词，随时可加、可删；不重编译。
- 映射到的中文节点必须**在图里**（有事实），否则替换完照样是"不知道"——
  这是对的：英文只是"说法"，知识还是图里的知识。

## 下一步（如果要把"写"也做掉）

1. 判句：输入只含 ASCII ⇒ 走英文答复模板（`Yes, a cat is an animal.` / `No.`）；
2. 关系词也要英文表（`is a`=属于、`can`=会、`has`=有…）；
3. 判据沿用 `tests/english.sh`：英文问、英文答，且**中文回归零掉**。
