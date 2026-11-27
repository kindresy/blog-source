---
title: "ARI 与 SR-IOV：从多 Function 到 PF、VF"
date: 2026-11-27
updated: 2026-11-27
description: "理解扩展 Function 编号、VF 资源分配与隔离，区分标准机制和平台启用条件。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "基础架构"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-ari-sriov/"
---

## 1. Multi-Function 先天只有 8 个 Function 编号

传统 Routing ID 是：

```text
[ Bus 8 bit ][ Device 5 bit ][ Function 3 bit ]
```

同一个 Device Number 下只有 Function 0–7。ARI（Alternative Routing-ID Interpretation）把低 8 bit 改解释为一个 8-bit Function Number：

```text
[ Bus 8 bit ][ Function 8 bit ]
```

同一个 ARI Device 因而最多 256 Functions，Device Number 概念在该段被消除。上游紧邻的 Downstream Port 必须支持并由 software enable ARI Forwarding，否则 extended Function 无法正确发现。

来源：Base Spec §6.13，printed/PDF pp.590–592。

## 2. SR-IOV 为什么出现

用软件模拟设备时，一些寄存器访问需要退出虚拟机，交给后端处理。队列和中断越忙，这部分开销越明显。SR-IOV 让一颗物理 PCIe Device 暴露：

- **PF（Physical Function）**：完整、可信管理功能，含 SR-IOV Extended Capability；
- **VF（Virtual Function）**：轻量 Function，保留直接 data movement 所需的 queue/BAR/DMA/MSI-X 资源，可直通给 VM/System Image。

PF driver/virtualization manager 分配 device-internal resources、创建 VF；VF 在 Linux 看起来仍是独立 `pci_dev`，有唯一 Requester ID，可绑定 VF driver/VFIO、经过 IOMMU 隔离。

## 3. VF 不是纯软件影子

VF 至少要有真正可区分的：

- Configuration Space view；
- Routing/Requester ID；
- VF BAR aperture 中属于自己的 slice；
- queue/context；
- DMA/IOMMU identity；
- interrupt vector；
- FLR/reset边界。

SR-IOV 只标准化 discover/configure 外壳，内部 queue、scheduler、bandwidth/QoS、PF↔VF mailbox 仍由 device-specific design 决定。

## 4. VF BAR 怎样分配

同一 PF 的一组 VF 共享一套 VF BAR register 描述。software 做 size probing 并为一组 VF 预留连续的地址窗口；第一个 VF 的 base 加上每 VF aperture stride，得到后续 VF 的 space。

这不是每个 VF 都有六个完全独立、随意分配的实体 BAR register。VF MSE 也在 SR-IOV control 中统一控制相关 VF Memory Space。

## 5. 创建 VF 的概念流程

```text
enumerate PF
 → discover SR-IOV capability: TotalVFs/InitialVFs/offset/stride/VF Device ID/page size
 → reserve/assign VF BAR resource
 → configure NumVFs + VF MSE + VF Enable
 → Device instantiates VF config/routing identity
 → PCI core discovers/adds VF pci_dev
 → IOMMU group/domain + MSI-X
 → bind VF driver or assign to VM
```

关闭时要先从 guests/drivers 收回 VF、停止 DMA/IRQ，再 clear enable；不是简单把 sysfs number 写 0 就可以无视业务 ownership。

## 6. Linux 边界

Linux 把一个已经启用的 VF 当作可独立绑定驱动的 PCI 设备。PF 的设备目录下，`sriov_totalvfs` 给出可用上限，`sriov_numvfs` 记录启用数量。排查“明明有 PF，为什么没有 VF”时，还要看 PF 驱动是否实现了 `sriov_configure`，以及主机有没有资源容纳这些 VF。

## 7. 安全和 debug

- 每个 VF 是否有独立 Requester ID/IOMMU mapping；
- PF reset/link reset 是否连带所有 VF；
- VF FLR 是否真正只清该 VF queue/context；
- VF BAR slice/stride 是否正确，越界是否能碰相邻 VF；
- MSI-X table/vector 是否按 VF 隔离；
- PF mailbox 输入是否验证 VF identity/length；
- ARI forwarding 只在确认下游是 ARI Device 后打开，错误 enable 可能产生 function alias。

## 8. 参考资料

- PCI Express Base Specification 5.0 r1.0, §6.13, printed/PDF pp.590–592。
- PCI Express Base Specification 5.0 r1.0, Chapter 9, printed/PDF pp.1099–1168。
- [Linux 内核文档：PCI Express I/O Virtualization](https://docs.kernel.org/PCI/pci-iov-howto.html)
