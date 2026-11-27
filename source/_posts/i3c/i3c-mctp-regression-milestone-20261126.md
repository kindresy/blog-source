---
title: I3C 回归里程碑：7 项管理查询通过，连续分包仍停在第二包 NACK
date: 2026-11-26 19:00:00
permalink: 2026/11/26/i3c-mctp-regression-milestone-20261126/
categories: [嵌入式开发, I3C]
tags: [i3c, mctp, pldm, linux, baremetal, bringup, debugging]
toc: true
max_depth: 3
mermaid: true
description: 从 open 枚举基线追溯、MCTP 模式切换和 AF_MCTP 类型字节修正，到 7 项只读查询、69 字节边界与连续分包实验，记录 2026-11-26 的调试过程和未解决问题。
---

**2026 年 11 月 26 日，MCTP/PLDM over I3C 的基础查询终于有了端到端结果：3 项 MCTP Control、4 项 PLDM Base，共 7 项只读查询全部通过。** Host 能读到 Target 的 EID、TID、协议版本和命令位图，响应版本列表的 CRC32 也通过了校验。

接着往包长边界走，新的问题出现了。Host 连续发送一条拆成两包的消息时，第一包 69 字节发送成功，第二包 6 字节在 `0x44/W` 地址阶段收到 NACK。两包之间留约 1 秒，Host 驱动又都报告发送成功；重新枚举后去掉间隔，第二包 NACK 再次出现。

今天可以把“单包管理查询打通”记为一个里程碑。连续分包、Target 重组和满包响应还没有完成验收，接下来要盯的是第一包结束后，Target 怎样准备好接下一包。

<!-- more -->

**11 月 27 日补充阅读：** 把这轮回归涉及的概念单独整理成了[网络、ID 与 GetTID 收发路径](/2026/11/27/mctp-pldm-network-ids-packet-path/)，并用[六幅拓扑图](/2026/11/27/mctp-pldm-topologies-from-simple-to-complex/)讲解从直连到多级桥接的变化。下文保留当日实验过程与停点。

上一篇[移植记录](/2026/11/25/mctp-pldm-i3c-milestone-20261125/)停在首笔请求地址 NACK。今天继续在 `i3c回归验证` 会话里推进，前半段先找回枚举基线，再切入 MCTP；中间修掉 Host 请求工具的字节错位；最后才碰到分包问题。下面把这几个阶段分开记，否则同样一个“没收到响应”，很容易被当成同一个故障。

## 1. 先把历史基线找回来：open 成功，究竟是哪一版

今天一开始没有急着发 MCTP 请求，而是回头找最初用 `i3c_test open` 配合 Host 枚举的版本。这一步有实际用途：后面不论改了协议层还是 Target 运行模式，总得知道底层原来在哪个组合上跑通过。

查到的记录有三组，含义并不一样。

| 记录 | 能确认的内容 | 不能据此确认的内容 |
|---|---|---|
| 2026-09-24 的 `open-2khz-3placeholder` 部署 | Host 分支 `lab/imx93-i3c-open-2khz`，提交 `028713bdf`；配置约 2 kHz，正式枚举前先发三次 placeholder 供钟；已有构建、部署和重启检查 | 当时记录明确没有完成 Target 枚举成功验证 |
| 2026-11-18 的 `i3c-poll-write4/run03` | 找到明确记载 `i3c_test open` 后 Host 枚举成功的记录；SETDASA、ENTDAA 和身份查询通过；Host 产物有哈希和源码快照 | 没有 Target 固件指纹，不能还原双端完整版本 |
| Baremetal unified open 实现记录 | 找到实现基线 `348f02c72c42300f3a0fd8e6830bc72f06223c19` | 不能证明 run03 烧入的就是这一提交对应的固件，相关阶段还有未提交修改 |

run03 的 Host app 基于 `befcc155109d0413e4c0b9dee093530839fbcbbc`，叠加了工作树改动。因此，这个 commit 只能叫源码基线；实际产物还要结合 R4 的 source 快照和 `artifact-sha256.txt` 看。

这里留下一个以后要补的工程习惯：每轮回归，双端都保存源码状态和固件指纹。否则过几周再追溯，只能说“某个基线加一些修改跑通过”，很难把同一套实验环境重新搭出来。

### 1.1 原来裸写通过，并不包含今天的 PEC 验证

昨天的讨论还专门核对过旧 `i3c-host ... write HEX` 回归：Host 把十六进制文本解码后按原长度写出去，没有自动追加 PEC；Target 当时要求 PEC 关闭。

所以，即使旧回归里写过一串 69 字节，也不能把它算成 MCTP 的“68 字节包加 1 字节 PEC 已验证”。当时末尾的 `E2` 是用户提供的载荷字节，没有计算或校验含义。

今天走 `mctp-i3c` 后，PEC 是 transport 的组成部分。长度刚好相同，不代表经过同一条代码路径，也不代表覆盖了相同的功能。

## 2. 今天的枚举分了几轮，DCR 的变化要保留

中午先检查当前状态。当时控制器已有绑定，`i3c` 模块被 `mctp_i3c` 使用，sysfs 里能读到 PID 和动态地址。这次只是确认已有状态，没有新发一轮枚举。后来明确要求重启内核、只做 I3C 枚举，才重新走 Controller 的初始化。

这两种动作值得区分：读到已有设备，说明设备模型里存在这个 Target；重新枚举成功，才说明这一轮初始化和 CCC 交互实际走通了。

当日几次结果按会话时间排列如下，时间采用北京时间：

| 时间 / 阶段 | 动作与结果 |
|---|---|
| 12:04 左右 | 检查现存枚举状态；保留活动绑定，没有重新发总线事务 |
| 12:06 后 | 重启到 `6.6.36-slim-mctp`，仅加载 I3C core 和 Controller；枚举成功，DCR 为 `0x00` |
| 17:05 后 | 再做一次纯 I3C 枚举，DCR 仍为 `0x00`，没有发送 Private 数据 |
| 17:18 后 | 操作员确认已执行 `bmc start`；Host 重启并枚举，读到 DCR `0xcc` |
| 17:24 后 | 根据 Target 状态截图，确认 MCTP endpoint 就绪，开始绑定 `mctp-i3c` |
| 18:51 后 | 为复现分包问题再次枚举、绑定并恢复路由；DCR 仍为 `0xcc` |

DCR 从 `0x00` 到 `0xcc`，不能在最终表格里悄悄抹平。前几轮只验证通用 I3C 设备身份；后面才在操作员确认 BMC 启动的条件下进入 MCTP 模式。早期枚举成功，没有顺便证明 MCTP endpoint 已经准备好。

### 2.1 枚举日志里的 assigned=0 为什么不算失败

后面复现用的一轮日志很完整，顺序是：

```text
PLACEHOLDER 1/3：0x7e/W ACK
PLACEHOLDER 2/3：0x7e/W ACK
PLACEHOLDER 3/3：0x7e/W ACK
SETDASA：rc=0
ENTDAA：rc=0, assigned=0
GETPID / GETBCR / GETDCR / GETMRL / GETMWL / GETCAPS：成功
identity OK：da=0x44 PID=beee00000101 BCR=3e DCR=cc
```

这套实验配置先通过 SETDASA，把静态地址 `0x44` 对应的 Target 配到动态地址 `0x44`。后续 ENTDAA 没有再分配一个新设备，`assigned=0` 与前面的成功并不冲突。判断整轮结果，还要看身份查询和最终 sysfs 状态。

本轮读回的关键信息是：

| 字段 | 值 | 在当前系统中的用途 |
|---|---|---|
| PID | `beee00000101` | I3C 设备身份，也是 MCTP 邻居表使用的链路地址 |
| DA | `0x44` | 实际 Private Transfer 的 I3C 寻址地址 |
| BCR | `0x3e` | transport 检查 IBI 与 MDB 能力的依据 |
| DCR | `0xcc` | 当前 MCTP 模式下读到的设备类别 |
| MRL / MWL | `260 / 260` | Target 广告的最大读写长度 |
| GETCAPS | `01 f1 18` | 本轮 CCC 能力查询的实际返回 |

MRL/MWL 为 260，没有让当前 MCTP 接口自动变成 260 字节 MTU。本次 binding 仍按基线配置：MCTP payload 64 字节、MCTP 包 68 字节，加 PEC 后 69 字节。后面的边界测试就是围绕这个限制做的。

驱动日志还打印了 PP `1994.681 Hz`、OD `1953.125 Hz` 和 SKEW `4`。这是配置计算与寄存器读回信息，今天没有为这些轮次另抓物理波形，不能把它写成实测总线频率。

## 3. 枚举之后，还需要把三层地址接起来

Target 就绪后，Host 加载与当前内核匹配的 `mctp-i3c.ko`。绑定完成，创建 `mctpi3c0`，IBI 启用；日志中 ENEC CCC 返回成功。

这时接口存在，但最初还是 down，也没有 Host EID、路由和邻居。一个网口名字出现，不会自动告诉内核“EID 9 在哪里”。

本轮采用固定配置：

```text
MCTP network：1
本地 Host EID：8
远端 Target EID：9
出口设备：mctpi3c0，MTU 68，up
邻居：EID 9 → PID be:ee:00:00:01:01
I3C 设备：PID beee00000101 → 当前 DA 0x44
```

这里的 `network 1` 是 Linux MCTP 的网络编号；EID 是 MCTP 端点地址；PID 是 I3C 设备身份；DA 才是 Controller 发起总线事务时使用的动态地址。它们不能互相代替。

<pre class="mermaid">
flowchart LR
    A[请求：network 1 / EID 9] --> B[路由：mctpi3c0]
    B --> C[邻居：PID beee00000101]
    C --> D[I3C 设备：DA 0x44]
    D --> E[Private Write 到 Target]
</pre>

Host 配置本地 EID、路由和 PID 邻居，是本地 netlink 操作，不会因为加了一条路由就向 Target 发数据。加载 transport 并启用 IBI 则会涉及 ENEC 等总线操作，两者的作用不同。

Target 的 EID 9 来自对端状态确认。今天没有执行 SetEID，也没有部署自动发现与动态端点管理服务。这一轮证明的是固定 EID、单 Target 条件下的通信。

## 4. 第一笔 GetEID 超时：这一次请求已经到 Target 了

配置完成后，单独发送只读 GetEID。第一次执行的仍是原部署包里的工具：

```sh
# 执行端：i.MX93。此命令会产生实际 I3C 请求。
/opt/frdm/deployments/mctp-slim-20261125-run01/bundle/mctp-pldm-request control 1 9 get-eid
```

参数依次表示 MCTP Control、network 1、目的 EID 9、Get Endpoint ID。Host 本地 EID 已预先设为 8。

Host 结果是 TX 增加 1 包、8 字节，RX 没有增加，等待后没有得到 EID。到这里还不能断言“IBI 丢了”，因为不知道 Target 是否已经生成响应。于是没有马上重发，而是先看 Target 状态。

操作员随后提供的截图把故障位置往后推了一步：

```text
rx_packets=1, irq_count=1
rx_errors=0, rx_pec_errors=0，PEC=OK
EID 8 → 9
kind=CONTROL, cmd=0x83
pending_tx=0, tx_packets=0, ibi_submitted=0
```

这张截图提供的是 Target 侧观测。它表明请求已经收到、PEC 已通过，也进了 Control 解析；但读到的命令码是 `0x83`，正确的 GetEID 应为 `0x02`。没有排队响应，就不会有用于通知响应就绪的 IBI。

因此，本轮超时发生在响应生成之前。继续围着“IBI 为何没来”排查，会跳过前面已经出现的字节错位。

### 4.1 多出来的 0x00，是谁加的

沿着 Host 工具和本地 Linux `af_mctp.c` 对照，问题出在 socket 接口契约。

工具内部为了统一构造和解析，把消息放成了这样的布局：

```text
req[0]：MCTP message type，Control 为 0x00
req[1]：Control 请求位与 Instance ID
req[2]：Control 命令码，GetEID 为 0x02
```

这个内部布局可以保留，但不能原样作为 `sendto()` 的正文。

本地内核的 AF_MCTP 发送路径先从 `sockaddr_mctp.smctp_type` 取类型字节，放进 skb，然后才复制用户传入的消息正文。旧工具同时做了两件事：

```c
dest.smctp_type = req[0];
sendto(fd, req, req_len, 0, ...);
```

类型既放在地址结构里，又在正文开头带了一份，于是重复了。

用这次 Target 读到的 `0x83` 作字节位置示意，可以重建出下面的错位关系。这是根据源码与对端解析结果说明布局，不是额外抓到的一段完整总线原始数据：

```text
正确消息：00 | 83 | 02
          类型 请求头 命令码

旧工具路径：00 | 00 | 83 | 02
          内核  工具正文
          加类型 又带类型

Target 按 Control 结构解析，命令字段落到了 83 上。
```

昨天在地址阶段失败，数据还没进去；今天这笔已经通过地址和 PEC，才暴露出工具里的接口问题。两个阶段的失败都要留下，不能用今天的原因倒推昨天那笔 NACK。

### 4.2 发送少交一个字节，接收也要对应处理

修正集中在 `linux/vendor/intellif/tools/mctp-host/mctp-pldm-request.c` 的 socket 边界：

```c
/* req 内部仍然包含 type；交给 AF_MCTP 的正文不包含它。 */
size_t body_len = req_len - 1;
sendto(fd, req + 1, body_len, 0,
       (struct sockaddr *)&dest, sizeof(dest));
```

接收方向的契约相反：`recvfrom()` 返回消息正文，类型通过源地址结构里的 `smctp_type` 返回。原来的解析函数仍然需要“类型加正文”的内部布局，所以接收时先给类型留一字节：

```c
ssize_t n = recvfrom(fd, resp + 1, sizeof(resp) - 1,
                     MSG_DONTWAIT | MSG_TRUNC,
                     (struct sockaddr *)&from, &flen);
/* 先完成返回长度、地址长度等检查。 */
resp[0] = from.smctp_type;
size_t resp_len = (size_t)n + 1;
```

后续响应匹配和解析使用 `resp_len`。只修发送，很可能把问题从“Target 不认识请求”挪到“Host 不认识正确响应”，所以这两边要一起核对。

原来的离线 selftest 为什么没发现？它覆盖请求构造、响应匹配、解析与 CRC 检查，直接使用内部数组，没有真正经过 AF_MCTP socket 的收发边界。内部数组测试通过，并不能证明数组与系统调用之间的契约正确。

### 4.3 修正版先独立验证，再替换默认路径

修正版重新交叉编译为静态 AArch64 程序，先传到板端独立目录。核对 SHA-256、执行离线自检、确认 DA 和绑定状态后，只发送一笔 GetEID。

这次得到：

```text
response: 00 19 02 00 09 01 00
completion_code=0x00
eid=9 endpoint_type=0x01 medium_specific=0x00
REQUEST_EXIT=0
```

这是工具按“类型加正文”恢复后打印的响应，不包含 MCTP transport header 和 PEC。`00` 是 Control 类型，`19` 是本次响应头，`02` 是 GetEID 命令，后面的 `00` 是成功完成码，`09` 是 EID。

接口计数也有对应变化：

```text
TX：1 → 2，TX bytes：8 → 15
RX：0 → 1，RX bytes：0 → 11
tx_errors / rx_errors / rx_crc_errors / rx_length_errors：均为 0
```

所以修正版这笔增加的是 TX 7 字节、RX 11 字节。这些统计包含 4 字节 MCTP 头，不包含 PEC；不能拿 `tx_bytes` 直接当 I3C 数据长度。

正确 GetEID 消息为 3 字节，加 MCTP 头得到 7，再加 PEC 才是 8 字节 I3C 数据。旧请求多一个类型字节，因而之前统计成了 TX 8。源码解释、计数差值和 Target 的错位解析能互相对应。

独立目录验证通过后，再按要求替换板端部署包里的默认程序，并更新 `SHA256SUMS`；旧程序和清单另留了备份。这个顺序很有必要：独立路径下跑通，不等于平时直接敲工具名就已经在用新版。

两版程序的指纹是：

```text
旧版：93017c34021d85e3a91c7756ff53323e8dbdd1af5033cd1962b4d3aeb9fe5ab2
新版：e3717b9327c1d886655bcb82f418ffeccbd518345ee0630bbdb6d1c69083ca1b
```

板端默认路径已替换，整包校验通过。不过本地原始打包目录仍保留旧版，这一点没有随着板端替换自动解决。正式交付前还要从确定的源码重新打完整包，不能直接拿旧 bundle 再部署一遍。

## 5. 7 项只读查询，实际跑通了哪些内容

GetEID 通过后，继续逐项执行剩下 6 个只读查询。每一步都先保留响应与退出码，再核对 Host 收发计数，不一口气连续刷屏。

下表命令均在 i.MX93 上执行，前缀是板端已替换的 `mctp-pldm-request`。`control` 选择 MCTP Control，`base` 选择 PLDM Base；后面的 `1 9` 仍是 network 与目的 EID。

| 序号 | 命令参数 | 本轮结果 |
|---|---|---|
| 1 | `control 1 9 get-eid` | EID 为 9，完成码 `0x00` |
| 2 | `control 1 9 get-version` | 返回 4 个 MCTP 版本条目 |
| 3 | `control 1 9 get-types` | 声明支持 `0x00` Control 和 `0x01` PLDM |
| 4 | `base 1 9 get-tid` | TID 为 1 |
| 5 | `base 1 9 get-types` | PLDM 类型位图支持 Base，类型 0 |
| 6 | `base 1 9 get-version` | Base 1.1.0，版本列表 CRC32 校验通过 |
| 7 | `base 1 9 get-commands` | Base 1.1.0 命令位图支持 `0x01`～`0x05` |

后 6 项均为完成码 `0x00`、退出码 0，每项 TX 增加 1 包、RX 增加 1 包；这 6 笔期间收发错误、PEC 错误、长度错误和丢包计数均无增量。

PLDM Base 的版本返回里，版本字段为 `00 f0 f1 f1`，工具解析为 1.1.0；版本列表 CRC32 的字节为 `ba be 9d 53`。这里的 CRC32 是 PLDM 版本响应内部的校验，和 I3C transport 每包末尾的 PEC 是两回事。

命令位图开头为 `3e`，二进制 `00111110`，置位的是 1～5，对应 `0x01`～`0x05`。能力声明说明对端支持这些命令，不代表今天已经执行了其中的状态修改命令。例如 SetTID 并没有因为这个位图被置位就算验证过。

### 5.1 回包经过了怎样一条链

在当前实现中，一次查询走的是：

<pre class="mermaid">
sequenceDiagram
    participant U as Host 请求程序
    participant K as AF_MCTP / MCTP core
    participant D as mctp-i3c / Controller
    participant T as Target endpoint
    U->>K: sendto：类型在地址结构，正文在 buffer
    K->>D: 路由、Tag、MCTP 包
    D->>T: Private Write：MCTP 包 + PEC
    T->>T: 校验、处理命令、排队响应
    T-->>D: IBI，MDB 0xAE
    D->>T: Private Read
    T-->>D: 响应包 + PEC
    D->>K: 按实际长度校验 PEC，再上送
    K-->>U: recvfrom：正文与 smctp_type
</pre>

今天收到了成功响应，结合当前代码，基础请求和响应链路已经形成闭环。上图描述的是实现路径；它不等于本轮已经归档了一份逐边沿的完整波形。

读路径还有昨天移植时处理过的一个细节：Controller 请求的读长度是上限，Target 可以短读结束。binding 要用 `actual_len` 作为本次实际收到的长度，再取末尾 PEC 校验，不能用缓冲区容量或请求上限去校验没收进来的字节。今天这些短响应通过了这条路径，但 69 字节满包响应仍需另测。

到这一步，首轮固定 EID 的 MCTP Control / PLDM Base 目标有了实板结果。传感器值、PDR、FRU、固件更新等业务今天没有接入；连续压力和异常恢复也不能由这 7 项查询代替。

## 6. 做边界测试之前，先把长度单位算清楚

后半段开始验证包长与分包。这里一共有四个容易混用的长度：

1. 测试工具 `len` 参数指定的测试数据长度。
2. 完整 MCTP 消息长度，包含一次 message type。
3. 单个 MCTP 包长度，包含该包自己的 4 字节 transport header。
4. 交给 I3C Private Transfer 的数据长度，另含 1 字节 PEC。

今天 echo 测试的消息内容是：

```text
7E | CC DE F0 | 00 01 02 03 ...
类型  测试标识     len 个递增数据字节
```

也就是：

```text
MCTP message length = 1 + 3 + len = len + 4
每包最多装 64 字节 MCTP payload
I3C 数据长度 = 4 字节 MCTP 包头 + 本片 payload + 1 字节 PEC
```

这里把带 PEC 的长度写成“69 字节包”是实验里的简称，不包含 START、地址帧、ACK/T-bit 和 STOP 等总线开销。

| `len` 参数 | 完整消息长度 | 64 字节 payload 下的分片 | 每次 I3C 数据长度，含 PEC |
|---|---:|---|---|
| 59 | 63 | 63 | 68 |
| 60 | 64 | 64 | 69 |
| 61 | 65 | 64 + 1 | 69 + 6 |
| 125 | 129 | 64 + 64 + 1 | 69 + 69 + 6 |

从 60 增到 61 个测试数据字节，只多了一个字节，线上却多出一个完整的 I3C 事务。这正好能把“单包长度”与“连续接下一包”的问题拆开。

### 6.1 64 字节消息的单包布局

本次软件观测到的单包头为：

```text
01 09 08 C8 | 7E CC DE F0 00 01 ... 3B | PEC
```

前四字节依次包含版本、目的 EID、源 EID和分包控制信息。在这组实测中 Tag 为 0：`C8` 表示 SOM 与 EOM 都置位、序号为 0、TO 为 1。一包既是开头又是结尾，完整消息一次装下。

`7E CC DE F0` 加 `00` 到 `3B` 共 64 字节；再加 4 字节包头与 1 字节 PEC，总计 69。

PEC 由 transport 根据地址字节和本包数据计算。DA 为 `0x44` 时，写方向参与 PEC 的地址字节为 `0x88`，读方向则为 `0x89`。地址参与校验，不等于地址字节也计入上面的 69 字节数据长度。

### 6.2 65 字节消息为什么拆成 69 + 6

测试数据增加到 `00`～`3C`，整个消息长 65 字节。Host 内核生成了两片：

```text
第一片：01 09 08 88 | 7E CC DE F0 00 01 ... 3B | PEC
第二片：01 09 08 58 | 3C                       | PEC
```

第一片的 `88` 表示 SOM=1、EOM=0、序号 0；第二片的 `58` 表示 SOM=0、EOM=1、序号 1；TO 和 Tag 保持一致。

类型字节只在整条消息开头出现一次，第二片不会再补一个 `7E`。否则重组出的正文会多一个字节。

第一片是 `4 + 64 + 1 = 69`；第二片是 `4 + 1 + 1 = 6`。每片有自己的 transport header 和 PEC，Target 需要把两片的消息内容重组起来，再交给上层处理。

上面两片来自 Host 软件观测。后面会看到：第二片虽然在 Host 网络路径里生成了，但地址 NACK 时，它的数据并没有因此自动出现在物理总线上。

## 7. Host 内核回环通过了，但它不经过 I3C

先做 Host 内核回环，是为了把 Linux MCTP 的分包重组和真实硬件链路拆开验证。

实验是在 i.MX93 当前运行内核上做的。通过 `unshare -n` 创建临时 network namespace，在里面启用 `lo`、把 MTU 设为 68、配置本地 EID 8，再运行 echo 进程和请求进程。

下面是当时使用方式的核心部分，仅作为实验复现记录。这段在板端独立 namespace 里改变网络配置并走本地 socket，不会向 Target 发送 I3C 数据：

```sh
unshare -n sh
cd /root/mctp-get-eid-fix-20261126-iG7pGX
ip link set lo mtu 68 up
/opt/frdm/modules/6.6.36-slim-mctp/mctp address add 8 dev lo
./mctp-echo &
echo_pid=$!
trap 'kill "$echo_pid" 2>/dev/null || true' EXIT
sleep 0.15
for n in 59 60 61 125; do
    timeout 5 ./mctp-req net 1 eid 8 len "$n" || break
done
```

两只 AF_MCTP socket 分别负责请求与应答。目的 EID 是本地 8，内核走本地路由；MTU 68 使相应长度覆盖 1 包、2 包和 3 包的情况。echo 收到消息后应答，请求端逐字节比较返回数据。

四组输出均为 `byte_compare=PASS`：

```text
data_len=59  message_len=63   PASS
data_len=60  message_len=64   PASS
data_len=61  message_len=65   PASS
data_len=125 message_len=129  PASS
```

这说明当前 Host 内核在这组参数下可以完成 MCTP 本地发送、分包、重组和回送。它没有验证 I3C PEC、Controller 时序、Target 接收与重组，也没有验证反向 IBI 通知。

所以，129 字节回环通过，不能写成“129 字节已通过 I3C 发到 Target”。真实链路上的三包实验今天没有完成。

## 8. 真实 I3C 边界：68、69 字节发出，第二片失败

真实链路使用 EID 9，让请求经过 `mctpi3c0`。测试脚本用 AF_PACKET 观察 Host 生成的 MCTP 包，同时比较 `mctpi3c0` 驱动统计。

这里使用的是测试 echo 消息，Target 没有该 echo handler。因此脚本并不期待收到 echo 响应；内部请求进程超时返回 124，是这组单向发送观察的预期条件。是否发送成功，还要单独看驱动统计与内核错误。

| 消息长度 | Host 生成的包 | 驱动统计变化 | 本轮能确认的结果 |
|---:|---|---|---|
| 63 | 67 字节 MCTP 包，加 PEC 为 68 | TX +1，TX bytes +67，TX errors +0 | Host 驱动报告一次发送成功 |
| 64 | 68 字节 MCTP 包，加 PEC 为 69 | TX +1，TX bytes +68，TX errors +0 | Host 驱动报告满长度单包发送成功 |
| 65 | 68 与 5 字节 MCTP 包，加 PEC 为 69 + 6 | TX +1，TX bytes +68，TX errors +1 | 第一片成功，第二片失败 |

前两项的脚本输出 `HOST_I3C_TX_PASS`，对应的是 Host 发送路径通过。Target 实际收到了多少字节、满包 PEC 是否通过，需要 Target 日志交叉核对；也没有在这一步读回一包 69 字节响应。

第三项观察到了两个 `HOST_PACKET`，脚本却报：

```text
Driver successful-transfer counters do not match capture
```

这是有用的失败：网络路径捕获到了两个待发送的包，驱动只完成一个，另一个产生 TX error。软件捕获没有被误当作物理发送成功。

### 8.1 sendto 返回，并不是最后一次成功判定

当前 transport 的发送经过 netdev 与发送线程。应用把数据交给内核后，后续 I3C 操作可以在异步路径里才失败。

在 `mctp_i3c_xmit()` 中，真正调用 `i3c_device_do_priv_xfers()`；返回 0 才增加 `tx_packets` 和 `tx_bytes`，否则增加 `tx_errors`。发送线程完成后再释放 skb。

所以这里至少有三个观察点：

```text
应用 sendto 被接受
    ↓
Host 网络路径生成 / 观察到包
    ↓
Controller 完成 Private Transfer，驱动更新成功或错误统计
```

再往后才是 Target PEC、重组与业务处理。今天第二片的错误，就出现在前两层已经经过、硬件传输尚未成功的地方。

### 8.2 失败后，小 GetEID 仍然通过

第一次两片测试失败后，又单独验证了一笔 GetEID，返回成功、EID 仍为 9。Host TX 和 RX 各增加 1 包，已有的 `tx_errors=1` 没有继续增加。

这说明当时失败没有让所有后续通信都永久失效，也没有证据要求立刻重做整套部署。但它不能证明所有恢复场景都没问题，更不能代替丢失分片后的 Target 重组状态检查。

## 9. 两片间隔约 1 秒：一个缩小范围的实验

看到第二片失败，接着按要求做了一个很小的对照：保持两片内容，第一片完成后等 1 秒，再送第二片。

具体做法没有往正式 AF_MCTP 驱动里加 `sleep`，而是用临时 Python 脚本经 AF_PACKET raw socket，把已知的两片分别交给现有 `mctpi3c0` 发送路径。脚本先等第一片的驱动统计出现完成，再休眠，然后发第二片。

这与正常应用只调用一次 `sendto()`、内核连续完成分片发送的路径有区别。它适合观察“给接收端留出时间，结果会不会变化”，还不足以证明一个正式的软件修复。

结果如下：

```text
FRAG 1：MCTP 68 字节，I3C 数据含 PEC 69 字节
        tx_packets +1，tx_bytes +68，tx_errors +0

FRAG 2：MCTP 5 字节，I3C 数据含 PEC 6 字节
        tx_packets +1，tx_bytes +5，tx_errors +0

OBSERVED_GAP_S=1.002566
HOST_I3C_TX_PASS
```

约 1.003 秒是脚本观测到第一片统计完成、到提交第二片之间的时间差，不是逻辑分析仪测出的 STOP 到下一次 START 间隔。

这个结果使“连续两片之间的准备时序”成为下一步值得重点检查的方向。它还没有告诉我们：

- 第一片之后哪个状态迟迟没有恢复，是接收使能、FIFO、buffer 归还，还是其他条件。
- 真正需要的时间是多少，是否与主循环调度或协议处理有关。
- 第二片在 Target 上是否通过 PEC，两片有没有形成完整消息。
- 其他长度和连续多条消息下能否复现同样的规律。

因此，1 秒只是这次诊断实验的参数。正式发送路径没有因此增加固定延时，也没有把它当作分包问题已修复。

### 9.1 Host 发两片，中间要等 IBI 吗

这也是今天讨论得很具体的一个问题。

在当前实现中，Host → Target 的每片是一次 Private Write。Host 继续写下一片，并不会先等一个表示“可以写下一片”的 IBI。能不能接受这次访问，先体现在地址应答与传输结果上。

Target → Host 则不同：Target 有数据待 Host 读取时，通过带 MDB `0xAE` 的 IBI 通知；Host 的 `mctp_i3c_ibi_handler()` 识别后进入读路径，再发 Private Read。

```text
Host → Target 分片：
Private Write 第 1 片 → Private Write 第 2 片

Target → Host 数据：
Target IBI 通知 → Host Private Read
```

今天第二片被 NACK，不能用“少等了一个 IBI”直接解释。应先检查第一片完成后，对端为什么还不能应答下一次写地址。如果以后设计额外的流控或重试，也需要单独明确接口与错误处理，不能靠实验间隔含糊带过。

## 10. 重新枚举后再试一次，第二片 NACK 仍能复现

间隔实验之后，又按步骤重建环境：卸载 transport，重新枚举，确认 PID、DA、DCR，再绑定 `mctp-i3c`、启用 IBI，恢复 Host EID 8、Target EID 9 路由和 PID 邻居。

这一步完成时，接口 TX/RX 都是 0，没有先夹带 GetEID 或其他分片。也就是说，后面的测试有一套较清楚的起点。

随后只发一次不加延时的 69 + 6 字节组合。结果仍然是第一片成功，第二片失败：

```text
COUNTER_DELTAS:
    tx_packets +1
    tx_bytes   +68
    tx_errors  +1
    rx_packets +0

I3C XFER FAIL rc=-6 recovery_required=0
I3C XFER cmd=1 addr=0x44/W stage=address-nack rc=-6 stop_rc=0
```

这里的 `stage=address-nack` 很关键。Host 已生成第二片 `01 09 08 58 3C`，但寻址 `0x44/W` 时没得到应答，不能说第二片的 `3C` 和 PEC 已经传给 Target。报错位置比“重组失败”更靠前。

`stop_rc=0` 表示本次错误后的 STOP 处理返回成功；`recovery_required=0` 是当前驱动的恢复判断。它们不能用来反推 Target 软件状态一定正常。

重新枚举后仍出现相同现象，说明这次问题并没有随着这一轮 Host 环境重建而消失。至于 Target 的重组上下文、buffer 状态是否也恢复到了完全相同的起点，仍需对端日志证明。

18:56 左右停止测试时，保持了枚举、绑定、IBI 与路由状态，没有继续重试。本文整理也没有重新操作板子。

## 11. 今天到底验收到了哪一层

| 层次 / 项目 | 截至本轮结束的状态 |
|---|---|
| Host 内核与工具 | `6.6.36-slim-mctp` 已运行，AF_MCTP 与 I3C transport 可用；请求工具收发契约已修正 |
| I3C 枚举 | 多轮通过；进入 MCTP 模式后 PID、DA、BCR、DCR 读回符合本轮配置 |
| transport 与固定拓扑 | `mctp-i3c` 绑定、ENEC、接口 up、EID 8 → 9 路由和 PID 邻居通过 |
| MCTP Control | GetEID、GetVersion、GetMessageTypeSupport 三项端到端通过 |
| PLDM Base | GetTID、GetPLDMTypes、GetPLDMVersion、GetPLDMCommands 四项端到端通过 |
| Host 本地分包重组 | 63、64、65、129 字节消息回环，数据比较通过 |
| 真实链路 68 / 69 字节单包 | Host 驱动报告发送成功；Target 长度与 PEC 交叉验证待补 |
| 真实链路 69 + 6 连续发送 | 失败，第二片地址 NACK；重枚举后复现 |
| 同样两片，间隔约 1 秒 | 临时重放路径下 Host 两次发送成功；Target 重组未确认 |
| 真实三包消息 | 未完成验证 |
| 69 字节满包响应读取 | 未完成验证 |
| 压力、异常与恢复 | 尚未系统验证；有一次分包失败后 GetEID 成功的局部结果 |
| 传感器 / PDR / FRU / 固件更新 | 不在今天的验证范围 |

今天的结论比昨天前进了一大步：小消息已经能从 Host 命令一路走到 Target，再带着有效响应回来。下一步不需要从“内核里有没有 MCTP”重新查起，要从连续两次 Private Write 的衔接处继续。

## 12. 下次接着做，要先留住第一包之后的现场

下一轮优先把两端观测对到同一笔事务上。需要留的并不只是第二片失败那一行错误，而是第一片结束到第二片地址应答之间发生了什么。

Target 侧要看第一片实际接收长度、PEC 结果、SOM/EOM 和序号；随后谁持有 buffer，何时归还，接收中断何时清除，RX 何时重新使能，以及主循环是否在等待一个不会到来的条件。这些都是待查方向，今天还没有哪一条被确认为根因。

Host 侧保留每片的提交与完成、驱动统计和错误阶段；有条件时抓一组连续写与一组有间隔写的波形，把第一片 STOP、下一次 START、地址应答和 Target 日志时间点对应起来。不要只拿 Host 软件生成的包替代波形。

待第二片能够连续接收，再检查 Target 是否只把重组后的完整消息交给上层、超时后能否清理半包、下一条正常消息能否继续。同时安排真实 69 字节响应和三包消息，补齐双向边界。

交付前还有一件独立的事：从确定的源码状态重新打包，记录双端指纹，冷启动重新做枚举和 7 项查询。今天 Host 工作区的基底提交是 `c58292616eeed014c605d572ae3f58a5c7095b43`，MCTP 移植与今天的工具修正仍有未提交改动；这个提交号不能单独代表上板运行的全部源码。Target 当前固件的完整指纹，本轮材料也没有补齐。

## 13. 留给后续复查的材料

关键原始输出整理在[本轮证据摘录](evidence.txt)，包括修正版 GetEID、6 项后续查询、内核回环、包长边界、有间隔发送、重新枚举与最终 NACK。附件保留各阶段的结果，不把所有 PASS 合并成一次完整链路通过。

本地工程中的主要定位入口如下。路径以 `code_wave_hw` 工程顶层为基准，便于之后在工作区里查找；源码仍以实际工作树和产物指纹为准。

| 内容 | 工程内位置 |
|---|---|
| 历史 open 枚举记录 | `deployments/open-2khz-3placeholder-20260924/` |
| 11 月 18 日枚举成功与产物指纹 | `verification/i3c-poll-write4-20261118/run03/` |
| 本轮 Host 工作区 | `worktrees/imx93-slim-mctp-20261125/` |
| 请求工具修正 | 工作区下 `linux/vendor/intellif/tools/mctp-host/mctp-pldm-request.c` |
| AF_MCTP 类型字节契约 | 工作区下 `linux/net/mctp/af_mctp.c` |
| I3C transport、PEC、异步统计 | 工作区下 `linux/drivers/net/mctp/mctp-i3c.c` |
| 修正版与 GetEID 结果 | `build/mctp-get-eid-fix-20261126-iG7pGX/` |
| 后续 6 项只读查询 | `build/mctp-readonly-regression-20261126-tncB2R/` |
| 回环与包长边界 | `build/mctp-boundary-20261126-zN2Jjh/` |
| 约 1 秒间隔实验 | `build/mctp-fragment-gap-1s-20261126-Ohl2Dk/` |
| 重枚举与无间隔复现 | `build/mctp-reenumeration-20261126-t2ENIx/` |

板端日志目录里出现了早于当天的日期，本轮日历日期按会话记录使用 2026-11-26，内核方括号时间按启动后的单调时间理解。不能拿板端目录名去重排今天的实验顺序。

今天先记到这里。下次打开这份记录，先确认使用的是修正版请求工具，再对齐第一片结束后的 Target 状态，从第二片地址 NACK 这个现场继续。
