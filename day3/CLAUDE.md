# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## 项目概述

**智能共享充电宝租借柜系统** - C 语言实验项目，模拟共享充电宝的租借、归还、充电和计费流程。

## 编译与运行

```bash
# 编译
gcc -o shared_power_bank.exe shared_power_bank.cpp -lm

# 运行
./shared_power_bank.exe
```

## 需求文档

详见 `plan.md`，核心要求：

- N 个槽位（用户启动时输入），每个初始空闲、电量 100%
- 三种状态：空闲、租借中、充电中
- 费率用户自定义（元/小时）
- 实时模拟（sleep(1) 每秒更新）

## 架构设计

### 数据结构

- `State` 枚举：`STATE_AVAILABLE`, `STATE_RENTED`, `STATE_CHARGING`
- `PowerBank`：id, state, battery(0-100%), rental_start, charge_start
- `Cabinet`：banks 指针, total(槽位数), price_per_hour

### 函数模块

| 函数 | 职责 |
|------|------|
| `init_cabinet()` | 用户输入 N 和费率，初始化所有充电宝 |
| `display_status()` | 显示所有充电宝编号、状态、电量 |
| `rent_power_bank()` | 从空闲中随机分配，记录租借时间 |
| `return_power_bank()` | 校验编号，计算费用，随机电量(20%-90%)，决定下一状态 |
| `update_charging()` | 每秒 +0.25%，充电中状态 +20%/时间推进，≥100% 变空闲 |
| `calculate_fee()` | 按时长和费率计费，不足1小时按1小时 |

### 主循环

```
display_status → update_charging → 显示菜单 → sleep(1) → 处理用户输入
```

### 关键业务规则

- **租借**: 找所有空闲充电宝 → 随机选一个 → 状态改 RENTED → 记录时间
- **归还**: 输入编号 → 校验存在且为 RENTED → 随机电量(20%-90%) → ≥50% 变空闲，<50% 变充电中 → 计算费用
- **充电**: 每秒 +0.25%；充电中状态每次时间推进 +20%；≥100% 变空闲
- **计费**: `(当前时间 - rental_start) / 3600.0 * price_per_hour`，向上取整到小时
