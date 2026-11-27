---
title: "Expansion ROM：驱动加载前，设备如何参与启动"
date: 2026-11-27
updated: 2026-11-27
description: "解释 ROM BAR、临时地址窗口、镜像读取与信任校验，区分标准机制和控制器实现。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-expansion-option-rom/"
---

## 1. 它解决什么问题

有些 PCIe 设备在 OS driver 启动前就必须工作。比如，机器想从一张 NVMe 或 RAID 卡启动，firmware 至少要知道如何识别它、如何读启动介质。Expansion ROM（常被叫做 Option ROM）就是 Function 可选提供的设备本地 firmware image。

它不是 BAR0 后面“偷藏的一段 RAM”，而是一个独立、可选的 Memory Space decoder，由 Type 0 header offset `0x30`的 Expansion ROM Base Address Register 描述。Type 1 header 中对应位置是`0x38`。

## 2. 直觉：临时分配地址并读取镜像

系统 firmware 或配置软件的典型动作是：

```text
发现Function
  ↓
探测ROM decoder需要多大窗口
  ↓
临时分配一段不冲突的Memory Space
  ↓
同时打开Command.MSE和ROM Enable
  ↓
CPU读取该窗口 → Memory Read TLP → Function的ROM decoder
  ↓
检查image格式/可信性，必要时复制到系统内存
  ↓
关闭ROM Enable，恢复临时改写的resource状态
```

所以 ROM“在卡上”，但 CPU 并不是通过 SPI 私下找它；它在启用时是 PCIe Memory Space 的一个目标。

## 3. ROM BAR 和普通 BAR 有什么不同

PCIe Base Specification 5.0 的 Type 0 Expansion ROM Base Address Register 中：

- bit 0 是`Expansion ROM Enable`，默认 0；
- bits 3:1 可选报告 Expansion ROM Validation Status；
- bits 7:4 可选报告 implementation-specific Validation Details；
- bits 10:8 保留；
- bits 31:11 是 32-bit ROM base address 的高 21 bit。

它和普通 Memory BAR 一样通过“写全 1、读回硬连线 0”暴露 size/alignment，最大请求空间为 16 MiB。但它多了一个明确的 Enable bit，并且只有当下面两个条件同时满足时才能命中：

```text
Command.Memory Space Enable == 1
              AND
Expansion ROM Enable == 1
```

只有 ROM Enable 置位而 MSE 仍为 0 时，设备不会响应该窗口的内存读。排查应先读回这两项，再追索镜像内容。

### 3.1 一个 128 KiB sizing 例子

旧教材给出了一个很直观的例子：软件在保持 bit 0 为 0 的前提下对 base field 写全 1，读回`0xfffe0000`。最低可写地址位对应`0x20000`，因此 decoder 要求 128 KiB，base 也必须 128 KiB 对齐。

工程上不要在 Function 仍在 decode Memory Space 时随手对 BAR 做 sizing。Base Spec 要求先禁用相应 address decode，保存原值，sizing 后恢复，最后再重新开 decode。

## 4. 一次 ROM 读最终变成什么

假设 host 把 128 KiB ROM 临时放在 PCIe address `0x9000_0000`，然后 CPU 读取 image 的`0x40`偏移：

```text
firmware/OS read
  ↓ CPU virtual/physical mapping
host MMIO address
  ↓ Root Complex outbound translation
Memory Read Request, address = 0x9000_0040
  ↓ PCIe hierarchy
Function ROM decoder hit
  ↓ local ROM/flash read path
Completion with Data
  ↓ match Requester ID + Tag
Root Complex returns bytes to CPU
```

这是 Non-Posted Memory Read，所以需要 Completion with Data。ROM BAR 本身不会导致一种新 TLP type；它只是让 Function 的 address decoder 把这笔 Memory Request 导向 ROM 数据源。

## 5. PCIe 5.0 比 2003 教材多了一道安全问题

2003 年教材的主线是“找到 ROM、检查`0xAA55`和`PCIR`、选择匹配 Vendor/Device/Class/Code Type 的 image、shadow、执行初始化代码”。它还详细描述了旧 PC 把 image 复制到`0xC0000–0xDFFFF`、调用 x86 初始化入口和 hook 实模式中断链的做法。后者是很有价值的历史背景，却不是现代 UEFI/PCIe 5.0 平台的通用实现要求。PCIe Base Spec 5.0 对 ROM 内容格式直接引用独立的 PCI Firmware Specification，不应用 2003 教材的旧 BIOS 流程替代它。

现代 PCIe 还定义了可选的 hardware validation 状态：未支持、验证中、有效、有效且可信、无效、有效但不可信，以及带 warning 的结果。如果 Fundamental Reset 解除 1 分钟后仍显示“验证中”，software 可假定它不会完成。

这不等于“PCIe 帮你安全执行了 ROM”。完整的 trust policy、签名格式和是否允许执行仍是 platform firmware/security policy 的事。Validation Details 也明确是 implementation-specific。

PCIe 5.0 还允许用 Enhanced Allocation（EA）Capability 中`BEI=8`的 entry 来表示 Expansion ROM。同一 Function 不能同时用 EA entry 和传统 ROM BAR 机制。

## 6. 配置头有这个位置，并不保证卡上装了 ROM

先看枚举得到的 ROM resource。Type 0 配置头为 Expansion ROM BAR 预留了偏移 `0x30`，某个设备可以完全不实现解码。另一种情形是硬件支持 ROM，但系统尚未给它分配地址，或者读取时没有打开 Enable。两者在“拿不到启动镜像”这个症状上很像，下一步却不同。

如果资源存在且能读出字节，再检查镜像头和平台的执行策略。镜像无法用于启动，并不能反推 ROM BAR 一定没工作。

## 7. Linux 驱动工程师需要关心吗

普通 Function driver 的`probe()`不应把 Option ROM 当成常规 MMIO BAR 来映射和运行。ROM 的 resource discovery、必要时的临时 decode 和 firmware policy 属于 PCI core/platform firmware 边界；它也不会被`pci_iomap(pdev, 0, ...)`当成 BAR0 返回。

Linux 将 ROM 作为独立的 PCI resource。平台支持时，可以通过该 PCI 设备的 sysfs `rom` 属性访问。上游 `drivers/pci/rom.c` 中的 `pci_map_rom()` 处理必要的映射和解码。读取到字节与固件是否信任并执行镜像是两个步骤。

## 8. Debug 清单

读不到 Option ROM 时，按这个顺序查：

1. Function 真的实现了 ROM BAR 吗？写全 1 前先保存状态并禁用 decode。
2. 分配的 base 是否按 size 对齐，且不与其他 resource 冲突？
3. `Command.MSE`和`Expansion ROM Enable`是否同时为 1？
4. bridge Memory window 和 RC outbound window 是否覆盖该地址？
5. 是没有 Completion、返回 UR，还是返回了内容但 image signature/validation 失败？这三种故障不在同一层。
6. 如果 decoder 与普通 BAR 共享，读 ROM 时是否还有 driver 在访问那个 BAR？Base Spec 明确要求 device-independent software 此时不要通过共享 decoder 的其他 BAR 访问 Function。

## 9. 常见误解

- **“Option ROM 就是 Endpoint 的 firmware 本体。”** 不一定。它是 host 可见的 Expansion ROM image，可以和设备内部执行 firmware 分开。
- **“ROM 是只读的，所以 ROM BAR 也全是只读。”** 内容可能只读，但 base address 和 Enable 是系统配置机制的一部分。
- 看到 offset `0x30` 时，还需要看实际探测到的资源大小。只凭这个地址无法断定卡上有 ROM。
- **“验证通过就一定可执行。”** 不对。Validation Status 提供硬件检查信息，最终执行 policy 仍属于 platform。

## 10. 参考资料

- PCI Express Base Specification 5.0 r1.0：§7.5.1.2.4，Type 0 Expansion ROM Base Address Register；§7.5.1.3.12，Type 1 对应定义；§7.8.5，Enhanced Allocation；§9.3.4.1.15，SR-IOV PF/VF 变化。
- [Linux 内核文档中的 PCI sysfs ROM 接口](https://docs.kernel.org/PCI/sysfs-pci.html)
- [Linux v6.6 的 ROM 映射实现](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/rom.c)
- MindShare *PCI Express System Architecture*：Ch.22 printed pp.783–785，ROM BAR 和 128 KiB sizing 例子；Ch.23 printed pp.871–892 左右，ROM detection、multiple image、shadowing 与旧 BIOS/Open Firmware 流程。
- [BAR 与地址翻译](/2026/11/27/pcie-bar-address-translation/)和[配置空间与 ECAM](/2026/11/27/pcie-configuration-ecam-routing/)。
