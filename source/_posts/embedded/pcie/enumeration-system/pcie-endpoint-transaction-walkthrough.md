---
title: "一块 Endpoint 的完整旅程：从上电到 DMA 与 MSI-X"
date: 2026-11-27
updated: 2026-11-27
description: "用固定的 BDF、BAR 和地址走完枚举、doorbell、DMA Read/Write 和中断，逐层追踪真实事务形式。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-endpoint-transaction-walkthrough/"
---

这是一台虚构但协议上自洽的设备。我们用同一组 BDF、BAR 和地址走完整条链，便于在各层之间核对同一笔事务。

## 0. 场景和地址表

```text
Root Port                 00:01.0
Endpoint                  01:00.0
EP Vendor/Device ID       只用于核对枚举身份，数值略
EP BAR0 requirement       64 KiB, 32-bit, non-prefetchable
EP BAR2 requirement       64 KiB, MSI-X table/device SRAM

Host ECAM CPU base        0x4000_0000
Host CPU MMIO window      0x4_8000_0000 ...
Window maps to PCI addr   0x8000_0000 ...
BAR0 PCI / CPU physical   0x8000_0000 / 0x4_8000_0000
BAR2 PCI / CPU physical   0x8001_0000 / 0x4_8001_0000
```

这里故意让 CPU physical 与 PCI address 不相等。现实平台可能恰好相等，但把“可能相等”背成“按定义相等”，会在存在 host bridge 地址平移的平台上出错。

## 1. 上电：先有电气世界，再有 BDF

平台准备供电、参考时钟、复位和 PHY，EP 也要准备好主机将要读取的配置头及 BAR 大小。两端完成链路训练，进入 L0；数据链路层初始化流控后，主机才有条件向这个方向发起配置请求。

此时只能说明 Link 已具备传输 packet 的条件。EP 还没有被 OS 枚举，BAR 尚未分配，Function driver 也没有进入 probe；Link Up 不等于设备已经可以执行正常业务。

## 2. 枚举：`01:00.0` 怎样出现

系统先给 Root Port Type 1 header 写：

```text
Primary Bus=0, Secondary Bus=1, Subordinate Bus=1
```

随后访问 EP offset 0。ECAM CPU 地址为：

```text
0x4000_0000 + (1 << 20) + (0 << 15) + (0 << 12) + 0
= 0x4010_0000
```

```text
CPU ECAM read
  → host bridge config path
  → Type 0 Configuration Read, target 01:00.0 offset 0
  → EP config decode
  → CplD 返回 Vendor/Device ID
  → Linux 创建/填充 struct pci_dev
```

若 Vendor ID 返回 `0xffff`，通常表示 Function 不存在或访问没成功；这里不能把它当作有效设备身份。

### BAR size probing

Linux/固件关闭相关 decode、保存原值，向 BAR0 写全 1 再读回。对 64 KiB 32-bit Memory BAR：

```text
readback mask   0xffff_0000（忽略 flags）
size            ~(0xffff_0000) + 1 = 0x1_0000
```

系统把 BAR0 放在 PCI 地址 `0x8000_0000`，BAR2 放在 `0x8001_0000`，并把这两段范围纳入 Root Port 的桥窗口。EP 命中 BAR 后，用窗口内的偏移选择本地寄存器或 SRAM。

## 3. Linux probe：API 各铺哪一段路

下面只列成功路径，便于把调用与硬件动作逐项对照。实际 probe 要检查各步骤的返回值，失败时释放之前取得的资源。为了与后面的报文一致，这个例子要求系统成功分配 MSI-X 向量。

```c
pci_enable_device(pdev);
pci_request_regions(pdev, "demo");
regs = pci_iomap(pdev, 0, 0);
dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(64));
pci_set_master(pdev);
nvec = pci_alloc_irq_vectors(pdev, 1, 8,
                             PCI_IRQ_MSIX);
request_irq(pci_irq_vector(pdev, 0), demo_irq, 0,
            "demo", priv);
```

- enable 使 Function/address decode 可用，精确内部链依目标 kernel；
- request regions 声明驱动独占 BAR；
- iomap 返回 CPU 可用的 `__iomem` virtual mapping；
- DMA mask 描述设备寻址能力，set master 设置 BME；
- IRQ API 让 PCI/MSI core 分配并编程消息，不由驱动手拧 capability。

本例 `pci_iomap(BAR0)` 最终对应 CPU resource `0x4_8000_0000` 一带，不是把 PCI address `0x8000_0000` 直接当普通指针。

## 4. 一次 `writel()`：Host 敲 EP doorbell

```c
writel(0x12345678, regs + 0x100);
```

```text
CPU __iomem VA
 → CPU physical 0x4_8000_0100
 → RC outbound window
 → PCI address 0x8000_0100
 → 3DW MWr, Length=1 DW, BE=1111/0000
 → EP BAR0 hit, offset 0x100
 → Endpoint 本地总线写，偏移 0x100
 → device doorbell register
```

Requester ID 由 RC requester/path 生成，不是目标 EP 的 BDF。MWr 是 Posted Request，没有正常 Completion；Data Link ACK 也只证明下一跳收到，不能证明业务状态机已经处理 doorbell。

## 5. Linux 准备 Host DMA buffer

```c
ring_cpu = dma_alloc_coherent(&pdev->dev, 4096,
                              &ring_dma, GFP_KERNEL);
```

假设结果是：

```text
CPU virtual            ffff....          // 只给 CPU 解引用
DMA address / IOVA     0x4000_0000       // 写进 EP descriptor
IOMMU mapping          0x4000_0000 → PA 0x12_3400_0000
```

驱动写 `ring_dma`、length、ownership，执行相应 barrier，最后敲 doorbell。若把 `ring_cpu` 填进 descriptor，EP 会对内核虚拟地址发 TLP；这个地址不能由设备按 Linux 内核页表解释。

## 6. EP DMA Read：从 Host 取 256 B

EP `01:00.0` 发一笔 aligned MRd：

| 字段 | 示例 |
|---|---|
| Fmt/Type | `000b/00000b`，3DW MRd |
| Address | `0x0000_0000_4000_0000` |
| Length | 64 DW = 256 B |
| First/Last BE | `1111b / 1111b` |
| Requester ID | `01:00.0` → `0x0100` |
| Tag / TC | `0x2a` / `0` |

本例请求地址低于 4 GiB，因此使用 32-bit 地址格式和 3DW header。Base Spec 5.0 的地址规则要求这类地址使用 32-bit 格式；不能因为驱动支持 64-bit DMA，就把这笔请求改成高 32 位全零的 4DW 格式。

```text
EP DMA/local AXI requester
 → Endpoint 生成上行 Memory Read
 → MRd, RID=0x0100, Tag=0x2a, IOVA=0x4000_0000
 → Root Port / IOMMU permission+translation
 → Host PA 0x12_3400_0000
 → memory/cache subsystem
```

假设设备的 MRRS 至少为 256 B，完成方的 MPS 和 RCB 都为 128 B。这笔对齐的读以两个 128 B 的 CplD 返回。两包沿用请求的 Requester ID、Tag 和 TC，第一包 Byte Count 为 256，第二包为 128。Requester 凭 ID 与 Tag 找回原请求，并根据返回的数据量判断何时收齐。

若 IOMMU 没映射，平台会记录 fault；EP 可能收到 UR/CA 或最终 CTO，具体映射由 RC/IOMMU 实现决定。此时也需要查看 Host 的 IOMMU fault。

## 7. EP DMA Write 与 ordering

设备把采样数据写到 `0x4000_0100`：

```text
local SRAM → DMA → Endpoint 生成上行写请求
 → one or more MWr（受 MPS/4 KiB boundary约束）
 → RC/IOMMU → Host memory
```

MWr 没有 CplD。设备在数据写后发 MSI-X 时，仍须靠 ordering、barrier 和 descriptor ownership 保证 Host 不会先处理“完成中断”，后看到半份旧数据。Coherent memory 不能替代 ordering。

## 8. MSI-X：最后一笔 4 B 写通知 CPU

Host 已在 BAR2 MSI-X entry 0 写入一组平台决定的值：

```text
Message Address    0x0000_0000_fee0_1000（纯示例）
Message Data       0x0000_0045
Vector Mask        0
```

设备完成 DMA 后发 3DW MWr，Length=1 DW，address=`0xfee0_1000`，payload=`0x45`，Requester ID=`0x0100`：

```text
EP interrupt condition
 → MSI-X entry address/data/mask
 → PCIe Memory Write
 → Root interrupt controller/remapper
 → Linux IRQ number → demo_irq()
 → driver 读 status，回收 DMA buffer
```

MSI-X 仍是 Posted MWr，不返回普通 Completion。Linux IRQ number 也不等于 Message Data；前者是内核抽象，后者是平台写入设备的硬件消息。

## 9. 连续提交时，哪些地方最容易拖慢传输

王齐教材第 12 章以 Capric FPGA 卡说明一种简单做法：Host 写地址、长度和启动位，设备每做完一笔 DMA 就发一次 MSI，ISR 再读状态。如果每轮都要等上一次传输和中断完全收尾，链路空闲的时间可能比搬运本身更长。

若想连续搬运，可以依次改四处：

1. 用 host-memory descriptor ring 一次提交多笔工作，doorbell 只通知 producer index；
2. 同时保留多个 MRd Tag，让读延时被其他 outstanding request 覆盖；
3. 按 Requester ID 和 Tag 归集返回的 CplD，确认一笔读收齐后才释放对应的 Tag；
4. 用 MSI-X 向量或写回主机内存的完成记录区分通知来源，减少中断处理中等待跨 PCIe 寄存器读的次数。

同一时间保留多个 Tag 后，请求端还需要保存每笔读的目的缓冲区、剩余字节数和超时状态。否则几笔 Completion 交错回来，DMA 引擎无法把数据放回正确的位置。

来源：王齐《PCI Express 体系结构导读》第 12 章，尤其 §§12.2.2、12.4。

## 10. 如果中途 Link Down

如果 DMA Read 发出后中途掉链，Requester 手里可能还挂着未完成的 Tag。此时不要根据“doorbell 写成功”判断数据已经搬完；Posted 写也没有逐笔的成功 Completion 可供回查。

恢复时先阻止提交新的工作，保留端口和 DMA 状态，再分清发生的是 Function Reset、链路复位还是设备被拔出。接口重新可用后，检查 BAR、BME、MSI-X 和队列所有权，再允许业务继续。某个寄存器是否保留，需要结合具体复位类型与设备说明。

## 11. 延伸阅读与来源

- PCI Express Base Specification 5.0：§§2.2–2.3、6.1.4、7.5、7.7。
- [Linux PCI 驱动接口](https://docs.kernel.org/PCI/pci.html)
- 王齐《PCI Express 体系结构导读》：Chapter 12 Capric FPGA endpoint 实例（用作实现直觉和年代校正，不照搬 Linux 2.6 代码）。
- [Configuration Space、ECAM 与路由](/2026/11/27/pcie-configuration-ecam-routing/)
- [BAR 与地址翻译](/2026/11/27/pcie-bar-address-translation/)
- [Memory TLP 与 Completion](/2026/11/27/pcie-memory-tlp-completion/)
- [PCIe DMA 与 Linux DMA API](/2026/11/27/pcie-dma-linux-api/)
- [INTx、MSI 与 MSI-X](/2026/11/27/pcie-intx-msi-msix/)
- [复位与设备恢复](/2026/11/27/pcie-reset-recovery/)
