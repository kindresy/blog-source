---
title: I3C 上为什么会有网口：沿一笔 GetTID 理解 MCTP、PLDM 和五种 ID
date: 2026-11-27 12:00:00
permalink: 2026/11/27/mctp-pldm-network-ids-packet-path/
categories: [嵌入式开发, I3C]
tags: [i3c, mctp, pldm, linux, debugging, architecture]
toc: true
max_depth: 3
description: 从 network ID、EID、PID、DA、TID 的来源讲起，沿一笔 PLDM GetTID 拆解 Host 与 Target 的路由、封装、PEC、IBI 和响应解析，附可切换路径的完整总图。
---

调 I3C 时看到 `mctpi3c0`，再看到 route、neigh、MTU 和 socket，很容易犯嘀咕：我明明只接了两根线，怎么突然要配置网络了？

这个问题是在小包查询跑通后冒出来的。Host 能读到 Target 的 EID 9、PLDM TID 1，但把配置命令和线上数据放在一起看，几个数字很容易串：`network 1` 是谁的地址？为什么 I3C 已经有 `0x44`，还需要 EID？GetTID 返回 1，又为什么要向 EID 9 发请求？

下面就拿我们已经跑通的 GetTID，顺着启动、配置、发包和回包走一遍。实现细节以 11 月 26 日归档的这套 Host 为准；字节示例用于拆包，不补造 Tag 或 PEC 的实测值。

<!-- more -->

## 先看总图，再沿着一笔请求读

图的左边是 i.MX93 Linux Host，右边是 Current Proj Target。请求从左上向下，经 I3C 总线，再沿右边向上交给 PLDM；响应反向返回。

顶部四个按钮分别追踪 ID 配置、GetTID 请求、IBI 和响应解析。底部会跟着切换字节布局。图中的固定 EID 和 TID 来自本轮配置，跨总线、桥接和多 network 的扩展示例放在另一篇[从简单到复杂的拓扑图解](/2026/11/27/mctp-pldm-topologies-from-simple-to-complex/)。

<iframe id="mctp-network-map" src="network-map.html" title="MCTP PLDM 的 ID 配置、拓扑与 GetTID 收发解析总图" loading="lazy" style="display:block;width:100%;height:1650px;border:0;"></iframe>

[单独打开交互总图](network-map.html) · [查看静态图片](network-map.png)

## 1. MCTP 为什么需要网络

即使只有 Host 和一个 Target，也要回答几个问题：消息送给谁、走哪个接口、长度超了怎么拆，以及收到响应后交给哪个请求。

MCTP 处理端点寻址和消息传输，PLDM 定义管理命令和数据含义，I3C 完成当前链路上的实际访问。比如“查询对端的 PLDM TID”可以拆成三句话：

```text
PLDM：我要执行 GetTID。
MCTP：把消息送给 EID 9。
I3C：在这条总线上向 DA 0x44 发起访问。
```

MCTP 的网络可以只存在于一块板子内部。它有自己的端点地址与传输绑定，并不要求使用 IP、TCP 或以太网。MCTP bridge 还能把不同总线接入同一个管理网络。[MCTP Base 1.3.3](https://www.dmtf.org/sites/default/files/standards/documents/DSP0236_1.3.3.pdf)

Linux 复用了网络子系统来实现这套机制：应用使用 `AF_MCTP / SOCK_DGRAM`，transport 注册为 `net_device`，于是有了 `mctpi3c0` 以及路由、邻居和收发统计。`SOCK_DGRAM` 在这里表示消息式接口，不能据此把它当成 UDP。[Linux MCTP 文档](https://docs.kernel.org/networking/mctp.html)

## 2. 五种 ID，各自解决什么问题

| 标识 | 所属层次 | 我们的值 | 在本轮中的用途 |
|---|---|---|---|
| network ID | Linux MCTP 本地拓扑 | `1` | 选择哪一个 EID 地址空间和路由表 |
| EID | MCTP | Host `8`，Target `9` | 标识源端点与目的端点 |
| PID | I3C 设备身份 | Target `beee00000101` | 识别设备；当前 binding 用它作为邻居链路地址 |
| DA | I3C 总线寻址 | Target `0x44` | Controller 执行当前 Private Transfer 时使用 |
| TID | PLDM | Target `1` | 标识 PLDM terminus，本轮通过 GetTID 查询 |

这几层之间是映射关系，不是把同一个数字换几种写法。

### 2.1 network ID：Host 自己给地址空间起的编号

假设 Host 接着两套互不相通的管理网络，两边恰好都有 EID 9。程序必须说明自己访问的是 `(network 1, EID 9)`，还是 `(network 2, EID 9)`。

Linux 的 network 可以关联一个或多个接口；接口属于一个 network，路由也按 network 区分。因此 network 和某条物理 I3C 总线没有固定的一对一关系。[Linux MCTP 文档：Interfaces & networks](https://docs.kernel.org/networking/mctp.html#structure-interfaces-networks)

**`network 1` 是本地选择信息，不会变成 MCTP 包头中的一个字段。** Target 不需要把自己的 Linux network 编号也设成 1。

### 2.2 EID：管理消息最终要找哪个端点

EID 是 MCTP 层的地址。动态管理时，Bus Owner 从自己管理的地址池里给端点分配 EID；简单系统也可使用预先配置的静态 EID。Bus Owner 是 MCTP 管理角色，和 I3C Controller 的总线角色分开定义，可能由同一个设备承担。[MCTP Base §8.18](https://www.dmtf.org/sites/default/files/standards/documents/DSP0236_1.3.3.pdf)

我们这次先用固定配置：Host 本地 EID 8，Target 已配置 EID 9。没有部署自动端点管理服务，也没有发送 SetEID。

GetEID 成功的意思是读回了对端报告的 EID，并没有在查询时才把 9 分给它。

### 2.3 PID：设备在分配动态地址之前就要有的身份

I3C PID 是 48 位设备身份，由设备侧提供，包含厂商和厂商定义的信息。同一 I3C 总线上的设备需要具有不冲突的 PID，Controller 才能在动态地址分配等过程中区分它们。[MIPI I3C FAQ](https://www.mipi.org/resources/i3c-frequently-asked-questions)

本轮 Host 读到 `beee00000101`。我们没有据此认定它一定烧在熔丝中：可能的实现还包括寄存器配置或固件初始化，具体来源要查当前 Target 实现。

在 Host 邻居表中，PID 被写成六个字节的形式 `be:ee:00:00:01:01`。这个外观像 MAC 地址，但这里使用的是 I3C PID。

### 2.4 DA：Controller 给这条总线分配的访问地址

DA 由当前 I3C Controller 分配。我们的枚举先根据已知静态地址，通过 SETDASA 将 Target 配到 DA `0x44`，然后读回设备身份。静态地址和动态地址恰好同为 `0x44`，不改变它们各自的含义。

重新枚举后，应重新核对 PID 与当前 DA 的对应关系。即使新一轮仍然分到同一个数值，也不能把旧设备上下文直接当成已经恢复。

### 2.5 TID：PLDM 功能终止点的标识

PLDM 把通信及相关功能的终止点称作 terminus，TID 用于标识它。SetTID 可以设置这个值，通常由 PLDM 初始化代理执行。[PLDM Base 1.1.0 §8.1](https://www.dmtf.org/sites/default/files/standards/documents/DSP0240_1.1.0.pdf)

本次 Target 已配置 TID 1，我们执行 GetTID 读回它，没有执行 SetTID。发送这条查询时靠 EID 9 寻址，返回的 TID 1 是 PLDM 查询结果。

TID 不要求等于 EID，也不是每个 PLDM 请求头都携带的固定目的地址。

## 3. 从启动到查询，究竟是谁在配置

按本轮顺序看，整个过程有六步：

| 阶段 | 执行方与动作 | 得到什么 |
|---|---|---|
| Target 准备 | 设备提供 PID；操作员启动 BMC 模式，端点使用已有 EID/TID 配置 | PID 可读，MCTP endpoint 就绪 |
| I3C 枚举 | Host Controller 发 SETDASA、ENTDAA 和身份能力 CCC | 建立 PID → DA `0x44` 的设备关系 |
| transport 绑定 | Host 加载 `mctp-i3c`，申请并启用 IBI | 出现 `mctpi3c0`，ENEC 成功 |
| 本地网络配置 | Host 配置 network、本地 EID、路由和邻居 | 建立发往 EID 9 的本地发送路径 |
| MCTP 查询 | Host 发 GetEID | 确认对端 EID 为 9 |
| PLDM 查询 | Host 发 GetTID 等 Base 命令 | 读到 TID 1、版本和能力 |

当时 Host 的四条配置命令如下，工作目录是板端 bundle。它们修改 Host 的本地表项，不会替 Target 设置 EID，也不是每轮都应该无条件重复执行的脚本：

```sh
./mctp link set mctpi3c0 network 1 mtu 68 up
./mctp address add 8 dev mctpi3c0
./mctp route add 9 via mctpi3c0
./mctp neigh add 9 dev mctpi3c0 lladdr be:ee:00:00:01:01
```

第一条选择 network 并启用接口；第二条设置 Host 自己的 EID；第三条选择出口；第四条把目标 EID 映射到当前链路设备。

所以 `route add 9` 不能把一个实际 EID 为 10 的 Target 改成 9。它只会让 Host 按自己的表项寻找 9。

## 4. 一笔 GetTID 怎样从应用走到线上

本轮查询命令是：

```sh
# 执行端：i.MX93。此命令会产生实际总线事务。
mctp-pldm-request base 1 9 get-tid
```

其中 `base` 选择 PLDM Base，`1` 是 Linux MCTP network，`9` 是目的 EID。Host 本地 EID 8 已在前面配置。

### 4.1 PLDM 编码与 socket 边界

为了看清字节位置，下面选择 Instance ID 3 作为示例。PLDM 请求头是：

```text
83 00 02
│  │  └─ GetTID 命令码
│  └──── PLDM Type 0：Base
└─────── 请求位为 1，Instance ID 为 3
```

它外面还要有一次 MCTP message type `01`，表示这条消息承载 PLDM。这里的“消息类型 01”和“PLDM Type 00”处在不同层次。

我们这套 Host 的 AF_MCTP 会从 `smctp_type` 加入消息类型字节，应用交给 `sendto()` 的正文从 PLDM 头开始。工具内部虽然保留 `[type + body]` 布局，实际发送时使用 `req + 1`、`req_len - 1`。

这是已经核对过的本地源码行为。11 月 26 日曾因旧工具重复提交类型字节，让 Target 将错误位置的字节当成命令码；修正 socket 边界后查询才通过。不能只凭一段通用 socket 示例，推翻当前内核和工具的真实收发契约。

### 4.2 MCTP core 选择路径并加包头

应用指定 `(network 1, EID 9)` 后，Host 在这个 network 中选路：

```text
EID 9
  → 路由选中 mctpi3c0
  → 邻居表得到 PID beee00000101
  → I3C 设备对象给出当前 DA 0x44
```

MCTP core 负责包头、Tag 和必要的分片。这条 GetTID 很短，一个包就能装下。以 Tag 为 `t`、序号为 0 表示请求包头：

```text
01 09 08 (C8 | t)
│  │  │  └─ SOM=1，EOM=1，序号 0，TO=1，Tag=t
│  │  └──── 源 EID 8
│  └─────── 目的 EID 9
└────────── MCTP 头版本
```

`t` 是 0～7 的 Message Tag，不能与 PLDM Instance ID 混为一谈。前者供 MCTP 的消息关联使用，后者属于 PLDM 请求/响应头。

### 4.3 I3C binding 追加 PEC，再交给 Controller

binding 根据邻居 PID 找设备，使用 DA 发起 Private Write，并在 MCTP 包后追加 PEC。完整数据布局是：

```text
I3C 寻址：0x44 / W

Private Write 数据：
[01 09 08 (C8|t)] [01] [83 00 02] [P]
  MCTP 头 4 B    类型1 B PLDM头3 B PEC1 B
```

合计 `4 + 1 + 3 + 1 = 9 B`，不计地址帧及 START、STOP 等总线开销。`P` 代表按这笔数据计算的 PEC，没有把未归档的具体校验值补进示意图。

本地 binding 的 PEC 计算包含地址字节：写方向是 `(0x44 << 1) | 0 = 0x88`。把地址纳入校验，与把地址算进上面的 9 B 数据长度，是两件事。

`network 1` 和 PID 到这里已经完成了本地选路、找设备的任务；它们没有被塞进 MCTP 的四字节包头。

## 5. Target 收到包以后，逐层解析什么

接收顺序和发送封装相反：

1. I3C 硬件接收寻址后的数据，驱动取得实际长度。
2. I3C binding 检查长度和 PEC，去掉 PEC 后把 MCTP 包交给上层。
3. MCTP 检查目的 EID、SOM/EOM 和序号，必要时重组完整消息。
4. 消息类型 `0x01` 把消息交给 PLDM。
5. PLDM 解析请求位、Instance ID、PLDM Type 和命令码，进入 Base GetTID handler。

这条请求没有 TID 数据字段。Target 从自己的 PLDM 状态中取出 TID 1，生成成功响应。

上面描述的是分层职责与本轮实现路径，不是额外声称已经抓取了所有中断和总线边沿。若在地址阶段 NACK，第一步就没有完成；若 PEC 失败，数据也不应被拿去解释成 PLDM 命令。

## 6. 回包为什么要先发 IBI

在当前 MCTP over I3C 路径中，Target 准备好待发送数据后，用携带 MDB `0xAE` 的 IBI 通知 Host；Host 再发 Private Read 取得响应。IBI 承担“有数据可取”的通知作用，PLDM 响应经后续读取传输。[DSP0233：MCTP I3C binding](https://www.dmtf.org/sites/default/files/standards/documents/DSP0233_1.0.1.pdf)

因此它们是三个有先后关系的动作：

```text
Host Private Write 请求
    → Target 处理、准备响应并发 IBI
    → Host Private Read 响应
```

Host 连续写两个 MCTP 分片时，当前路径不会在两片之间先等一个“允许继续写”的 IBI。11 月 26 日第二片地址 NACK，要从连续接收准备状态查起，不能直接解释成少等了一次 IBI。

### 6.1 响应的字节布局

同样取 Instance ID 3、Tag `t`，成功响应可以表示为：

```text
I3C 寻址：0x44 / R

Private Read 数据：
[01 08 09 (C0|t)] [01] [03 00 02] [00 01] [P]
  MCTP 头 4 B    类型1 B PLDM头3 B  数据2 B PEC1 B
```

源、目的 EID 与请求交换，Tag 关联原消息，TO 为 0；PLDM 头中的请求位清零，Instance ID 和命令保持对应。`00 01` 中的 `00` 是成功完成码，`01` 才是返回的 TID。

总数据长度为 `4 + 1 + 3 + 2 + 1 = 11 B`。I3C 地址帧参与读方向 PEC 时使用地址字节 `0x89`。

本轮工具实际打印过：

```text
response: 01 03 00 02 00 01
completion_code=0x00
tid=1
```

打印结果是恢复成“类型加 PLDM 正文”的内部布局，不包含 MCTP 四字节头，也不包含 PEC。因此，工具打印 6 B、netdev RX 统计 10 B、I3C 数据 11 B，可以描述同一笔响应。

### 6.2 Host 为什么要用 actual_len

Host 可以给读操作一个最大长度，Target 在数据结束时提前终止。当前 Controller API 把请求上限保留在 `len`，实际接收量放在 `actual_len`；binding 必须据实际长度定位末尾 PEC。

验 PEC 并移除后，MCTP core 做目的端点、Tag 和消息重组等处理，再交给对应 socket。我们这套 AF_MCTP 把类型放在源地址的 `smctp_type`，正文通过 `recvfrom()` 返回；工具补回类型字节后，再匹配源端点、Instance ID 和命令并解析完成码。

收到一包数据还不等于命令完成。至少要走到响应匹配正确、完成码与返回内容符合预期，才算这一笔 GetTID 通过。

## 7. 69 B 到底是谁的限制

69 B 来自 I3C binding 对 MCTP 基线传输能力的要求：64 B MCTP payload，加 4 B MCTP 头，再加 1 B PEC。规范要求实现支持这个基线，不能把它当成协议永远不允许超过的最大长度；更长传输需要发现并协商。[DSP0233 §5.4.2](https://www.dmtf.org/sites/default/files/standards/documents/DSP0233_1.0.1.pdf)

我们当前工程把接口 MTU 固定为 68 B，暂未开放更长单包。于是：

| 完整 MCTP 消息长度 | 每片 payload | I3C 数据长度，含 PEC |
|---:|---|---|
| 4 B，例如本例 GetTID 请求 | 4 | 9 B |
| 64 B | 64 | 69 B |
| 65 B | 64 + 1 | 69 B + 6 B |

消息类型只在完整消息开头出现一次。后续分片继续传剩余内容，各自带 MCTP 头和 PEC。Target 广告 MRL/MWL 为 260，不能据此跳过 MCTP 的传输单元配置与验证。

## 8. 下次看日志，先问数据走到了哪一层

几个看起来相似的“没回包”，可能停在完全不同的位置：

| 现象 | 先核对什么 |
|---|---|
| 根本没有 `mctpi3c0` | 枚举身份、transport 匹配、能力与 IBI 设置 |
| 发向错误端点或找不到路径 | network、本地 EID、目的路由和邻居 |
| Controller 报地址 NACK | 当前 DA、对端准备状态与总线应答 |
| Target 收到但 PEC 不通过 | 实际长度、方向地址字节和校验范围 |
| PEC 正确却识别错命令 | 类型字节是否重复、PLDM 头偏移与编码 |
| Target 没有 IBI | 是否已经生成并排队响应，再查通知路径 |
| Host 已收数据但查询超时 | 响应类型、Tag、Instance ID、命令和完成码 |

11 月 26 日的记录里，上面几种问题确实发生在不同阶段：工具的类型字节错位修正后，7 项只读查询通过；连续两片写入仍在第二片地址阶段 NACK。小查询的成功，不会替分包、压力和恢复测试提前验收。

完整实验过程见[11 月 26 日回归里程碑](/2026/11/26/i3c-mctp-regression-milestone-20261126/)。如果现在已经能把 EID、DA 和 TID 放回各自层次，就可以继续看[六种逐步扩展的 MCTP/PLDM 拓扑](/2026/11/27/mctp-pldm-topologies-from-simple-to-complex/)，理解设备和总线增加后，哪些地址会变化、哪些关系必须保留。
