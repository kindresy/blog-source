---
title: "Data Link：ACK、重放与逐跳流控"
date: 2026-11-27
updated: 2026-11-27
description: "区分链路 ACK 与业务 Completion，解释重放、LCRC 和逐跳 Credit。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "基础架构"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-data-link-replay-flow-control/"
---

## 1. Data Link Layer 的职责

以下讨论 PCIe 5.0 使用的非 FLIT 传输方式。Transaction Layer 负责“这是一笔读、写还是 Completion”；Physical Layer 负责“怎样把 bit 送过 Lane”。Data Link Layer（DLL）负责让一条相邻 Link 上的 TLP **可靠、按序** 到达，并管理这条 Link。

它的服务包括：

- 给 TLP 加 Sequence Number 和 32-bit LCRC；
- 保存未获确认的 TLP 到 Replay Buffer；
- 用 Ack/Nak DLLP 确认或要求重传；
- 超时自动 replay；
- 用 InitFC/UpdateFC DLLP 交换 Flow Control credits；
- 用 PM DLLP 协调部分 Link power transition；
- 把 DL_Up/DL_Down 通知 Transaction Layer。

DLLP 只在相邻两个 Port 之间存在，不穿过 Switch 端到端路由。TLP 可以从 EP 经多级 Switch 到 RC；每一跳都有自己独立的 sequence/LCRC/replay。

## 2. Data Link Layer 为 TLP 增加的字段

概念上：

```text
Transaction Layer TLP
 → Data Link 加 Sequence Number + LCRC
 → Physical framing/encoding
 → Link
 → 对端 Physical decode
 → Data Link 检查 sequence + LCRC，移除外套
 → Transaction Layer 收到原 TLP
```

如果接收 TLP 的 LCRC/sequence 错误，接收端用 Nak 请求 replay；若发送端迟迟收不到 Ack，也会因 replay timer 超时重发。发送端只有收到 positive acknowledgement 后才能从 Replay Buffer 清掉相应副本。

Ack 不是“一包一回”。接收端可以把连续收到的多个好 TLP 合并确认：Ack 中的 `AckNak_Seq_Num` 表示截至哪个 Sequence Number 都已经收好。因此一条 Ack 可以一次释放 Replay Buffer 中一串旧副本。反过来，Nak 或 replay timer 到期会让发送端暂停接收新的 Transaction Layer TLP，先重放尚未确认的部分。`REPLAY_NUM` 是 2-bit 计数器；从 `11b` rollover 到 `00b` 时，PCIe 5.0 规定通知 Physical Layer retrain Link，并报告 correctable error。这里的“四次”不是应用事务重试次数，而是本 Link 的 Data Link replay 保护机制。

收到 Nak 本身不是一个 reported error。偶发的传输错误可以由 replay 恢复；持续出现 Nak、Replay Timer Timeout 或 REPLAY_NUM Rollover，才表明链路可靠性可能存在持续问题。

DLLP 自己使用 16-bit CRC。损坏的 DLLP 被丢弃，不为 DLLP 再套一层 ACK；相关协议必须能靠后续重复/更新自愈。

来源：Base Spec 5.0 §§3.1、3.5–3.6，printed/PDF pp.209–228。

## 3. ACK 不是 Completion

| 项目 | Ack DLLP | Completion TLP |
|---|---|---|
| 范围 | 一条相邻 Link | Requester 到 Completer，可跨 Switch |
| 对象 | 已正确收到某 sequence 之前的 TLP | 某个 Non-Posted Request |
| 能否说明业务执行成功 | 不能 | 能给 SC/UR/CA 等事务结果 |
| Memory Write 正常存在吗 | 会有逐跳 Data Link Ack | 没有普通 Completion |

例如 EP 发 DMA Memory Read：第一跳对它的 MRd 回 Ack，只说明相邻 Port 收到了这个 TLP；真正读到 Host Memory 后，还要等 CplD。Ack 和 Completion 都带有“确认”的意味，但它们属于不同协议层，不能相互替代。

## 4. Credit-based Flow Control

Receiver 的 Posted、Non-Posted、Completion receive buffers 都有限。PCIe 不会等 buffer 已满后再跨链路施加逐拍反压，而是由 Receiver 预先通告 credits，Transmitter 只在 credit 足够时发送。

每类 traffic 又分：

- Header credit；
- Data credit。

所以每个 VC 有 P/NP/Cpl × H/D 六种账本。VC0 建立时先用 InitFC1/InitFC2 初始化，运行中用 UpdateFC 更新已释放容量。Transmitter 消耗的是远端广告的 credit；Receiver 维护的是自己 buffer 的可用性。双方记错账会成为 Flow Control Protocol Error，而不是“性能稍差一点”。

credit 是累计计数，不是每收到一个 TLP 就把一个 token 递回来。发送端比较累计 `Credit Limit` 与 `Credits Consumed`，计数器按模回卷；接收端释放 buffer 后推进它广告过的累计值。初始化时的全零编码还可以表示 infinite credit，但这不是“buffer 真有无限大”，而是该接收路径保证不会因这种 traffic 耗尽。advertise infinite 之后，运行时 UpdateFC 仍要按规范发送，只是对应 credit 字段保持零。

早期资料里的普通字段宽度在高速 Link 上可能限制可广告的 buffer。PCIe 5.0 的 Scaled Flow Control 给 header/data credit 增加倍率；支持 16.0 GT/s 或更高速率的 Port 必须支持该能力，但是否在某条 Link 上激活仍由双方 DL Feature Exchange/FC initialization 的结果决定。读取计数值时要把协商好的倍率算进去。

Flow Control 不是 congestion control，也不保证一个请求最终成功；它解决的是**不要发送远端当前无空间接收的 TLP**。

## 5. 从链路两端读一条记录

假设 EP 读主存，分析仪先看到 MRd，随后看到相邻端口确认收包。如果重发的是同一个链路序号，先查这一跳的传输质量。如果确认已到，软件却一直等读数据，就沿 Requester ID 和 Tag 找回来的 CplD。两种故障用的是两套编号。

## 6. Debug 快速判断

- **TLP analyzer 看见重发**：查 LCRC、sequence、Nak、replay timer；问题多在这条 Link 的 SI/PHY/DLL。
- **一直没有发出 TLP**：查相应 P/NP/Cpl header/data credit 是否为零，也查上层 ordering/backpressure。
- **MRd 已 Ack 但软件超时**：继续追 CplD，确认相同 Tag 的返回包到了哪一跳。
- **Replay rollover**：2-bit `REPLAY_NUM` 从 `11b` 回卷，硬件 retrain Link并报告 correctable error；它说明重放一直不能恢复可靠投递。
- **DL 未 Up**：查 DL Feature/InitFC，而不只看 LTSSM 是否 L0。

## 7. 参考资料

- PCI Express Base Specification 5.0 r1.0, §§2.6、3.1–3.6（尤其 §§3.4.2、3.6.2–3.6.3），printed/PDF pp.187–243。
- MindShare *PCI Express System Architecture*, Chapters 5 and 7（2003）；仅取 ACK/NAK/credit 教学模型，credit 上限和线路编码以 PCIe 5.0 为准。
