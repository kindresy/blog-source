---
title: 从两端直连到多级桥接：六张图读懂 MCTP / PLDM 网络拓扑
date: 2026-11-27 12:10:00
permalink: 2026/11/27/mctp-pldm-topologies-from-simple-to-complex/
categories: [嵌入式开发, I3C]
tags: [i3c, mctp, pldm, linux, architecture, protocol]
toc: true
max_depth: 3
description: 用六幅拓扑图，从单 Host 单 Target、同总线多设备，逐步扩展到多接口、MCTP bridge、多级跨介质转发和独立 network，解释 EID、下一跳、地址池与 PLDM 终端的关系。
---

两端直连时，EID 和 I3C 地址看起来有些重复：Host 要找 EID 9，最后反正还是访问 `0x44`。把设备多接几个，再加一条总线，这两层地址的作用就开始分开了。

这篇从我们已经验证过的 Host—Target 直连关系出发，一次只增加一种复杂度：先加设备，再加接口，然后引入桥接，最后比较一个大网络和两个独立网络。

六幅图中的第一幅对应当前回归的连接关系；后五幅是为了讲清机制设计的教学拓扑，地址、桥接能力和地址池均为示例，没有声称 Current Proj 已实现这些扩展。每幅图都可以点击放大，也提供 PNG。

<!-- more -->

## 看图前，先区分三种边界

**物理链路边界**决定这一跳怎样访问设备。I3C 用 DA，其他 binding 使用各自的寻址方式。

**MCTP network 边界**决定 EID 在哪个地址空间里解释。同一 network 内的端点需要避免 EID 冲突；跨了总线，不意味着自动跨了 network。

**PLDM 处理边界**决定哪里真正接收管理命令、解释数据并生成响应。普通 MCTP bridge 的转发职责，并不要求它替最终 Target 执行 PLDM 命令。MCTP 将统一传输与底层 binding 分开，正是为了让上层不必跟着物理介质改写。[OpenBMC MCTP 设计说明](https://github.com/openbmc/docs/blob/master/designs/mctp/mctp.md)

如果还拿不准 network ID、EID、PID、DA、TID 的区别，可以先看[沿一笔 GetTID 拆解 ID 与收发路径](/2026/11/27/mctp-pldm-network-ids-packet-path/)，其中有一张可以切换封装、解析路径的总图。

## 1. 两端直连：先让一条管理消息走完

![拓扑一：i.MX93 Host 与 Current Proj Target 经一条 I3C 总线直连，属于同一个 MCTP network](topology-01-direct.svg)

[查看 PNG](topology-01-direct.png)

这是我们当前最熟悉的组合：一个 Linux Host，一个裸机 Target，属于同一 MCTP network。Host 的本地 EID 是 8，Target 的 EID 是 9。Host 的配置工具把这套地址空间标记为 network 1。

请求走：

```text
PLDM GetTID
  → 目的 EID 9
  → mctpi3c0
  → 邻居 PID beee00000101
  → 当前 I3C DA 0x44
  → Target PLDM handler
```

Target 响应时用 IBI 通知 Host 读取，成功返回 TID 1。TID 是 PLDM 身份，不是把 I3C DA 再编码一次。

这个拓扑不用 bridge，Host 不需要替其他设备转发。当前使用固定 EID，Host 手动配置地址、路由和邻居；GetEID/GetTID 是确认已有状态。11 月 26 日的 7 项只读查询通过，但连续分包、满包响应和压力测试仍有各自的验收边界，详见[回归记录](/2026/11/26/i3c-mctp-regression-milestone-20261126/)。

这里先形成一个判断方法：**PLDM 命令选目标 EID，路由选接口，binding 再找到这一跳的物理设备。**

## 2. 同一总线加多个 Target：EID 开始显出价值

![拓扑二：同一条 I3C 总线上有三个 Target，各自具有不同 EID、PID、DA 和 PLDM TID](topology-02-multi-target.svg)

[查看 PNG](topology-02-multi-target.png)

现在仍然只有一条 I3C 总线，但增加到三个 Target：

| 设备 | EID | I3C PID | 当前 DA | 示例 PLDM TID |
|---|---:|---|---|---:|
| A | 9 | PID-A | `0x44` | 1 |
| B | 10 | PID-B | `0x45` | 2 |
| C | 11 | PID-C | `0x46` | 3 |

PID-A/B/C 是不同设备身份的代号，不是可直接写进配置命令的十六进制值。同一总线上的 DA 也不能冲突。

Host 的应用只需选择 EID；本地表项把它们映射到同一个接口上的不同邻居：

```text
EID 9  → mctpi3c0 → PID-A → DA 0x44
EID 10 → mctpi3c0 → PID-B → DA 0x45
EID 11 → mctpi3c0 → PID-C → DA 0x46
```

访问 EID 10 时，MCTP 头中的目的 EID 为 10，Controller 实际寻址 `0x45`。不能因为都连在同一个接口，就给三个 Target 配相同 EID。

设备加入、退出或重新枚举时，还要分别维护物理身份与动态地址、EID 分配、路由邻居，以及 PLDM 终端状态。只看到 I3C 枚举成功，还不知道这个新设备使用什么 EID、支持哪些管理消息。

这个阶段没有跨总线转发，新增的主要是设备发现、标识分配和表项维护，而不是桥接。

## 3. Host 接两条总线：多接口可以属于同一个 network

![拓扑三：一个 Host 的两条 MCTP 接口处于同一 network，分别连接不同 I3C 总线和端点](topology-03-multi-interface.svg)

[查看 PNG](topology-03-multi-interface.png)

接着给 Host 增加第二个接口。示例中，两条接口都归入 Linux network 1：

| 出口 | Host 本地端点 EID | 可达 Target | Target 当前 DA |
|---|---:|---:|---|
| `mctpi3c0` | 8 | EID 9 | `0x44` |
| `mctpi3c1` | 12 | EID 10 | `0x44` |

图中为两个 Host 接口明确配置了不同的本地 EID，便于追踪源端点；具体产品如何建模多接口端点，需要按其实现核对，不能只看它们是否装在同一颗 CPU 上。

两边 Target 都用 DA `0x44` 没有冲突，因为它们在不同物理总线上。目的 EID 9 和 10 却仍要在同一个 MCTP network 中区分。

Linux 允许一个 network 关联多个接口，路由负责选择出口。[Linux MCTP：Interfaces & networks](https://docs.kernel.org/networking/mctp.html#structure-interfaces-networks)

对 Host 自己发起的请求而言：

```text
访问 EID 9  → 选择 mctpi3c0 → 本地源 EID 8
访问 EID 10 → 选择 mctpi3c1 → 本地源 EID 12
```

但这还没有证明 A 可以经 Host 访问 B。**Host 能通过两个接口访问设备，与 Host 已经启用并完成跨接口 MCTP 转发，是两项能力。** 若希望 Host 充当 bridge，还需要转发实现、双向路由和相应管理策略，不能仅靠把接口放进同一个 network。

这一图要记住两句话：同一个 network 可以跨多个接口；不同物理总线可以复用 DA。

## 4. 加入一个 MCTP bridge：目的地址与下一跳分开

![拓扑四：Host 通过 MCTP bridge 访问下游 I3C Target，端到端 EID 保持不变，每跳使用各自 DA](topology-04-bridge.svg)

[查看 PNG](topology-04-bridge.png)

现在 Host 不能直接接触最终 Target，中间加一颗具备两侧收发和 MCTP 转发能力的 bridge B。上游总线 A 连接 Host 与 bridge，下游总线 B 连接 bridge 与最终 Target。

本例设定 bridge 上行管理端点 EID 9，下行管理端点 EID 16，最终 Target 为 EID 20。两个 bridge 端点的数字只是本图选用的模型，不是说所有 bridge 都必须照抄这两个值。

Host 要查最终 Target 的 TID，目的 EID 仍写 20；但它在上游总线上只能先访问 bridge：

```text
Host 路由：目的 EID 20 → 上游接口 → bridge 的链路身份
Host 实际访问：bridge 在上游 I3C 总线的 DA 0x44

bridge 路由：目的 EID 20 → 下游接口 → Target 的链路身份
bridge 实际访问：Target 在下游 I3C 总线的 DA 0x30
```

因此，Host 上发往 EID 20 的下一跳邻居，是上游 bridge 的 PID。Host 不该把下游 Target 的 PID 当作上游总线里可直接寻址的设备。

### 4.1 经过桥接，什么保持，什么改变

| 内容 | Host → bridge | bridge → Target |
|---|---|---|
| MCTP 源 EID | 8 | 8 |
| MCTP 目的 EID | 20 | 20 |
| 本跳 I3C 寻址 | bridge DA `0x44` | Target DA `0x30` |
| PLDM 请求 | GetTID | 同一条 GetTID |
| I3C PEC | 上游包对应的校验 | 下游包重新计算的校验 |

普通 MCTP 转发不需要把源 EID 改成 bridge 的 EID，再替 Host 新发一笔 PLDM 请求。bridge 的自身 EID 供访问它自己的管理端点；转发时仍根据原目的 EID 选下一跳。[MCTP Base：桥接与路由](https://www.dmtf.org/sites/default/files/standards/documents/DSP0236_1.3.3.pdf)

响应反向返回时，也需要到 Host EID 8 的路由。只配去程，很可能看到 Target 已生成响应，Host 却收不到。

### 4.2 IBI 也不会作为一根中断线穿过 bridge

在两侧都用当前 I3C 通知模式的前提下，最终 Target 有响应时先通知下游 Controller，也就是 bridge；bridge 读取并转发数据，上游再使用自己的 Target 端通知 Host 来读。

每跳有各自的 Private Transfer、IBI 和 PEC。不要把下游 Target 的 IBI 想象成直接进入 Host 的同一个硬件事件。[DSP0233：I3C 消息传输](https://www.dmtf.org/sites/default/files/standards/documents/DSP0233_1.0.1.pdf)

## 5. 多级 bridge、跨介质：同一条 PLDM 请求经过不同 binding

![拓扑五：同一 MCTP network 中，PLDM 请求经 I3C、SMBus 和 PCIe VDM 及两级 bridge 到达最终端点](topology-05-multi-hop.svg)

[查看 PNG](topology-05-multi-hop.png)

再增加一级 bridge，并让几段链路使用不同介质：

```text
Host → I3C → bridge B → SMBus → bridge C → PCIe VDM → Target
```

这里假定各设备具有对应 binding、桥接与管理能力，硬件和软件都已实现；换几根线不会自动得到这张图的功能。

最终 Target 的 EID 为 32、TID 为 2。Host 发出的管理请求仍可表达为“把 GetTID 送到 EID 32”。每个 bridge 根据目的 EID 选下一跳：

| 位置 | 当前要做的决定 |
|---|---|
| Host | EID 32 从 I3C 接口发给 bridge B |
| bridge B | EID 32 从 SMBus 接口发给 bridge C |
| bridge C | EID 32 从 PCIe VDM binding 送往最终设备 |
| 最终 Target | 接收消息类型为 PLDM 的完整消息，执行 GetTID |

I3C 的 PID/DA 只适用于相应 I3C 链路。到了 SMBus 和 PCIe VDM，使用各自 binding 规定的物理寻址与封装，不能继续套用“PID → DA”。MCTP 的统一传输与多种 binding 正是为了隔开这些差异。[OpenBMC MCTP 架构说明](https://github.com/openbmc/docs/blob/master/designs/mctp/mctp.md)

### 5.1 多级结构里，谁保证 EID 不冲突

如果每个下游 Controller 都从 EID 9 开始随便编号，同一 network 很快就会冲突。动态管理通常使用层级地址池：顶层 Bus Owner 给下级分配一段 EID，下级再从自己的池中分配或继续划分。bridge 与 Bus Owner 是不同职责，本例让同一设备同时承担两者。[MCTP Base §8.18](https://www.dmtf.org/sites/default/files/standards/documents/DSP0236_1.3.3.pdf)

用本图数字作一个地址规划示例：

```text
顶层管理者：为 bridge B 分配池 16～39
    B 使用 16 作为图中的下行端点 EID
    B 为 C 的上行端点分配 17
    B 再把 24～39 划给 C
        C 使用 24 作为图中的下行端点 EID
        C 为最终 Target 分配 32
```

bridge B 的上行端点 9 由上层另行分配。这份数字规划没有把同一 EID 同时分给两个端点；实际实现还要同步路由、分配结果与设备生命周期。

地址池管理解决“谁可以用这些 EID”，转发表解决“到这些 EID 从哪里走”。分到地址池，不等于每条转发路径已经配置并验证完成。

### 5.2 PLDM 终端不会因为经过两座桥就多出两个

这笔 GetTID 的 PLDM 处理点仍在最终 Target，返回的 TID 为 2。bridge 可以有自己的 PLDM 管理功能，但那是另一个可单独访问的终端，不是转发每条消息都必须进入的 handler。

如果中间设备先终止原请求、解析业务，再以自己的身份向下游新发一条 PLDM 请求，它承担的是代理或网关行为。此时已经是两笔事务，不能继续套用“普通转发中源、目的 EID 保持不变”的描述。

## 6. 两个独立 network：相同 EID 可以分别存在

![拓扑六：一个 Linux Host 连接两个独立 MCTP network，两个网络各有 EID 9，通过 network ID 区分](topology-06-independent-networks.svg)

[查看 PNG](topology-06-independent-networks.png)

最后换一种扩展方式：两套管理系统不互通，各自保留自己的 EID 地址空间。Host 在 Linux 中把它们标为 network 1 和 network 2。

| Host 看到的目的地址 | 出口 | 目的设备 |
|---|---|---|
| `(network 1, EID 9)` | `mctpi3c0` | 系统 A 的设备 |
| `(network 2, EID 9)` | `mctpi3c1` | 系统 B 的设备 |

两个 EID 9 在各自 network 中解释，不构成冲突。Linux 的 network 编号属于本地选择信息，不会作为 MCTP 包头字段传给对端，两套系统也不会仅因接到同一个 Host 就自动互通。[Linux MCTP 地址空间](https://docs.kernel.org/networking/mctp.html#structure-interfaces-networks)

这和第三幅图的区别就在于：第三幅图把两条接口放进同一个地址空间，用不同目的 EID 选路；本图把接口放进两个独立地址空间，允许目的 EID 数值相同。

图中给两个 PLDM Target 选了 TID 1 和 2。不要从 EID 可跨 network 重复，推导出 TID 也自动按 Linux network ID 隔离；PLDM 终端标识与系统管理范围还需要另行规划。[PLDM Base：Terminus](https://www.dmtf.org/sites/default/files/standards/documents/DSP0240_1.1.0.pdf)

## 7. 拓扑变复杂后，怎么确认自己没有画错

拿到一张板级连接图，我会按下面的顺序把信息补全：

1. 先圈物理总线，标出每段的 Controller、Target 或其他 binding 角色。
2. 再圈 MCTP network，确认哪些接口共用 EID 地址空间。
3. 给端点标 EID，给每跳标物理身份和当前地址，检查是否在各自作用域内冲突。
4. 选一笔请求，从源端点一路写出出口和下一跳，再写反向响应路径。
5. 单独标 Bus Owner、地址池来源和配置责任，别把“能转发”与“会分配地址”混成一项。
6. 最后标出真正执行 PLDM 命令的终端及其 TID；若有代理，明确拆成两笔事务。

这些步骤也是调试顺序。例如 Host 访问远端 EID 32 失败，第一跳根本没有 ACK，就先查上游 bridge 的物理地址和接收状态；第一跳成功，也只能说明到达了 bridge，后面几跳仍需逐段核对。

六幅图变化的是设备数量、接口和转发层次。沿请求检查时，始终保留三个问题就不容易绕晕：**最终目的 EID 是谁，这一跳实际访问谁，PLDM 命令最后在哪里执行。**
