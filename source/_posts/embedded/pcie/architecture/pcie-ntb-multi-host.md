---
title: "NTB 与多 Host：两棵 PCIe 树怎样通信"
date: 2026-11-27
updated: 2026-11-27
description: "通过窗口、地址翻译、doorbell 和 scratchpad，理解独立枚举域之间的数据交换。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "基础架构"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-ntb-multi-host/"
---

## 1. 先看普通透明桥

一个普通PCIe Switch Port在配置模型里是Type 1 PCI-to-PCI bridge。Root Complex的enumeration会穿过它，继续为后面的bus分配number，并为整棵下游树分配resource。对host来说，桥是“透明”的：它后面的Endpoint仍在我这棵PCI hierarchy里。

如果桥的另一侧是拥有独立 CPU、DRAM、Root Complex 和 OS 的完整主机，透明枚举就不再适用：两个 Host 都需要独立管理自己的 bus number、Configuration Space 和地址资源，不能共享同一个枚举域。

Non-Transparent Bridge（**NTB，非透明桥**）的做法是在两个主机域之间设一道明确边界。

## 2. 为什么叫“非透明”

每一侧的Host在边界处看到一个像Endpoint的Type 0 Function，而不是一个可继续下探的Type 1 bridge。Enumeration在这里停下：

```text
Host A hierarchy                         Host B hierarchy

CPU A ─ RC A ─ ... ─ [Type 0 face A] ║ [Type 0 face B] ─ ... ─ RC B ─ CPU B
                              NTB boundary

Host A不枚举Host B的bus/devices；Host B也不枚举Host A。
```

“非透明”不是说数据过不去，而是说**topology、configuration和address ownership不穿透**。数据要过去，必须走双方约定的window和通知机制。

## 3. 四个基本积木

### 3.1 Memory Window（MW）

本地Host给NTB Function的BAR分配地址，但访问该BAR的transaction会被NTB翻译到peer的一段内存或aperture。它就像一扇开在墙上的窗：窗框在A的PCI address space，窗外的景物却在B的address domain。

```text
Host A CPU writes NTB BAR aperture
  → A-side Memory Write TLP
  → NTB window match + address translation
  → B-side target address / backing buffer
```

Window不是“自动共享整个DRAM”。双方software必须协商size、alignment、translation target、buffer lifetime和access permission。

### 3.2 Doorbell

Doorbell是对peer的事件通知bit，通常可mask并发中断。常见用法是：A先把数据写进peer window，执行必要的ordering/barrier，然后敲doorbell告诉B“数据已放好”。

B 收到 doorbell 后，仍要到约定的队列位置取数据。

### 3.3 Scratchpad / Mailbox

双方可见的小寄存器用来交换protocol version、queue size、buffer address、ready/owner/generation等控制信息。它容量小，适合握手，不适合当数据面。

### 3.4 Link/status与peer lifetime

NTB软件必须把link up/down、peer reboot、host failover和window失效当成一等公民。Peer重启后，旧的translation、buffer address和queue ownership可能已经全部过期；仅凭“Link又Up了”继续往旧地址DMA，可能覆盖对端已经重新分配的内存。

## 4. 一次跨Host发送的完整例子

假设B准备了1 MiB receive buffer，NTB把A侧BAR2的`0x1000_0000–0x100f_ffff`翻译到它。

1. B通过scratchpad发布protocol generation、buffer size和ready。
2. A验证alignment/size，建立本地BAR mapping和必要的NTB translation。
3. A向`BAR2 + 0x2000`写4 KiB payload；在Link上它是一个或多个Posted Memory Write TLP。
4. A做平台/驱动要求的write ordering/flush，避免doorbell超车。
5. A设peer doorbell bit 3。
6. B的NTB hardware产生中断，B driver清doorbell，根据queue metadata消费buffer。
7. B通过反向doorbell或completion queue返还ownership。

注意第 4 步：Memory Write 是 Posted Request，没有 Completion 向 Host A 证明 Host B 已经看到数据。跨 Host protocol 必须自行定义 producer/consumer ordering；doorbell 只能通知事件，不能单独证明此前的数据传输已经按双方协议完成。

## 5. Linux NTB框架映射

Linux NTB 分成硬件驱动和使用它的客户驱动两层：

- hardware driver实现`struct ntb_dev_ops`；
- client driver通过`struct ntb_client_ops` probe/remove，并注册context callback接收link、doorbell和message event。

`ntb_dev_ops`的接口几乎就是上面四块积木的直译：

| Linux operation族 | 硬件含义 |
|---|---|
| `link_is_up/enable/disable` | Link/lifetime |
| `mw_count/get_align/set_trans/clear_trans` | Memory Window与translation |
| `db_read/set/clear/*mask*` | Doorbell status、trigger、mask |
| `spad_count/read/write` | Scratchpad握手 |
| `msg_count/read/peer_msg_write` | 额外message/mailbox通道 |

Kconfig还显示`NTB_TRANSPORT`可在hardware API上提供queue-pair transport，而`NTB_PINGPONG`、`NTB_TOOL`、`NTB_PERF`和`NTB_MSI_TEST`分别用来做基本doorbell/scratchpad测试、debugfs操作、raw window性能和MSI forwarding测试。

例如，两侧系统都已启动，但对端刚重启过。重新看到链路之后，两侧要再次确认窗口指向哪块内存、上一批队列是否作废，再提交下一笔数据。原来的总线地址看起来合法，也可能已分配给别的缓冲区。

## 6. Debug时把问题分成两个世界

| 现象 | 先查什么 |
|---|---|
| 本地根本看不到NTB Function | 本地Link/enumeration/configuration，不要先查peer buffer |
| Function可见，MW读写UR/CTO | BAR resource、bridge window、NTB translation和peer backing memory lifetime |
| 数据到了，peer没中断 | doorbell valid/mask/vector/clear语义，不要重新调BAR size |
| doorbell先到，payload还是旧的 | Posted Write ordering、write-combining/barrier/flush和peer cache coherency |
| peer reboot后随机破坏 | generation/handshake、旧MW translation和buffer ownership是否彻底失效 |

## 7. 常见误解

- **“NTB是普通Switch的另一个名字。”** 不对。透明Switch延伸同一hierarchy；NTB终止枚举并隔离两个domain。
- **“两侧看到的BAR地址应该一样。”** 不对。两个Host独立分配resource，地址通过NTB window翻译。
- **“Doorbell等于DMA completion。”** 不对。它是软件协议通知，ordering和ownership必须另行保证。
- **“PCIe Base Spec定义了一个通用NTB configuration header。”** 不对。NTB历史上是厂商/平台机制；通用概念稳定，寄存器、window和doorbell细节依hardware。

## 8. 参考资料

- MindShare *PCI Express System Architecture*：Appendix C，printed pp.999–1017左右，约PDF pp.1055–1073；Type 0 enumeration boundary、BAR translation window、mailbox/doorbell和multi-host/failover例子。
- [Linux NTB 驱动接口、内存窗口和客户驱动](https://docs.kernel.org/driver-api/ntb.html)
- 相关文档：[Topology与分层](/2026/03/03/pcie/pcie_introduction/)、[BAR与地址翻译](/2026/11/27/pcie-bar-address-translation/)、[DMA与Linux DMA API](/2026/11/27/pcie-dma-linux-api/)。
