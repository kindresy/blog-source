---
title: "BAR 与地址翻译：一笔访问怎样到达设备内部"
date: 2026-11-27
updated: 2026-11-27
description: "用一个 64 KiB 寄存器窗口追踪 CPU、PCIe 和设备本地地址，理解 BAR sizing、桥窗口和 DMA 寻址。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-bar-address-translation/"
---

## 1. BAR 解决的是“设备需要多大窗口、放在哪里”

Endpoint 内部有寄存器或存储资源，Host 需要通过 PCIe Memory Space 访问它们。但不同机器的地址布局不同，EP 需要通过 BAR 报告窗口需求，由系统分配合适的基址。

BAR（Base Address Register）提供两部分信息：

- **硬件声明**：窗口是 Memory 还是 I/O、32/64-bit、是否 Prefetchable、需要多大空间；
- **系统分配**：firmware/OS 规划全局地址图后，把基址写回 BAR。

所以 BAR 像“房屋规格 + 最终门牌”，不是房屋里的那块 SRAM。

## 2. Type 0 BAR 的编码

Type 0 header 在 offset `0x10–0x24` 有六个 DWORD 位置。

### Memory BAR

- bit 0 = 0；
- bits 2:1：`00b` 为 32-bit，`10b` 为 64-bit；其余保留；
- bit 3：Prefetchable；
- 高位保存 base address；低属性位只读。

64-bit BAR 使用连续两个 DWORD：低 DWORD 含属性和地址低位，高 DWORD 含地址高 32 bit。因此“六个位置”不等于“必然有六个独立 BAR”。

### I/O BAR

- bit 0 = 1；
- 固定 32-bit I/O address；
- PCIe Endpoint 不应依赖 OS 为 I/O BAR 分配资源，现代设备控制空间优先用 Memory BAR。

### Prefetchable 到底在承诺什么

它不是“CPU 可以随便 cache”。规范核心条件是：读无副作用、读能返回请求的所有 byte、Host Bridge 合并写不会破坏语义。doorbell、clear-on-read status 这类寄存器通常不能轻率标成 prefetchable。

Base Spec 对标准 PCIe Endpoint 还要求：申请 Memory Space 的 BAR 除非包含 read side effect 或不能容忍 write merging，否则应置 Prefetchable；prefetchable BAR 必须支持 64-bit addressing。来源：§§1.3.2.2、7.5.1.2.1。

## 3. BAR sizing 为什么要写全 1

硬件只实现它关心的高地址位，窗口内 offset 对应的低地址位固定返回 0。软件在禁用相应 address decode 后：

1. 保存 BAR 原值；
2. 写全 1；
3. 读回；
4. 清掉属性位；
5. 对掩码按位取反再加 1，得到 power-of-two size；
6. 恢复原值，再按资源规划写入新基址。

例如 32-bit Memory BAR 读回：

```text
0xFFFF_0000  （低 4 bit 属性先清掉）
~0xFFFF_0000 + 1 = 0x0001_0000 = 64 KiB
```

窗口必须自然对齐到其大小。未实现 BAR 读回 0。64-bit BAR 要对连续两个 DWORD 一起 sizing。

Base Spec 的 implementation note 还强调 sizing 前通过 Command Register 禁用对应 decode，避免写全 1 的中间值被当成真实地址。来源：§7.5.1.2.1，printed/PDF pp.696–698。

## 4. 地址分配不只写 EP BAR

若 EP 在 Switch/Root Port 后面，配置软件还要为路径上的 Type 1 bridge 编程 Memory Base/Limit 或 Prefetchable Memory Base/Limit。只有目标地址同时命中每一级桥的下游 window，请求才会被逐级转发。

因此：

```text
EP BAR 配对了，但桥窗口没包住它 → Memory TLP 仍可能到不了 EP
```

如果 Link 已经起来，配置读也正常，仍要看上游桥窗口有没有包含这个 BAR。

## 5. 三个地址域

先约定名字：

1. **CPU virtual address**：驱动 `pci_iomap()` 后拿到、交给 `readl/writel` 的 `__iomem` 地址。
2. **CPU physical / AXI address**：CPU/SoC interconnect 上用于访问 host bridge aperture 的地址。
3. **PCIe address**：Memory TLP header 中用于 fabric address routing 的地址。

在 EP DMA 方向还会出现 **DMA address/IOVA**，它是设备应发出的地址，可能经 IOMMU 翻译到 Host physical memory。

数值可能恰好相等。换到带地址平移的 Host Bridge，或者为 DMA 打开 IOMMU，这个巧合就消失了。

## 6. 入站访问怎么落到设备寄存器

假设一块卡通过 BAR0 暴露 64 KiB 寄存器。系统把 BAR0 放在 PCI 地址 `0x8000_0000`，Host Bridge 把 CPU 侧的 `0x4_8000_0000` 起始窗口映射到这里。驱动想写第 `0x100` 字节处的 doorbell，各处看到的是同一次访问：

| 观察位置 | 地址或字段 |
|---|---|
| 驱动 | `writel(value, regs + 0x100)`，其中 `regs` 来自 `pci_iomap()` |
| Host Bridge 的 CPU 侧 | `0x4_8000_0100` |
| 链路上的 Memory Write | `0x8000_0100` |
| Endpoint | 命中 BAR0，窗口内偏移 `0x100` |

一个简单设备可以直接用偏移译码寄存器。带片内互连的设备也可以把 BAR0 对应到另一段本地地址，两种方案向主机呈现相同的 BAR。Linux Endpoint 框架用 EPC 的 `set_bar` 操作描述这层关系。[Linux PCI Endpoint 框架](https://docs.kernel.org/PCI/endpoint/pci-endpoint.html)

## 7. 反方向的 DMA 地址从哪里来

现在让卡读取主机的一页内存。驱动通过 DMA API 获得 CPU 侧指针和一个给设备使用的 DMA 地址，把后者填进描述符。如果地址为 `0x4000_0000`，卡发出的 Memory Read 就使用这个地址。Host 是否再将它翻译成另一段物理内存，取决于平台的 DMA 映射。

从 Endpoint 看，doorbell 是入站，DMA Read 是出站。要查“出站窗口没配好”，先记下自己的观察点。在 Host Bridge 一侧，CPU 写同一个 doorbell 恰恰是出站。

## 8. Resizable BAR：软件在设备支持范围内选择 BAR 大小

普通 BAR sizing 只读取当前固定 aperture；Resizable BAR Extended Capability 则列出硬件可接受的多个 size，系统软件选择最合适值写入 Control.BAR Size。

resize 不是在线拉伸内存窗口：

1. quiesce Function，停止相关 MMIO/DMA；
2. 清 Command.Memory Space Enable；
3. 写 Resizable BAR Control 的 size；
4. 此时原 BAR 内容未定义，重新分配并写 BAR base/bridge window；
5. 恢复 MSE 和 driver 数据面。

只有 64-bit BAR 才能宣告 4 GiB 及以上 size。软件通常选择资源布局可容纳的最大受支持 size；更大 aperture 可能改善设备可直接暴露的资源范围，也会挤占其他 MMIO address space。

可用地址空间、桥窗口和设备支持的大小共同决定能否调整。PCIe Base Specification 5.0 §7.8.6 规定了大小编码和控制字段。

## 9. Bus Master Enable 是 DMA 前置条件

Command 寄存器中的 Memory Space Enable 控制主机发往 Function 的内存访问，Bus Master Enable 控制 Function 发出的普通内存读写。两者独立，所以“能读设备版本号，DMA 却一直没动”是可能的。

Linux 驱动调用 `pci_set_master(pdev)` 设置 BME。它只完成总线主控这一步。驱动还要设置 DMA mask、申请或映射缓冲区、填入 DMA 地址，最后按设备规定启动队列。

## 10. Debug 路径

### CPU 访问 EP BAR 失败

1. `lspci` 检查 BAR 是否有资源、Command.MSE 是否置位。
2. 检查 Host/RC outbound CPU/AXI-to-PCIe region 是否覆盖 CPU physical access。
3. 看协议分析：是否发出正确 PCIe address 的 MWr/MRd。
4. 检查桥 Memory/Prefetchable window。
5. EP 侧检查 Function/BAR hit 和 inbound translation。
6. 如果链路上已能看到正确地址，查 Endpoint 的 BAR 命中和寄存器偏移。

### EP 的 DMA 没有返回数据

用三处观察分段：设备有没有发 MRd、Root Port 有没有收到它、Completion 有没有按 Requester ID 和 Tag 返回。没有 MRd 时先确认 BME 和描述符，根端收到 MRd 却报告 IOMMU fault 时查 DMA 地址和映射方向。若收到了 Cpl(UR/CA)，读 Completion Status；若一直等不到返回，再对齐超时记录、Link 状态和复位时间。

## 11. 参考资料

- PCIe Base Specification 5.0 r1.0, §7.5.1.2.1, printed/PDF pp.696–699。
- PCIe Base Specification 5.0 r1.0, §7.5.1.3.3–.9, pp.705–708。
- PCIe Base Specification 5.0 r1.0, §7.8.6, pp.862–866。
- [Linux PCI 驱动接口](https://docs.kernel.org/PCI/pci.html)
