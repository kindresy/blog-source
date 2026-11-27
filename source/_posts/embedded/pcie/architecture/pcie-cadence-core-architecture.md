---
title: "读 Linux Cadence PCIe Host 驱动：一次配置读怎样找到 Endpoint"
date: 2026-11-27
updated: 2026-11-27
description: "用一次 Vendor ID 读取串起 PCI Core、Cadence 配置窗口和下游链路，再看同一块设备的 BAR 访问走哪条路。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "基础架构"]
tags: ["pcie", "cadence", "linux", "code-analysis"]
permalink: "2026/11/27/pcie-cadence-core-architecture/"
---

假设根总线编号为 0，Root Port 下面直接插着一个 Endpoint。Linux 想知道 `01:00.0` 是什么设备，先读配置空间偏移 0，取回 Vendor ID 和 Device ID。这里有个容易错过的细节：在 Linux v6.6 上游的 Cadence 默认 Host 驱动里，CPU 读哪个本地地址，只能说明读了哪个寄存器，还不能单凭这个地址知道读的是哪块卡。设备身份的另一半在控制器刚刚写好的出站窗口里。

## 控制器怎样接上 PCI Core

入口是 `cdns_pcie_host_setup()`。它分别映射名为 `reg` 和 `cfg` 的平台资源。前者用来操作控制器自身和 Root Port 配置寄存器，后者是 CPU 访问下游配置空间时用的窗口。驱动启动链路，初始化 Root Port 和地址翻译，最后把 `pci_host_bridge` 交给 `pci_host_probe()`。桥上可以由平台预置别的 `pci_ops`。下面追的是没有覆盖时安装的 `cdns_pcie_host_ops`。[Linux v6.6 的 Host 初始化与默认回调](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/cadence/pcie-cadence-host.c)

链路启动本身又分两步。发起启动失败，setup 直接返回错误；启动动作已经接受，但等待 Link Up 超时，这个版本记录调试消息后仍会继续桥的初始化。因此系统里能看到 Root Port，只能说明根端口自己的配置空间可读，桌上的卡有没有成功建立链路还要单独看。

## 跟一笔 Vendor ID 读取

在通用层，`pci_generic_config_read()` 先向桥的 `map_bus` 要一个 I/O 地址，再按请求宽度执行读操作。在 Cadence 的 `cdns_pci_map_bus()` 里，把几种输入放在一起看，窗口的用途就清楚了。[通用配置读的实现](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/access.c) · [Cadence 对 BDF、链路状态与请求类型的处理](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/cadence/pcie-cadence-host.c)

| 例子（根总线为 0） | Cadence 默认回调怎么处理 |
|---|---|
| 读 `00:00.0` 的标识 | 根总线的 `devfn` 为 0，直接取 `reg_base` 加配置偏移，访问 Root Port 本地寄存器。 |
| 读 `01:00.0` 的标识 | 检查链路状态，将总线号 1 和设备、功能号写入出站区域 0，选择 Type 0 配置请求，返回 `cfg_base` 加配置偏移。 |
| 读交换机后面更深总线上的设备 | 仍然通过区域 0，把目标 BDF 换成那台设备，选择 Type 1 配置请求，再由下游桥路由。 |

配置偏移只有 12 位。同样是读偏移 0，连续枚举两台下游设备时，从 CPU 端看，两次读可以落在**同一个** `cfg_base` 地址，而线上寻址的设备不同。若要从软件日志还原一次配置访问，要把目标 BDF、配置偏移和请求类型放在一起。只记最后一次 MMIO 读的地址就丢了目标。这里也解释了为什么这套实现不能直接套用 ECAM 的“把总线号左移 20 位”公式。[ECAM 与配置路由](/2026/11/27/pcie-configuration-ecam-routing/)

链路没起来时，默认回调在访问下游前就返回空地址。通用读把这个结果转换为 `PCIBIOS_DEVICE_NOT_FOUND`。所以看到软件报告“设备不存在”时，有一个很具体的分界点可查：这次检查是在产生 Configuration Read 之前就结束了，还是控制器已经把请求送了出去？

## 同一块卡，访问 BAR 又走哪里

假设枚举完成，Host 驱动准备写卡的门铃寄存器。目标现在是 BAR 分配到的 PCI 内存地址，加上门铃的偏移。BDF 帮助软件找到设备及 BAR 资源；线上这笔 Memory Write 则按地址路由。

`cdns_pcie_host_init_address_translation()` 留出区域 0 给配置访问，从区域 1 开始给桥的资源窗口建立出站翻译。看它处理内存窗口时的减法最直观：设备侧 PCI 起始地址等于 CPU 资源起始地址减去该资源的 `offset`。举个便于心算的例子，CPU 内存窗口从 `0x9000_0000` 开始，若 offset 为 `0x1000_0000`，PCI 窗口就从 `0x8000_0000` 开始。驱动把 BAR 映射后在窗口内偏移 `0x100` 写一次，对应的 PCI 侧地址就是 `0x8000_0100`。这个例子取 BAR 位于窗口开头，两侧之差在整个窗口内保持不变。[Linux v6.6 的 Cadence 地址翻译实现](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/controller/cadence/pcie-cadence-host.c)

方向再反过来，设备发起 DMA 访问主机内存，控制器要处理入站事务。Host 驱动在另一条路径里处理 `bridge->dma_ranges`。驱动作者写给设备描述符的地址来自 Linux DMA API；BAR 在 Host 上映射出的 CPU 指针只供 Host 驱动访问设备。把两者记在不同栏里，抓到一笔访问时，直接问发起方是谁、目的地在哪一侧，就能选对窗口。[设备侧的 BAR 与地址映射](/2026/11/27/pcie-cadence-register-programming/) · [Linux DMA API 与设备地址](/2026/11/27/pcie-dma-linux-api/)
