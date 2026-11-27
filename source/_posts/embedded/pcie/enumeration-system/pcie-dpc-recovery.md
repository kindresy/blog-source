---
title: "DPC：先隔离下游故障，再恢复设备"
date: 2026-11-27
updated: 2026-11-27
description: "解释 Downstream Port 的流量阻断、链路状态和软件恢复职责，并与 AER、热插拔区分。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-dpc-recovery/"
---

## 1. DPC 解决什么问题

Downstream Port Containment（DPC）用于隔离故障端口下游的流量。触发后，端口可以主动让链路进入 Disabled。调试时若只记下最后的 Link Down，就会丢掉最早触发隔离的原因。

DPC 与 AER 的分工：AER 记录/报告错误；DPC 执行 containment。两者经常一起工作，但“有 AER”不等于“实现 DPC”。

## 2. 触发后的硬件后果

```text
uncorrectable/fatal or configured trigger
 → Downstream Port DPC trigger/status/source capture
 → block/terminate downstream traffic
 → LTSSM directed to Disabled
 → outstanding requests按规则完成/终止
 → DPC interrupt/error signaling
 → software收集证据、reset/retrain/rescan/recover
```

因此 DPC 后看到 Link Disabled 是 containment 结果，不能只按普通 Link Not Up 重新训练并清光状态。应先读 DPC/AER status、source ID、header/prefix log。

## 3. 它和 Surprise Down/Hotplug 的区别

- Surprise Down：检测到 Link 非预期消失，是现象/错误类型。
- DPC：Port 主动隔离下游，是机制；可由多类错误触发。
- Hotplug remove：软件/用户有管理语义的设备移除流程。

一条日志可同时出现 AER fatal、DPC containment、Link down。时间顺序决定谁是根因、谁是保护动作。

## 4. Linux 恢复思路

1. freeze/quiesce 下游 driver I/O；
2. 保存 DPC/AER/Root Port config 与 topology；
3. 判断设备是否仍物理存在；
4. reset containment domain；
5. retrain Link；
6. 恢复/重新分配下游 configuration/resource；
7. driver slot reset/resume 或移除设备。

在 Linux v6.6，DPC 由 PCIe Port Service 驱动处理。中断路径读取 DPC 状态，恢复路径再与 PCI 错误恢复框架协作。平台上还要确认固件是否将这项服务的控制权交给操作系统。

## 5. 怎样判断这次掉链是否由 DPC 触发

先找出出问题的卡上游紧邻的 Downstream Port，检查该端口有没有 DPC Extended Capability。再对比 Trigger Status、Trigger Reason 和日志时间。普通的断电、热复位、链路训练失败也能让 Link 消失，只有 AER 记录或者一行 Link Down，还无法区分这几种情况。

## 6. Debug checklist

- DPC capability 是否真实存在、Trigger Enable/Reason/Status；
- 首个 AER error 与 severity；
- RP PIO/header/prefix log、Requester ID；
- Link 是错误先 down 还是 DPC 后被导向 Disabled；
- downstream outstanding NP/DMA 如何终止；
- reset 后 bridge bus/resource 与 IOMMU/IRQ 是否重建；
- 恢复失败时保留原状态，避免无限 clear-retrain loop。

## 7. 参考资料

- PCIe Base Specification 5.0 r1.0, §§2.9.3、6.2.10、DPC Extended Capability in Chapter 7。
- [Linux v6.6 的 DPC 服务](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/pcie/dpc.c)
