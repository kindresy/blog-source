---
title: "INTx、MSI 与 MSI-X：中断怎样沿 PCIe 到达 CPU"
date: 2026-11-27
updated: 2026-11-27
description: "从传统 IRQ 直觉出发，解释消息中断、Capability、Table、Mask 和 Linux 投递路径。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "驱动实例"]
tags: ["pcie", "linux", "interrupt", "tutorial"]
permalink: "2026/11/27/pcie-intx-msi-msix/"
---

在示波器上看一颗 UART，常能找到一根单独的 IRQ 线。把一张卡插进 PCIe 插槽，卡要通知 CPU“接收队列有数据”，这条通知怎样穿过中间的 Switch？从链路分析仪上看，答案有两种样子。

| 机制 | PCIe 链路上的事务 | 向量 | 屏蔽位置 |
|---|---|---|---|
| INTx | `Assert_INTx` / `Deassert_INTx` Message | 四条虚拟线，可以共享 | Command 的 Interrupt Disable、设备自己的中断源 |
| MSI | 写入平台分配地址的 1 DW Memory Write | 最多 32 个，按 2 的幂分配 | 设备可选的 per-vector mask |
| MSI-X | 写入相应表项指定地址的 1 DW Memory Write | 最多 2048 个，可分别配置 | 每项的 Mask 位与 Function Mask |

## INTx 为什么成对出现

传统 PCI 中断有电平语义。PCIe 在链路上传递电平的**变化**。当 Endpoint 的中断条件成立，发出 Assert；驱动处理并清掉来源，设备撤销该条件时再发 Deassert。经过桥时，对应的虚拟引脚还可能重新对应到上游引脚（swizzle）。

共享中断说明这一点为什么重要。假设两张卡最终共用同一个 Host IRQ，A 清了自己的状态，B 的来源还在，线路仍可能有效。每个 handler 都应先读本设备的中断原因，属于自己才处理。如果在驱动里只做“来了就加一”，一次未清掉的电平足以把计数推得很高。

## MSI 的 Message 在哪儿

配置阶段，系统选择目标 CPU 或中断重映射路径，分配投递所用的 Message Address 和 Message Data。事件发生时，设备向该地址执行一次 Memory Write。抓包时应查 `MWr`。消息写属于 Posted Request，链路上通常能看到该写，却等不到逐次对应的事务层 Completion。[Linux MSI 驱动指南](https://docs.kernel.org/6.12/PCI/msi-howto.html)

```text
设备队列产生事件
  → 设备写入已分配的 MSI 地址，数据标识所选向量
  → Root Complex / 平台中断硬件处理这笔写
  → Linux IRQ 子系统调用设备驱动的 handler
```

MSI 的多个向量共用一套地址配置，设备通过允许变化的 Message Data 低位选择向量。分到 4 个向量时，可用低两位区分。Linux 驱动拿到的 IRQ 编号则供 `request_irq()` 使用。分析日志时，设备的“队列 3”、MSI 的数据字和 `/proc/interrupts` 左边的数字应分别记录。

## MSI-X 表放在设备的哪块地址里

配置空间里的 MSI-X Capability 指向 **Table** 和 **Pending Bit Array**。每个位置由 BAR 编号（BIR）与该 BAR 内的偏移共同描述。一个表项占 16 字节，保存地址的低、高 32 位、数据和向量控制字。PBA 用位记录被屏蔽时挂起的向量。[Linux 对 Capability、表项和位域的定义](https://github.com/torvalds/linux/blob/v6.12/include/uapi/linux/pci_regs.h)

举个纯算术的例子。假定 Host 为 BAR2 分配了总线地址 `0x90000000`，Table 在 BAR2 内偏移 `0x2000`，第 5 号表项（从 0 开始）位于 `0x90002050`。看清两笔方向相反的写，就能读懂 MSI-X。

1. **Host → EP**。Host 在设备 BAR 的表项里设置该向量的消息地址和数据。此时 `0x90002050` 是被写的表项地址。
2. **EP → Host**。业务发生后，若该向量可以投递，EP 把对应数据写到**表项里保存的消息地址**。这一笔写才请求 CPU 中断。

如果全局或该向量被屏蔽，相应的待处理状态可能记在 PBA。PBA 中每个向量只有一位，因此 handler 通常还需要检查设备自己的完成队列，才能知道积了几项工作。

## Linux 驱动按实际分到的数量建队列

最小的单队列示意如下。驱动已使设备处于中断源被屏蔽的状态，`handler` 和 `priv` 已准备好。

```c
int nvec, irq, ret;

nvec = pci_alloc_irq_vectors(pdev, 1, 1, PCI_IRQ_MSIX | PCI_IRQ_MSI);
if (nvec < 0)
        return nvec;

irq = pci_irq_vector(pdev, 0);
if (irq < 0) {
        pci_free_irq_vectors(pdev);
        return irq;
}
ret = request_irq(irq, handler, 0, "example-pcie", priv);
if (ret) {
        pci_free_irq_vectors(pdev);
        return ret;
}
```

成功后再开放设备中断源。驱动拆除时，先阻止新的设备事件、等待在途工作收尾，再 `free_irq(irq, priv)` 并 `pci_free_irq_vectors()`。多队列驱动如果请求 1 至 8 个向量，建队列时要以实际返回值为准。一个向量服务几条队列时，handler 得轮询这些队列的状态。

## 中断不来先看哪一端

先用 `lspci -vv -s <BDF>` 看当前启用的模式、表位置和向量容量，再看该设备实际分配的 IRQ 与 `/proc/interrupts` 的增长情况。如果设备有完成记录、Host 完全没有计数，可以核对业务中断源和向量是否仍被屏蔽。如果 Host 已经进了 handler，则继续核对完成记录所属队列、状态位的清除方式和缓冲区的 DMA 同步。

对按正常顺序完成 DMA 写再发 MSI 的常见设备，消息写排序使先前应有序的数据写先到达。设备若将相关写放在不同 Traffic Class，需要设备设计额外维持所需的先后关系。对 streaming DMA 缓冲区，驱动仍要按映射方向执行同步或解除映射，才能把数据交给 CPU。[Linux MSI 的顺序说明](https://docs.kernel.org/6.12/PCI/msi-howto.html) [Linux DMA 映射指南](https://docs.kernel.org/6.12/core-api/dma-api-howto.html)
