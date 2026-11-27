---
title: "Linux Host、EPC 与 EPF：控制器驱动的职责边界"
date: 2026-11-27
updated: 2026-11-27
description: "从 Linux 的通用主桥和测试 EPF 入手，沿一笔 BAR 命令和返回中断，看清两侧四类驱动的分工。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "驱动实例"]
tags: ["pcie", "linux", "tutorial"]
permalink: "2026/11/27/pcie-linux-host-controller/"
---

同一块 Endpoint 测试卡，两边都可能有个名字带 `pci` 的驱动。Host 一侧有个驱动负责把根总线交给 PCI core，另一个驱动与扫描出来的测试 Function 匹配。卡上如果也跑 Linux，还能看到 EPC 和 EPF 两层。排查“Host 枚举不到卡”，先把这四个角色放在各自所在的一侧。

| 所在侧 | 驱动 | 它处理的对象 | 典型工作 |
|---|---|---|---|
| Host | Host bridge / controller driver | 平台上的主桥 | 配置空间访问、总线号与资源窗口，把根总线交给 PCI core |
| Host | PCI Function driver | 枚举得到的 `struct pci_dev` | 按设备协议使用 BAR、DMA 和 IRQ |
| Endpoint | EPC（Endpoint Controller）driver | 本地控制器硬件 | 配置 BAR 映射、建立对端地址访问路径、实现触发中断等操作 |
| Endpoint | EPF（Endpoint Function）driver | 准备向 Host 暴露的一个功能 | 定义功能身份、BAR 内的数据和命令、处理业务 |

## 主桥究竟交给 Linux 什么

以 Linux 6.12 的通用 ECAM 平台主桥为例，`pci_host_common_probe()` 先通过设备匹配数据拿到访问配置空间的方法，分配 `pci_host_bridge`。其辅助函数读取设备树中的配置空间资源与总线号范围，创建配置窗口。之后，主桥记录配置访问操作并调用 `pci_host_probe()`，PCI core 才能沿这条通道找设备。[上游通用主桥驱动](https://github.com/torvalds/linux/blob/v6.12/drivers/pci/controller/pci-host-common.c)

对标准 ECAM，设备的 bus/device/function 和寄存器偏移可以换算成配置窗口中的偏移。一些控制器使用可重编程的共享窗口，驱动就得保证“选好目标”和“发出此次访问”属于同一笔操作。两种硬件都可以向上提供 `pci_ops`，功能驱动无需知道根端如何发出那笔配置读。

调试时，配置窗口、MMIO 窗口和 DMA 地址范围很容易被混用。在采用设备树的平台上，主桥的 `ranges` 描述子总线空间与父地址空间的关系，用来访问 BAR；`dma-ranges` 则描述子设备发起 DMA 时可以使用的地址范围与转换。设备实际拿到的 DMA 地址还可能受 IOMMU 影响。一次成功的 Vendor ID 读取验证了配置通路，接下来仍有 BAR 访问、DMA 两条独立的数据通路要查。

## 卡上怎样组装一个 Function

一个测试 EPF 可以给 Host 暴露一段控制 BAR。EPF 定义“命令”“结果”等业务字段，在本地分配相应内存，借助 EPC 把这段内容暴露给某个 BAR。Host 枚举成功后，写 BAR 下命令，EPF 处理命令，再请 EPC 按 Host 已启用的类型报告完成。Linux Endpoint Framework 还提供 configfs 接口，可把选定的 EPF 实例绑定到 EPC。[Linux PCI Endpoint Framework](https://docs.kernel.org/6.12/PCI/endpoint/pci-endpoint.html)

Linux 自带的 `pci-epf-test` 就展示了中断这一段。收到请求后，测试功能读取所需的 IRQ 类型；MSI-X 路径先用 `pci_epc_get_msix()` 取得数量、检查请求，再将功能号、虚拟功能号、类型和中断号传给 `pci_epc_raise_irq()`。最终如何把请求变成链路上的 Memory Write，由 EPC 实现。Host 完成中断的接收路径在另一侧。[上游测试 EPF 的中断实现](https://github.com/torvalds/linux/blob/v6.12/drivers/pci/endpoint/functions/pci-epf-test.c)

```text
Host 测试功能驱动 ── 写 BAR 命令 ──→ EPF 的命令区
                                    ↓
                                  EPF 处理
                                    ↓
Host IRQ handler ←── MSI/MSI-X 写 ── EPC 实现的投递路径
```

## 哪一侧的日志最值得先看

Host 上还没有这个 BDF 时，业务驱动的 `probe()` 无从发生。先找 Host bridge 是否注册成功、根端能否访问配置空间，再看 EPF 是否已绑定、EPC 是否处于可被枚举的状态、链路是否建立。

如果 Host 已经能读出正确的 Vendor/Device ID，但 BAR 读仍然失败，范围已经缩小到主桥的 MMIO 窗口、BAR 分配与解码、EP 侧 BAR 的映射和内存。若 BAR 命令、结果都能读写，唯独没有完成中断，再查向量配置、EPF 的请求与 EPC 的投递路径。把每一步看到的证据记在负责它的那一侧，会比在两边同时改驱动省下很多时间。
