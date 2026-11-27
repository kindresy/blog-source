---
title: "Linux PCI 功能驱动：probe、MMIO 与生命周期"
date: 2026-11-27
updated: 2026-11-27
description: "把 enable、region claim、BAR mapping、BME、DMA、IRQ 和 remove 连成驱动主线。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "驱动实例"]
tags: ["pcie", "linux", "tutorial"]
permalink: "2026/11/27/pcie-linux-function-driver/"
---

两条日志往往挨在一起。第一条来自 PCI core，说某个 BDF 的 BAR0 分到了地址；第二条来自功能驱动，开始执行 `probe()`。看到第二条时，Linux 已经发现了这个 Function，并按设备 ID 把它和驱动匹配上。驱动这时要解决的是怎样接手资源、读写寄存器，以及让设备什么时候可以动起来。[Linux PCI 驱动指南](https://docs.kernel.org/6.12/PCI/pci.html)

## 从一个只读寄存器开始

假设有张教学用的卡，BAR0 是内存型 BAR，偏移 0 保存可直接读取的 32 位版本号。下面这段只读版本号，不开启中断或 DMA。选这个最小例子，是因为在 Host 能完成第一次 BAR 读之前，继续调队列意义不大。

```c
static int demo_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
        void __iomem *bar0;
        int ret;

        ret = pcim_enable_device(pdev);
        if (ret)
                return ret;

        if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM) ||
            pci_resource_len(pdev, 0) < sizeof(u32))
                return -ENODEV;

        bar0 = pcim_iomap_region(pdev, 0, "demo-control");
        if (IS_ERR(bar0))
                return PTR_ERR(bar0);

        dev_info(&pdev->dev, "revision %#x\n", readl(bar0));
        return 0;
}
```

这里用 Linux 6.12 的接口。第一步启用设备，第二步确认 BAR 的类型和长度，第三步向资源管理器申请这个 BAR 并建立 I/O 映射。若申请因冲突失败，驱动应返回错误。换成较早内核时，还会见到 `pci_request_region()` 加 `pci_iomap()` 的组合，后者失败时返回 NULL，与示例中 `pcim_iomap_region()` 的错误指针约定有别。[PCI Support Library](https://docs.kernel.org/6.12/driver-api/pci/pci.html)

## `readl(bar0)` 会去哪里

```text
CPU 的 __iomem 映射
  → Host 的 MMIO 资源与桥窗口
  → PCI 总线地址（BAR0 基址 + 0）
  → Endpoint 响应这个 BAR
  → 版本寄存器的值随读完成包返回
```

Linux 驱动访问内存型 BAR 用 `readl()`、`writel()` 等 I/O accessor。普通内存指针没有这套 I/O 访问语义。写也和读不同，PCIe Memory Write 可以是 posted 的。在中断屏蔽或复位前必须确认先前写已到达时，通常可在同一设备读一个允许读且没有副作用的寄存器。不要随手拿一个写门铃的偏移做回读。[Linux 设备 I/O 文档](https://docs.kernel.org/6.12/driver-api/device-io.html)

## 增加一条 DMA 队列会多出什么

让 Endpoint 主动搬 Host 内存时，地址路径反了过来。

| 业务 | 发起者 | 驱动准备的地址 |
|---|---|---|
| 读版本、写门铃 | Host CPU | BAR 的 I/O 映射加寄存器偏移 |
| 设备取描述符，或者把结果写回 Host | Endpoint | DMA API 返回的 `dma_addr_t`，保存于队列寄存器或描述符 |

驱动先按硬件宽度设置并检查 DMA mask，准备描述符和数据缓冲区。启动队列前调用 `pci_set_master()` 打开 Bus Master Enable。若设备有中断，先让业务中断源保持屏蔽，分配向量、注册 handler，再按设备协议启动队列、开放中断。这样即使第一笔完成得很快，处理它所需的私有数据和 handler 都已经就绪。[Linux PCI 驱动指南](https://docs.kernel.org/6.12/PCI/pci.html)

## 卸载时谁还握着这些资源

`pcim_*` 会在驱动解绑时归还它管理的映射。它无法知道 Endpoint 手里是否还有一张写着 Host 地址的旧描述符。对有 DMA 的驱动，`remove()` 至少要能回答三件事。

- 谁还能提交新请求？先停掉上层提交入口，处理尚在软件队列中的请求。
- 设备什么时候真正结束访问？按设备协议停队列，确认旧 DMA 已完成或被可靠终止，然后才能撤销映射、归还内存。
- 谁还可能运行回调？保留收尾所需的中断处理，待设备静止后屏蔽来源，等待 handler 和下半部退出，再释放 IRQ、向量和私有状态。

同一套判断也适用于 `probe()` 失败回退。若只是申请 BAR 失败，设备还没有队列可停；若失败发生在向设备发布描述符之后，就得考虑硬件在后台继续访问的可能。热拔或永久失联时，要走能容忍设备不可访问的分支，避免在 `remove()` 里无限轮询已经失效的 BAR。
