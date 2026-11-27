---
title: "PCIe 热插拔：有序移除与意外掉卡"
date: 2026-11-27
updated: 2026-11-27
description: "沿停止 I/O、断电、链路变化和恢复说明热插拔，区分正常操作与异步移除。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-hotplug-removal/"
---

## 1. 两种“设备不见了”必须先分开

### Orderly removal

用户先按 Attention Button 或通过软件请求移除，OS 有机会：

1. 阻止新 I/O；
2. quiesce DMA 与中断；
3. 完成/终止 outstanding requests；
4. unbind driver、释放资源；
5. disable Link；
6. 关闭 slot power 并用指示灯告诉用户可以拔卡。

### Async removal

卡、线缆或 Downstream Link 在 OS 同意前已经消失。此时在途 MRd 可能无法收到 Completion，Posted MWr 是否到达也可能无法判断；Requester 会看到 CTO，Port 可能记录 Receiver Error、Bad TLP、Bad DLLP 或 Surprise Down。软件只能在事件发生后进行 containment 与恢复，无法补做移除前本应完成的 quiesce。

突然拔卡时，系统可能来不及先停队列或给设备下达命令。移除路径需要处理最后一次 MMIO 和最后一笔 DMA 都没有可靠完成通知的情形。

## 2. Hot Plug 为什么属于 Downstream Port

每个可热插拔 slot 由相邻 Root Port 或 Switch Downstream Port 控制。PCIe 点到点拓扑使每个 slot 有独立 Port，不需要像并行 PCI 那样为整条共享 bus 加一套隔离开关。

标准 toolbox 包括：

- Presence Detect；
- Attention Button；
- Power Fault；
- Manually-operated Retention Latch（MRL）sensor；
- power controller；
- Attention/Power indicator；
- electromechanical interlock；
- Slot Capabilities/Control/Status；
- Command Completed 与 Data Link Layer State Changed event。

不是所有系统都装齐这些零件。Capability bit 说明有什么，Control bit 发命令，Status bit 保存发生过什么。看到`Slot Implemented=1`也不等于一定有 Attention Button、电源开关和 MRL。

## 3. 一次安全拔卡的协议/硬件链

```text
Attention Button / software request
 → Slot Status event + hot-plug IRQ
 → OS验证请求
 → Function driver停止queue、DMA、IRQ
 → PCI core解除driver与resource使用
 → Downstream Port Link Disable
 → Slot Control关闭power / indicator
 → Command Completed + 状态确认
 → 用户得到“可以拔”指示
```

写 Slot Control 后不能立刻假定继电器已经动作。Hot-plug command 可能异步完成；支持 Command Completed 的实现用 status/interrupt 确认。现代规范要求单条 command 相关动作不超过 1 秒；power-off 之后，software 还要等待规定时间再执行依赖电源已移除的动作。

安全插入基本反向：检测 presence、上电、refclk、释放 PERST#，等待 LTSSM 与 DL_Up，再枚举/分配资源/bind driver。**Presence=1 不是可以读 Vendor ID 的同义词**；中间还有 power、reset、training 和 Data Link initialization。

## 4. Event 与 interrupt 为什么是 sticky + level 思维

Slot Status 里的 Attention Button、Power Fault、MRL Changed、Presence Changed、Command Completed、DLL State Changed 各有 status，Slot Control 有对应 enable，另有 Hot-Plug Interrupt Enable 总开关。

status 是 pending 事件，通常 RW1C。INTx 模式只要“master enable + 某 status 与 enable 同时为 1”就保持 assert；MSI/MSI-X 则在该逻辑从 false 变 true 时发送消息。ISR 应：

1. snapshot 所有 status；
2. 处理/排队 work；
3. 对确认过的 bit 写 1 清除；
4. 再读一次，处理清除期间新到的事件。

如果只应答中断控制器而未处理 Slot Status，电平中断会再次触发。先保存事件位，再清本次已处理的位。

## 5. Async removal 的 containment

PCIe 5.0 §6.7.6 对异步移除推荐使用 DPC。DPC 在 Downstream Port 建立隔离边界，软件据此停止访问下游。仅从一次意外掉链很难判断最后一笔写是否已抵达目标。

典型证据链：

```text
physical disappearance / containment disable
 → DL_Active→DL_Down
 → optional Surprise Down / DPC trigger
 → pending MRd becomes CTO or aborted
 → Port/Root AER + hot-plug event
 → OS停止访问removed subtree
 → driver error/remove path清DMA/IRQ/resource
```

## 6. 板上有哪些热插拔硬件

同一颗主控下的两个插槽，也可能只有一个带供电开关和状态灯。检查时从上游 Downstream Port 的 Slot Implemented 和 Slot Capabilities 入手，区分 Presence Detect、Power Controller、Attention Button 等可见能力。

如果只有 Presence Detect，软件可以获知插卡状态，但不能凭空切断插槽电源。对于意外移除，再单独检查端口是否实现并启用了 DPC。

## 7. Linux 边界与 driver 责任

通用 Linux 架构中，Root Port hot-plug service 负责 slot 事件、增删 PCI hierarchy；Function driver 的 remove/error path 负责设备特有的 queue、DMA、IRQ 和 context。即使 OS 能从对象树移除`pci_dev`，它也无法撤销已被设备发出、但状态不明的外部副作用。

Function driver 应保证：

- remove/suspend/error 路径可阻止新 doorbell 和 DMA descriptor；
- 中断 handler/workqueue 在 BAR unmap 前同步退出；
- streaming DMA mappings 按 API 收尾；
- MMIO read 返回 all-ones 或异常后不无限重试；
- async removal 后不等待永远不会到的 device completion；
- recovery 与正常 remove 可区分“设备还活着能 quiesce”和“只能本地清理”。

Linux 的 pciehp 服务处理插槽在位和链路变化，再把设备增删交给 PCI core。它能做哪些事，取决于硬件声明的插槽能力以及操作系统是否取得原生热插拔控制权。

## 8. Debug：设备突然消失时留什么

1. Presence、DLL Link Active、LTSSM history 的时间顺序；
2. Slot Status/Control/Capabilities 原始值，读取后再清；
3. AER Uncorrectable/Correctable、DPC（若有）、Surprise Down；
4. outstanding MRd/tag、CTO 与 DMA engine 状态；
5. `dmesg`中 hotplug、AER、IOMMU fault 和 driver remove 先后；
6. 电源、PERST#、refclk 与 PRSNT#波形；
7. 是 orderly 操作、真实拔卡、power fault，还是 Link/SI 故障伪装成拔卡。

## 9. 参考资料

- PCI Express Base Specification 5.0 r1.0：§§6.7、6.2、2.9，尤其§6.7.6 Async Removal。
- [Linux v6.6 的热插拔控制器实现](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/hotplug/pciehp_ctrl.c)
- MindShare *PCI Express System Architecture*：Chapter 17，约 PDF pp.705–750；orderly removal 直觉可用，surprise-removal 提前量说法已按 PCIe 5.0 校正。
