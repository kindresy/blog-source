---
title: "DPA：设备保持 D0 时怎样调整功耗预算"
date: 2026-11-27
updated: 2026-11-27
description: "用四档功耗预算解释 D0 内的动态功率分配，区分主机写入的目标和设备兑现的瓦数。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-dynamic-power-allocation/"
---

## 1. 它解决什么问题

传统 PCI PM 的 D0/D1/D2/D3 表达 Function 整体电源状态。很多设备在 D0 工作时也有很宽的功耗范围：加速卡可以全速跑，也可以降低频率、并发或启用单元，同时仍正常响应配置与业务。

Dynamic Power Allocation（**DPA，动态功率分配**）为这种场景定义标准控制面：软件选择一个 D0 内部 power substate，设备把实际功耗限制到该档声明的瓦数以内。

```text
PCI-PM D-state       Function整体电源状态
ASPM/L1SS            Link空闲省电状态
DPA substate         Function在D0内的功耗预算
DVFS/clock gating    设备实现预算的可能手段
```

DPA 不是 ASPM 或 Link speed。规范只定义预算、控制和状态；设备究竟降频还是关闭计算单元属于实现。

## 2. 最多 32 个逐级降低的预算档

- `Substate 0` 是 reset/default 状态，表示 Function 可能消耗的**最大功率**；
- 后续 substate 的 allocation 必须小于或等于前一档；
- 软件可在任意档间直接跳转；
- 所有 substate 中都必须响应 Configuration access；
- DPA 只适用于 D0，与 D1–D3 和 PCI-PM Capability 独立。

例如 25/20/12/5 W 四档。设备任何时刻都不能超过 `Substate Status` 所表示的预算。高→低转换时，Status 在完成前保持高档；低→高时，Status 必须在增加功耗前先报告高档。这样平台不会低估设备当前可能吃掉的电。

## 3. Capability 字段

DPA Extended Capability ID=`0x0016`，包含：

- `Substate_Max`：最大档号，即数量减一；
- Power Allocation Scale：8-bit allocation 乘 `10/1/0.1/0.01 W`；
- 两组 Transition Latency Value 和单位（1/10/100 ms）；
- 每档选择 latency 0 或 1 的 indicator；
- Power Allocation Array；
- `Substate Control`：Host 请求的目标档；
- `Substate Status`：设备当前承诺的档；
- `Substate Control Enabled`：Control 是否仍驱动转换。

Control Enabled 在 conventional reset/FLR 后由硬件置 1；Host 通过 RW1C 写 1 只能清掉，不能直接置回。清零后，对 Control 的写不再触发 DPA 转换，便于更高层软件协商机制接管。

软件可在一次转换未结束时再次改目标。设备完成动作后若 Status 与最新 Control 不同，必须继续转向最新目标，不能假设软件会耐心等上一笔。

## 4. 走一遍降档和升档

拿前面的四档教学算例，假设设备已经在 25 W 档工作。软件要求切到 12 W 时，可能需要先等一批在途任务结束，再减少正在运行的计算单元。若目标一写入就把状态改为 12 W，平台会提前把余下 13 W 分给别处。

| 阶段 | Control 请求 | Status 应代表的预算 | 此时可以做什么 |
|---|---|---|---|
| 降档刚开始 | 12 W 档 | 原 25 W 档 | 停止接入新的高功耗任务 |
| 降档已完成 | 12 W 档 | 12 W 档 | 平台可按新上限重新规划 |
| 请求升档 | 25 W 档 | 在提高实际功耗前先更新到较高档 | 提升频率或重新开放工作单元 |

表中瓦数是为了说明时序。软件写的是档号，瓦数要由该档的 Allocation 和 Scale 换算。VF 的功耗由 PF 的预算考虑，不能把它当成独立支持 DPA 的 Function 再算一次。

## 5. 一张档位表怎样才有用

同样是 12 W，设备可能降低频率，也可能保留频率、限制并发。PCIe 配置空间只能告诉主机设备承诺了什么，实际能否达到上限，要用相应负载与测量条件验证。一个适合上板核对的记录至少包含档号、声明上限、稳态实测、转换耗时四列。

## 6. Linux 怎样识别 DPA

上游 Linux 的 `pci_regs.h` 定义了 `PCI_EXT_CAP_ID_DPA` 和 `PCI_DPA_CAP_SUBSTATE_MASK`。软件先沿扩展能力链找 DPA，然后读取档位数量。字段定义能帮助解码，却不能告诉我们由哪段平台策略决定何时降档。

## 7. 目标写了，状态为什么没变

先核对 Substate Control Enabled 和目标档号。接着按设备声明的 Transition Latency 观察状态，再查设备有没有执行相应的频率或并发限制。若这几项都正常但整卡还是超预算，还要确认测的究竟是单个 Function、整颗芯片还是包含板上外围的输入功率。

## 8. 参考资料

- PCI Express Base Specification 5.0：§6.16 pp.604–606；§7.9.12 pp.941–946；§9.3.7.12 VF 规则。
- [Linux v6.6 的 PCI 配置字段定义](https://github.com/torvalds/linux/blob/v6.6/include/uapi/linux/pci_regs.h)
