---
title: "读 Linux Cadence Endpoint 驱动：BAR、出站窗口和一笔 MSI"
date: 2026-11-27
updated: 2026-11-27
description: "从测试 Function 的 BAR 后端出发，沿 Endpoint Framework 追踪控制器如何接住主机写、访问主机内存和发出 MSI。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "Endpoint 固件"]
tags: ["pcie", "cadence", "linux", "code-analysis"]
permalink: "2026/11/27/pcie-cadence-register-programming/"
---

主机驱动可以通过 BAR 向卡上写一条命令。翻到卡端看，BAR 后面得有一块能接住这条命令的内存，写完还得有代码读懂它。Linux v6.6 的测试 Function `pci-epf-test` 和 Cadence Endpoint 驱动正好分担这两件事。沿着一条“主机下命令，设备复制数据，再通知主机”的路径，可以在公开源码里看到每一步由谁负责。

## 谁定义设备，谁控制硬件

Endpoint Function（EPF）决定设备对主机的样子。以上游测试 Function 为例，它规定命令寄存器的位置、源地址、目的地址、长度以及完成状态。换一种设备，寄存器约定就可能完全不同。Endpoint Controller（EPC）负责本地硬件，向上提供写配置头、设置 BAR、映射地址、发中断等操作。configfs 可以把 Function 实例绑定到选定的控制器。驱动之间用 Endpoint Framework 的 `pci_epc_*` 接口合作。[Linux Endpoint Framework 文档](https://docs.kernel.org/6.6/PCI/endpoint/pci-endpoint.html) · [上游测试 Function](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/endpoint/functions/pci-epf-test.c)

Cadence 驱动的入口是 `cdns_pcie_ep_setup()`。它映射控制寄存器，用 `devm_pci_epc_create()` 注册回调表，再初始化供出站访问使用的地址空间，并为发中断预留一个出站区域。此时 EPC 已就绪，Function 还要准备自己的配置头与 BAR 后端。当框架请求启动时，`cdns_pcie_ep_start()` 把启用的 Function 写给控制器并发起链路启动。主机的枚举发生在链路另一端。[Cadence EPC 的注册、启动与回调](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/cadence/pcie-cadence-ep.c)

## 主机写 BAR，卡上哪里会变

测试 Function 为 BAR 申请本地空间，随后调用 `pci_epc_set_bar()`。传给 EPC 的 BAR 描述既有编号、大小和属性，也有本地后端地址。Cadence 的 `cdns_pcie_ep_set_bar()` 一方面设置这个 BAR 能覆盖多大的区域、属于哪种资源，另一方面把后端地址写进入站翻译寄存器。等主机枚举时，主机才为该 BAR 分配 PCI 总线地址。两边设置的是同一次访问的两个端点。[EPC 接口实现](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/endpoint/pci-epc-core.c) · [Cadence 的 set_bar 回调](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/cadence/pcie-cadence-ep.c)

举个独立算例。假设 BAR0 在 PCI 地址空间的基址为 `0x8000_0000`，卡端为它准备的本地后端位于 `0x2000_0000`，窗口大小为 64 KiB。主机对 BAR0 偏移 `0x20` 写一个字，线上目的地址是 `0x8000_0020`，设备命中 BAR0 后把访问送到后端 `0x2000_0020`。若在卡端检查 Host 分配给 BAR 的那个地址，就会站错一侧。换一次开机，主机可能把 BAR 放到别处，命令寄存器在后端的偏移仍然是 `0x20`。

## 设备去主机内存复制数据

方向反过来以后，原来的 BAR 映射就帮不上忙了。在 `pci_epf_test_copy()` 里，测试 Function 先用 `pci_epc_mem_alloc_addr()` 分配一段本地出站窗口空间，得到 CPU 能做 I/O 访问的地址和该窗口的本地物理地址。随后调用 `pci_epc_map_addr()`，把窗口物理地址、命令中的远端 PCI 地址以及长度交给 EPC。源和目的各做一次映射。[Linux v6.6 测试 Function 的复制及错误处理](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/endpoint/functions/pci-epf-test.c)

| 此时手上的量 | 用它做什么 |
|---|---|
| `src_addr`、`dst_addr` | 卡端 CPU 使用的 I/O 虚拟地址。测试走 CPU 复制分支时，通过 `memcpy_fromio()` 和 `memcpy_toio()` 使用。 |
| `src_phys_addr`、`dst_phys_addr` | 卡端出站窗口的本地物理地址。EPC 需要它们来设置窗口的起点。 |
| `reg->src_addr`、`reg->dst_addr` | 主机在命令中提供的设备可访问地址，出站事务以这里为远端目标。 |

在控制器一侧，`cdns_pcie_ep_map_addr()` 从位图里找空闲出站区域，记录翻译，再登记占用。窗口用尽时，这一步会失败，尽管用于存放数据的内存可能很充足。测试 Function 的错误出口很能说明问题：映射源成功、映射目的失败，就要撤销已经生效的源映射并释放先前分配的本地窗口空间。成功传输以后也做同样的成对清理，下一笔才能复用这些资源。[Cadence 的地址映射与撤销](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/cadence/pcie-cadence-ep.c)

## 发 MSI 时，最后写到哪里

完成复制以后，测试 Function 可以经 `pci_epc_raise_irq()` 请求通知主机。我们取普通 MSI 这一支。Cadence 回调先读主机写入配置空间的 MSI Enable，再根据主机实际启用的向量数检查中断编号。该接口中的编号从 1 开始。检查通过后，驱动从 MSI Capability 取出消息地址与数据，为选定向量算出最终数据，按需要更新预留的出站区域，最后通过 `writel()` 发出这笔写。[Cadence 的 raise_irq 与 send_msi_irq](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/cadence/pcie-cadence-ep.c)

由此可以解释一个常见现象：主机能读写 BAR，设备也能更新完成状态，主机却没有收到中断。前两项已经验证了 BAR 后端和业务路径；发 MSI 还依赖主机启用该能力、申请的向量编号有效，以及那笔写能到达主机提供的消息地址。先核对 Host 和 Endpoint 看到的 MSI Enable、向量数与消息地址，再追主机上的 IRQ 处理函数，就可把排查范围缩到传递消息的那一段。[Host 侧的配置访问](/2026/11/27/pcie-cadence-core-architecture/) · [INTx、MSI 与 MSI-X 的差别](/2026/11/27/pcie-intx-msi-msix/)
