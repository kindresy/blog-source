---
title: "PCIe 电源管理：D-state、ASPM、L1SS 与 PME"
date: 2026-11-27
updated: 2026-11-27
description: "把 Function 与 Link 的两套状态分开，理解进入低功耗、唤醒及操作系统协作。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-power-aspm-pme/"
---

本篇聚焦 Function D-state、Link ASPM/L1SS 与 PME。Function 在 D0 内部按标准功耗预算分档的机制见 [Dynamic Power Allocation (DPA)](/2026/11/27/pcie-dynamic-power-allocation/)。

## 1. 先拆坐标轴

PCIe power management 至少有三套状态：

| 状态族 | 对象 | 例子 | 谁主要控制 |
|---|---|---|---|
| D-state | PCI Function | D0、D1、D2、D3hot、D3cold | software 经 PM Capability，外加平台 power |
| L-state | 一条相邻 Link | L0、L0s、L1/L1.0/L1.1/L1.2、L2/L3 Ready | LTSSM/DLL 协议 + ASPM/PCI-PM |
| System state | 整个平台 | S0、sleep 等 | ACPI/平台规范，非 Base Spec 本章完整定义 |

一个 EP 可有多个 Function，但只有一条上游 Link。把某 Function 写到 D3hot，不等于“LTSSM 寄存器被写成 L1”；硬件会综合同一组件所有 Function 状态、在途 traffic 和两端协议，决定 Link 怎样过渡。

## 2. Function D-state

- **D0**：正常工作、最高功能和功耗；D0 uninitialized 与 fully initialized 在配置语义上还可区分。
- **D1/D2**：可选的中间省电状态，保留更多 context、恢复快于 D3。
- **D3hot**：主电仍在、Function 大部分不可操作；Configuration Space 的有限字段仍可访问。
- **D3cold**：Function 无主电，配置访问不可用；回归需要平台恢复 power，通常相当于更彻底的重新初始化。

每个 Function 的 PM Capability（传统 capability）声明支持状态、PME 能力，并提供 PMCSR：PowerState、PME Enable、PME Status 等。软件在改变 D-state 前必须满足设备和 driver 的 quiesce 要求；如果设备仍在 DMA，直接进入 D3hot 可能中断在途传输并留下不完整的 descriptor 状态。

来源：Base Spec 5.0 §§5.1、5.3.1、7.5.2，printed/PDF pp.439、444–449、712–716。

## 3. Link L-state

### L0

正常 active，TLP/DLLP 都可传。

### L0s

低退出延迟、较浅的 standby；TX/RX 方向可非对称。main power、refclk、PLL 保持，适合短空闲。不是所有 refclock 架构都支持。

### L1 / L1.0

更深、更高 exit latency；整个 Link 双向进入，TLP/DLLP 停止。ASPM 可在双方仍为 D0 时自治进入；PCI-PM 则可因下游所有 Function 离开 D0 而进入。

### L1.1 / L1.2

可选的 L1 PM Substates：

- L1.1 维持 Link common-mode，能进一步关 clock；
- L1.2 连 common-mode 也不要求维持，省电更深、恢复要求更严格；
- 退出由 `CLKREQ#` assertion 触发。

### L2/L3 Ready、L2、L3

L2/L3 Ready 是移除 main power/refclock 前的握手阶段。若仍保留 Vaux 或唤醒逻辑，关闭主电源后抽象为 L2；完全无电源则为 L3。它们不等同于一个始终运行的 LTSSM 状态，因为主电源移除后，维持完整状态机运行的条件通常已经不存在。

来源：Base Spec §§5.1–5.2，printed/PDF pp.439–443。

## 4. ASPM 和 PCI-PM 是两条进入路径

### ASPM（Active State Power Management）

双方 Function 仍可处于 D0。software 只配置 policy/capability；Link 空闲后 hardware 根据协商 autonomously 进入 L0s/L1，traffic 出现又退出。它追求 runtime idle power。

### PCI-PM software-compatible path

软件把 Function 写入 D1/D2/D3hot，组件据此请求 Link L1；系统进一步 power down 时，Root Complex 用 PME_Turn_Off / PME_TO_Ack 与所有下游协调，再进入 L2/L3 Ready。

所以：“ASPM 开了”不代表 device 进入 D3；“runtime suspend 到 D3hot”也不等于必然使用 ASPM L1.2。

## 5. L1 entry/exit 在协议上发生什么

以 ASPM L1 为例：

```text
Link idle
 → 下游组件（例如 EP 的 Upstream Port）在满足空闲条件后暂缓发送新 TLP
 → PM_Active_State_Request_L1 DLLP
 → 上游组件（例如 Root Port）检查是否可进入
    ├─ PM_Active_State_Nak Message：拒绝，恢复 L0
    └─ Ack + electrical idle sequence
 → LTSSM L1.Entry → L1.Idle

任一侧出现 traffic/explicit exit
 → electrical idle exit / FTS or Recovery
 → L0
```

进入低功耗前必须处理 ordering、outstanding transaction、receive buffer 和 wake latency。不能简单地直接关闭 clock；协议需要确保两端完成必要的握手，并妥善处理仍在传输或等待返回的事务。

## 6. PME：唤醒和事件通知是两步

PME（Power Management Event）用于设备请求系统恢复/处理事件：

1. 若主 power/refclock 已关，先通过 WAKE#/Beacon 等 form-factor/platform mechanism 让资源重新可用；
2. Link 恢复后，Function 发送 `PM_PME` Message TLP 到 Root Complex；
3. Root Port/RC 记录 PME source 并通知 system software；
4. software 读取/清 PMCSR.PME Status，恢复 Function。

`PME_Turn_Off` 则是 RC 向下游广播“准备断主电”，`PME_TO_Ack` 是上游 Port 汇聚后的确认。它们和 PM_PME 名字相近、方向/目的却不同。

## 7. L1.2 退出时，先恢复什么

假设网络设备在空闲时进入 L1.2。这时来了一笔需要 DMA 的数据。设备要请求恢复时钟，两端的模拟电路和参考时钟也要有足够的恢复时间，链路回到可以传输的状态后，请求才能到达主机。

这会造成一种有辨识度的现象：持续流量测出的延迟很好，一段空闲后的第一笔访问却明显更慢。可以把首次访问和同一批随后的访问分开统计，同时记录两端的 L1SS 配置。若访问引发了退出低功耗，循环读寄存器本身也会改变所观察的空闲模式。

## 8. Linux driver 的职责边界

稳定的概念层：

- PCI core/PM framework 保存配置、选择 D-state、设置 wake/PME，并协调 platform runtime/system sleep；
- driver 的 runtime/system suspend callback 必须停新请求、关业务 IRQ、quiesce DMA、保存 device-specific context；
- resume 恢复 power/config/BAR/device context，再重开 queue/interrupt；
- ASPM policy 通常由 PCI core/platform 控制，普通 function driver 不应随意改 Link Control 影响整条 Link；
- `pci_set_power_state()` 只处理 PCI Function 的标准状态转换，不替驱动停止硬件 DMA。

Linux 将 Function 的 runtime suspend/resume 与链路上的 ASPM 策略分开处理。排查哪一次切换带来了延迟时，既要看 Function 的 power state，也要读相邻两端的 Link Control 和 L1SS Control。

## 9. Debug

### 性能正常，启用省电后间歇超时

1. 禁用 ASPM/L1SS 做 A/B，不把它当最终修复；
2. 查 Link Control 两端 enable 是否一致、advertised exit latency；
3. 查链路空闲和退出的时刻，结合 CLKREQ#、参考时钟和 PHY 恢复时间；
4. 校验 T_POWER_ON/common-mode timing 与实际 PHY/refclock generator；
5. 查 outstanding request/Completion 是否在进入前正确处理；
6. 查设备从产生请求到真正发出第一笔 TLP 的时间；
7. 看是否频繁 L1→Recovery→Detect，而不是正常 L1→L0。

### runtime resume 后 BAR/DMA 不工作

1. Function 是 D3hot 还是 D3cold，配置 context 是否丢失；
2. Command MSE/BME、BAR、MSI/MSI-X 是否恢复；
3. IOMMU/DMA mappings 与 descriptor 是否仍有效；
4. 检查这段时间是否发生过 Link Down、FLR 或上游复位；
5. device-specific clocks/resets/FIFO 是否由 driver/platform 恢复。

## 10. 参考资料

功耗“状态”之外，平台还要处理设备需求报告、slot 额度、D0 内动态分配和 central-resource latency hint，分别见[Slot Power Limit 与 Power Budgeting](/2026/11/27/pcie-slot-power-budget/)、[Dynamic Power Allocation](/2026/11/27/pcie-dynamic-power-allocation/)和[LTR 与 OBFF](/2026/11/27/pcie-ltr-obff/)。

- PCI Express Base Specification 5.0 r1.0, Chapter 5, printed/PDF pp.439–494。
- PCI Express Base Specification 5.0 r1.0, §7.5.2, printed/PDF pp.712–716。
- [Linux 内核的 PCI 电源管理文档](https://docs.kernel.org/power/pci.html)
