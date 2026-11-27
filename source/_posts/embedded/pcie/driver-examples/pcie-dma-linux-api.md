---
title: "PCIe DMA 与 Linux DMA API：引擎、地址和缓存"
date: 2026-11-27
updated: 2026-11-27
description: "区分搬运引擎、总线主设备与 DMA API，解释 coherent、streaming、IOMMU 和数据所有权。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "驱动实例"]
tags: ["pcie", "dma", "linux", "tutorial"]
permalink: "2026/11/27/pcie-dma-linux-api/"
---

驱动里常有两个看起来都像地址的变量。一个叫 `buf`，CPU 用它往缓冲区里填数据；另一个叫 `dma_addr`，要写进设备的描述符。这两个数在一台机器上碰巧相同，换到有 IOMMU 的服务器上就可能完全两样。沿着一次 Host 把数据交给 Endpoint 的过程看，区别最直观。

## 谁真正搬数据

假设 Host 有一块 8 KiB 的发送缓冲区，Endpoint 要把它读走。

| 动作 | 谁负责 | 可以在哪里看见 |
|---|---|---|
| 让设备能够寻址这块内存 | Host 驱动调用 Linux DMA API，平台建立映射或执行必要的缓存维护 | 驱动拿到一个 `dma_addr_t` |
| 决定什么时候读、读多长 | 设备的 DMA 引擎或业务逻辑 | 队列、描述符、门铃和完成状态 |
| 把读请求与返回数据送过链路 | PCIe Requester、Completer 和中间的链路 | Memory Read（MRd）请求和带数据的 Completion（CplD） |

操作系统允许 Function 主动发起事务，要设置 PCI Command 中的 Bus Master Enable，Linux 驱动使用 `pci_set_master()`。DMA 引擎什么时候开始取描述符，另外由设备的队列协议决定。于是，一个已经能读 BAR、`lspci` 也显示 `BusMaster+` 的设备，仍可能因为队列尚未启动而一笔数据都没搬。

PCIe 传递 Memory Request，至于 DMA 引擎长在哪里、用环还是链表、几条队列共用一个中断，得看设备实现。与 SoC 内部的内存搬运相比，这里还多了一条 PCIe 链路。上例的 8 KiB 表示软件要传的总长，线上会有多个 MRd/CplD。Requester 要考虑 MRRS 和 4 KiB 地址边界，单个读请求的返回数据也可能分成几个 Completion。

## 把地址和方向一起交给设备

使用 DMA API 之前先根据**设备真实寻址能力**设置 DMA mask 并检查返回值。例如驱动可调用 `dma_set_mask_and_coherent()`。能从配置空间读出一个 64 位 BAR，并不能据此决定 DMA mask。BAR 描述 Host 能把设备映射到哪里，DMA mask 描述设备能访问哪些地址。[Linux DMA 映射指南](https://docs.kernel.org/6.12/core-api/dma-api-howto.html)

下面是单次发送的映射片段。假定 `buf` 是来自适合 `dma_map_single()` 的连续内存，`len` 已经检查，`req` 随该请求存活到完成。

```c
dma_addr_t addr = dma_map_single(&pdev->dev, buf, len, DMA_TO_DEVICE);

if (dma_mapping_error(&pdev->dev, addr))
        return -EIO;

req->dma_addr = addr;
req->len = len;
```

后续描述符填入 `addr`，CPU 继续用原来的 `buf` 变量管理这块内存。尤其不能把内核虚拟地址拿去填描述符。即便驱动能算出 CPU 物理地址，总线窗口、IOMMU 映射或者平台使用的中转缓冲区也可能改变设备看到的地址。

| DMA API 的方向 | 数据流向 | 典型的 EP 事务 |
|---|---|---|
| `DMA_TO_DEVICE` | 设备读取 Host 已准备好的数据 | EP 发 MRd，接收 CplD |
| `DMA_FROM_DEVICE` | 设备写入 Host 的接收缓冲区 | EP 发 MWr |
| `DMA_BIDIRECTIONAL` | 这次映射确实需要两个方向 | 仍需按设备协议交接访问权 |

如果提交队列失败且设备还没拿到地址，可以立即按原参数 `dma_unmap_single()`。一旦把描述符交给设备，就得等设备确认完成或被可靠地停住后才能解除映射。只凭主机等待超时就释放页，迟来的 DMA 写会落到已经另作他用的内存里。

## 同一条队列上通常有两类内存

**描述符环**经常由 `dma_alloc_coherent()` 分配，成功后同时得到 CPU 指针和 DMA 地址。前者用来填槽位，后者写进设备的队列基址。coherent 保证双方可以观察到对这块内存的更新；把几个字段发布给设备的先后仍然需要约定。例如下面这段示意假定设备以 `OWN` 位为接手信号，描述符字段为小端。

```c
desc->addr = cpu_to_le64(req->dma_addr);
desc->len = cpu_to_le32(req->len);
dma_wmb();
WRITE_ONCE(desc->flags, cpu_to_le32(OWN));
writel(next_tail, doorbell);
```

屏障放在 `OWN` 之前，是为了防止设备看见了“可以取走”，却还读到上一次传输的地址或长度。门铃的写法、字段宽度、环满时如何处理，要遵循这台设备的接口。[Linux 对 DMA 内存屏障的说明](https://docs.kernel.org/6.12/core-api/wrappers/memory-barriers.html)

**数据缓冲区**适合按请求做 streaming mapping。以接收为例，设备写完并把 ownership 交还后，驱动解除 `DMA_FROM_DEVICE` 映射，再读数据。如果映射要长期复用，CPU 和设备之间的每次交接应按需要使用 `dma_sync_single_for_cpu()` 和 `dma_sync_single_for_device()`。它们处理 DMA 映射的一致性，不能证明某个 DMA 命令已经完成。

## 完成中断到了，数据还不对

可以按一条请求的生命周期往回找。先把队列号、槽位、设备状态和传输长度对上，确认完成的是眼前这一笔。再比较描述符中的地址和这笔映射返回的 DMA 地址，特别留意高 32 位是否被截掉。最后核对方向、归还 ownership 的时机、接收缓冲区何时同步给 CPU。

如果 EP 已经发出 MRd，Host 侧却报告 IOMMU fault，应把 Requester 身份和 fault 地址与正在生效的映射放到一起看。如果 EP 连描述符都没取，检查 BME、队列基址和门铃往往比先分析缓存更有进展。
