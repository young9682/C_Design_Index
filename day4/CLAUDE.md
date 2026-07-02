# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

实验四：简易自然语言文本单词与情感统计 — C语言文本分析程序

## Build & Run

```bash
# 编译
g++ -o word word.cpp

# 运行
./word
```

## Architecture

单一文件 C++ 程序 (`word.cpp`)，实现：
1. **输入处理** — 读取含标点的英文句子
2. **分词清洗** — 用 `strtok` 按多分隔符(` `, `,`, `.`, `?`, `!`)分词，转小写，过滤空串
3. **统计分析** — 单词总数、平均字符长度
4. **情感匹配** — 内置正向词(good, nice, happy等)和负向词(bad, sad, worse等)，判断情感倾向

## Output Format

```
===== 文本分析结果 =====
单词总数量：N
单词平均长度：X.XX
积极词汇出现次数：N
消极词汇出现次数：N
文本情感倾向：积极/消极/中性
```
