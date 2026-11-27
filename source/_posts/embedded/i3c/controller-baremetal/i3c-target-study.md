---
title: DWC MIPI I3C Target 开发学习文档
date: 2026-11-25
permalink: 2026/11/25/i3c-target-study/
categories: ["嵌入式开发","I3C","控制器与裸机开发"]
tags: ["i3c","designware","target","driver","dma","tutorial"]
toc: true
max_depth: 3
mermaid: true
description: 对照 DWC Databook 学习 Target 的初始化、收发、中断、IBI、Hot-Join 和错误恢复。
---

版本：初稿 v0.1。依据 **DWC MIPI I3C Controller and Target Databook，1.05a-lca00，August 2025**。本文将已确认的大纲展开为 Target 开发教程；核心路径为 APB Target SDR，扩展功能与 Autonomous Target 独立说明。

<!-- more -->

先查寄存器、重要作用与地址证据表（`i3c-target-registers.md`），再阅读本教程。 普通端口与扩展命令的详细控制流见EXT 与 Vendor CCC 专题（`i3c-target-ext-tx-rx.md`）。规划依据见学习大纲（`i3c-databook-study-outline.md`）。唯一技术来源为当前目录的 Databook（`../DWC_mipi_i3c_v111_basic_databook.pdf`），以下页码均为印刷页码。

## 阅读约定与资料边界

按硬件配置选择收发路径，参见 TX/RX 路径与 RTL 条件专题（`i3c-target-transfer-modes-illustrated.md`）（含六张数据流配图）。

本文的“Controller”表示总线上的当前控制方；“Target 硬件/IP”表示当前学习对象；“Target 应用”表示本地软件或外部应用逻辑。原书某些句子将 Target IP 也称为 controller，阅读时必须按角色判断。

- **原文确认**：附章节、页码及原文短引；表格共享其紧邻的来源说明。
- **工程推导/示例**：由已列事实推导，注明条件，不声称是手册原句或已经执行的实验。
- **待核实**：缺少配套文档、具体配置，或原书存在相互不一致的描述。

本书 p.8 明确说明完整寄存器、信号和参数章节已迁移至 Reference Manual；p.29 将时钟与复位要求指向 User Guide。因此本文提供机制、顺序、位域示例和调试方法，**不提供猜测的 MMIO 地址，也不将学习步骤包装成已在芯片上验证的驱动**。实际 Target 类型和配置仍须与工程核对。

> “Register Descriptions, and Internal Parameter Descriptions chapters are available in the Reference Manual from 1.03a-lca00 release onwards.” — 修订记录，p.8，原句节选。

## 第 1 章：Target 定位与硬件架构

### 1.1 从职责理解 Target

Controller 决定何时发起普通读写；Target 准备好数据、接收空间和响应资源，在被寻址时执行收发。Target 的“发起 TX Command”是准备回应 Controller Read，不表示本地软件能够随时主动发起一笔普通总线写。主动通知另走 HJ/IBI 机制。

| 总线动作 | Target 视角 | 应用首先准备什么 |
|---|---|---|
| Controller Private Write | Target Receive | RX 空间、响应队列空间、数据消费能力 |
| Controller Private Read | Target Transmit | 有效 TX 命令、TX 数据、响应队列空间 |
| Controller CCC | 状态查询或控制操作 | 对应身份/能力来源，或 Vendor CCC 应用处理资源 |
| Target SIR/IBI | 主动请求 Controller 关注 | 动态地址、事件使能、MDB/可选 payload、请求状态管理 |

依据：§2.1.6 p.41；§4.2.5 pp.196–197；§4.2.9 pp.213–215。

> “The Target is responsible for responding to the commands, transmitting or receiving data to/from the Controller” — §2.1.6，p.41，节选。

### 1.2 三种 Target 路径不能混用

| 配置 | 应用连接方式 | 本文阅读路径 |
|---|---|---|
| APB Target | 处理器通过 APB 操作寄存器、命令和数据端口；可配置外部 DMA | 第 1–7 章为基础，第 8 章按功能选读 |
| Target-Lite | 非处理器原生接口；详细说明另有 Target-Lite 文档 | 理解协议职责后，对照对应产品手册 |
| Autonomous Target | Target-Lite 协议引擎配合事务层、AHB Manager，将总线访问转换为应用侧访问 | 第 9 章；不能直接套用 APB Target 软件流程 |

Secondary Controller 暂时作为 Target 工作时，还有控制权请求等职责；这不等于普通 Target-only 配置。依据：§2.1.6 p.41；§2.2 pp.48–53；§4.1 p.176；§4.3 p.232。

> “Target Lite: Enables the DWC_mipi_i3c_v111_basic to support a non-processor Interface.” — p.41。
>
> “This Autonomous Target facilitates direct data exchange with the end application through an AHB Manager Interface” — p.232。

### 1.3 数据、命令、响应是三类资源

以下为依据图 2-1、§2.1.1、§4.2.5 绘制的教学简图，不表示实际 CDC 连接或所有内部模块。

<pre class="mermaid">
flowchart LR
    C[外部 Controller] &lt;--&gt;|SDA/SCL| P[Target 协议引擎]
    P &lt;--&gt; T[Target 事务处理]
    T --&gt; R[RX FIFO]
    X[TX FIFO] --&gt; T
    Q[TX Command] --&gt; T
    T --&gt; S[Response Queue]
    A[Target 应用 / APB] --&gt; Q
    A --&gt; X
    R --&gt; A
    S --&gt; A
    A &lt;--&gt; B[身份、状态与事件寄存器]
    B &lt;--&gt; T
</pre>

FIFO 搬运 payload；命令说明待发送事务；响应说明已发生的事务。只清空 RX FIFO 而不回收响应，仍可能因 Response Queue 满而 NACK；只填 TX FIFO 而没有有效命令，也不能满足普通读的 ACK 条件。后两项由 pp.196–197 的条件列表直接支持。

> “Response Queue is not full and it has some space to hold the response for the current transfer.” — p.196。
>
> “TX Command is valid in the Command Queue” — p.197。

公共架构还说明普通 TX/RX 数据共用一个访问地址：写入压入 TX，读取取出 RX。因此，对数据端口执行普通存储器式的 read-modify-write 会改变 FIFO 状态；应使用明确方向的端口操作。前半为原文，后半是工程推导。〔§2.1.7.2，p.43〕

> “writing into that port pushes data into transmit buffer and reading fetches data from receive buffer.” — p.43。

### 1.4 时钟与 I/O 的角色边界

§2.1.3.3 p.39 特别指出 **APB Target 没有 core clock**，所以不能将图 2-1 中所有时钟域原样当作 APB Target 的实际接口清单。学习 APB Target 时，应分别确认 APB 应用域、SCL 相关协议逻辑、DMA 接口及同步机制。与此同时，p.213 的时间戳说明和 p.229 的 read-abort 定时又出现 `core_clk` 字样；这属于需要配置级资料澄清的时钟命名问题，本文不据此计算 Target 定时寄存器值。

> “For APB target, the core clock is not present.” — p.39。

I2C Target 模式不支持 clock stretching；glitch filter 应位于 IP 外部，IP 只提供使能/禁用控制。因此不能用“软件来不及就让 Target 拉低 SCL 等待”作为数据服务策略。前两项为 §4.2.1 p.178 原文，最后一项为工程结论。

> “In I2C mode of operation, the clock stretching is not supported by Target.” — p.178。
>
> “The spike filter required for I2C device should be present outside the Target interface” — p.178，节选。

**自检：** 为什么 TX FIFO 有数据仍然读不到？答：还要检查有效 TX Command、发送开始条件及响应资源，见第 4 章。

## 第 2 章：上电、身份与地址生命周期

### 2.1 身份信息由哪里提供

| 信息 | 含义与来源 | 应用必须确认的条件 |
|---|---|---|
| 静态地址 | `static_addr_en/static_addr`，可由 `DEVICE_ADDR` 对应字段覆盖 | 在 Target 使能前设置；静态地址有效与动态地址有效是两个状态 |
| PID | 48 位身份信息，涉及 `SLV_MIPI_ID_VALUE`、`SLV_PID_VALUE` 和 strap | 检查 `IC_SLV_UNIQUE_ID_PROG` |
| BCR | 总线特征/能力，涉及配置参数及 `SLV_CHAR_CTRL` 的相关项 | 宣告的能力必须匹配生成配置，不能仅凭软件希望启用 |
| DCR | 设备特征类别，由参数或 `slv_dcr`/`SLV_CHAR_CTRL.DCR` 提供 | 可编程性同样受配置影响 |
| GETMXDS 数据 | 速度及周转信息，涉及参数、strap 和 `MAX_DATA_SPEED` | 检查 `IC_SLV_MXDS_PROG`；部分读周转信息仍来自参数 |

依据：§4.2 p.177；表 4-2 pp.181–182；§4.2.4.3 pp.187–188；§4.2.4.5 pp.191–192。

`IC_SLV_UNIQUE_ID_PROG=0` 时，PID 的 44 位由 RTL 配置决定，仅 Instance ID `[15:12]` 可运行时调整；为 1 时，可通过 strap/寄存器提供 PID 和 DCR。因此不能写一个无条件覆盖 PID 全字段的初始化流程。〔pp.191–192〕

> “44 bits are configured during RTL generation and only Instance ID (Bits[15:12]) is changeable at run time.” — p.191。

### 2.2 从 Target 看 DAA

| Target 状态/收到的 CCC | 硬件行为 | 应用理解 |
|---|---|---|
| 有静态地址、尚无有效动态地址 | 可参与 ENTDAA、SETDASA、SETAASA | 静态身份决定可用分配路径，仍需满足 HJ 等其他条件 |
| 只有动态身份、尚无动态地址 | 参与 ENTDAA | 不应期待 SETDASA 匹配不存在的静态地址 |
| 动态地址已有效 | 停止响应上述三类分配 CCC，直到 RSTDAA 清除动态状态 | 重复 ENTDAA 不等于再次枚举已分配设备 |
| SETNEWDA | 替换当前动态地址 | 后续通信地址发生变化 |
| RSTDAA | 清除 `DEVICE_ADDR.DYNAMIC_ADDR_VALID` | 不可将此动作直接等同于 POR 或强制返回纯 I2C 模式 |

依据：§4.2 pp.177–179；表 4-8 p.186。最后一行“不等同”是结合 p.178 模式说明的工程边界。

> “Once the dynamic address is assigned and valid, then the Target stops responding to the ENTDAA, SETDASA, and SETAASA CCC commands” — p.179，节选。
>
> “Clears DEVICE_ADDR.[DYNAMIC_ADDR_VALID].” — 表 4-8，p.186，RSTDAA 行。

Target 应用准备身份并观察地址状态，不需要代替硬件逐位实现 ENTDAA，也不需要管理 Controller DAT/DCT。p.43 明确列出 DAT/DCT 等深度不适用于 Target-only。

### 2.3 不要把“无 DA”“纯 I2C”“等待 I3C 识别”混成一个状态

p.178 将 `mode_i2c=1` 描述为强制 I2C，改变角色需要复位并将输入改为 0。`mode_i2c=0` 下又描述 adaptive selection、DA 分配及滤波器变化。对于 adaptive 路径，首次 `7'h7E/W` 还会影响滤波器控制；这和 DA 是否已有效并非同一事件。

> “The adaptive selection is possible only when the input pin "mode_i2c" is tied to 0.” — p.178，引号样式统一。
>
> “Glitch filter is disabled upon receiving first I3C Address Header (7'h7E,w).” — p.178。

工程上至少分别记录：强制模式输入、是否识别 I3C 头、动态地址是否有效、HJ 使能/已请求状态、Target 是否真正启用。该记录方案是对原书行为的归纳，不是新增硬件寄存器定义。

### 2.4 使能顺序：机制级流程

以下为跨章节综合，适用于普通 APB Target 的学习/实现检查，不含未经确认的地址、复位位或默认值。

1. 确认 IP 角色、虚拟 Target/HDR/Vendor CCC 等生成参数，选择对应描述符格式。
2. 按 SoC/配套 User Guide 完成时钟、复位及 I/O 集成；本 Databook 未给出完整上电序列。
3. 在 Target 禁用时设置允许修改的身份、能力、静态地址、HJ 策略和可选 PEC 分类控制。
4. 准备 RX/TX/Response 资源及中断或 DMA 服务；阈值必须与实际容量匹配。
5. 设置 `DEVICE_CTRL.ENABLE`，按实际配置确认 SCL/placeholder transfer 条件，不把写入成功当作协议逻辑立即可用。
6. 由 Controller 完成地址分配；应用观察相关状态，进入收发服务。

证据：身份配置 pp.177、187、192；HJ p.212；使能 p.217；PEC 分类配置 p.227；资源条件 pp.196–197。

> “Enabling the Target depends on the availability of SCL.” — p.217。

当 `IC_REMOVE_PLACEHOLDER_XFER=0`，原书要求特定 POR 后禁用再启用场景提供 SCL，可通过以 STOP 结束的 placeholder transfer 完成；该事务被 Target 忽略。不要将这种配合要求误报为“Target 第一次传输必然丢数据”。〔p.217〕

**自检：** ENTDAA 没有看到本设备，先查身份类型、DA 有效状态、ENABLE 和 HJ 是否需要先发，而不是先检查 Host DAT 的软件写法。依据分别为 pp.177–179、212、217。

## 第 3 章：CCC 与 Target 应用责任

### 3.1 “硬件处理 CCC”不等于应用无需准备

表 4-1 所列 CCC 由 Target 内部处理，读数据可能来自配置、寄存器或端口。应用必须提前提供正确身份/能力，也要处理 CCC 引起的状态变化。Vendor-specific CCC 则有单独应用路径。

> “All the CCCs in Table 4-1 are handled within the Target controller without involving the target application.” — p.181。

| CCC 类别 | 典型命令 | 数据/状态路径 | Target 应用职责 |
|---|---|---|---|
| 身份查询/地址分配 | GETPID、GETBCR、GETDCR、ENTDAA | 参数、strap、身份寄存器 | 使能前准备正确来源，观察地址变化 |
| 事件控制 | ENEC、DISEC | 更新 `SLV_EVENT_STATUS` | 不向被禁止的事件重复提交请求 |
| 长度控制 | GET/SETMRL、GET/SETMWL | `SLV_MAX_LEN` 等配置 | 数据服务能力与宣告/协商长度一致 |
| 能力与速度 | GETCAPS、GETMXDS | 配置能力、可编程速度信息 | 明确 Defining Byte 和配置门控 |
| 活动状态/状态查询 | ENTAS、GETSTATUS | 活动端口与 `CCC_DEVICE_STATUS` | 保持应用活动信息，协同错误恢复 |
| Vendor Write | 广播或定向 Vendor CCC | RX 数据与响应信息 | 解析 CCC、Defining Byte、payload |
| Vendor Directed Read | 自定义读 CCC | 扩展命令匹配及对应 TX FIFO | 预装数据/匹配条件，完成后重新准备 |

来源：§4.2.4 pp.179–195。这里的职责为结合原书数据来源与收发规则的工程整理。

### 3.2 GETSTATUS 是通信双方共享的诊断窗口

`CCC_DEVICE_STATUS` 反映 Controller 通过 GETSTATUS 读取的信息。图 4-1 p.189 给出了本 IP 对 vendor-reserved 状态的使用，包括 buffer-not-available、data-not-ready、overflow、underflow、frame/PEC 等。**这是本 IP 的状态组织，不应推广为所有 I3C Target 的相同寄存器布局。**

> “The GETSTATUS vendor reserved fields are used by DWC_mipi_i3c_v111_basic Target to indicate to the controller the error conditions” — p.190，节选。

GETSTATUS 有 Defining Byte 形式；例如 `0x91` 的 PRECR 状态只适用于 Secondary Controller，Target-only 会 NACK。不能把所有合法协议子命令都当作本配置支持。〔表 4-12，p.190〕

### 3.3 SETMRL/SETMWL 不一定照收请求值

当 `IC_TGT_AUTO_UPDT_XFER_LEN_ATTR` 启用，p.195 描述了上下限处理：范围内采用 Controller 提供的值；高于上限时 `MAX_MODE=1` 使用最大支持值，=0 保留原值；低于下限时 `MIN_MODE=1` 使用最小支持值，=0 保留原值。这要求应用核对最终采用的值，而不是只记录 Controller 发来的请求。

> “If the received value falls within the Target's supported range ... it is used directly.” — p.195，省略中间括号说明。

### 3.4 Vendor CCC：接收与发送不对称

Vendor Write 沿 RX 路径进入应用。Directed 的 Defining Byte 可在响应信息中体现；Broadcast 的 Defining Byte 进入 RX FIFO，因为 IP 无法把它与一般 payload 区分。广播也不能像定向写一样在目标地址处逐设备 NACK；资源不足时可能不交付应用。〔pp.193–194〕

> “The Defining Byte (if any) for Broadcast Vendor Specific CCC is passed to Rx-FIFO as the Target cannot differentiate between Defining Byte and Data payload.” — p.194。

普通 Vendor Directed Read 使用表 4-22 的扩展命令匹配 CCC 与 Defining Byte，并要求对应扩展 TX FIFO 非空、Response Queue 有空间。应先准备数据，再提交命令；命令只服务一次读，下一次要重新准备。无完整 DATA_LENGTH 字段的这一格式，通过 FIFO 结束和 `DL_LSB` 描述最后 DWORD 有效字节。不能把它与 Format 2 有限长度命令混用。〔pp.194–195、221〕

> “the command is valid for only one Vendor Specific CCC Read transfer.” — p.195，节选。

**自检：** 自定义读第一次成功，第二次 NACK，优先检查命令是否已消费、对应 FIFO 是否重新装填，以及响应是否回收。依据 pp.194–195。

## 第 4 章：完成一次 Target 接收与发送

### 4.1 先选对描述符

表 4-19 p.218 的选择关系是读写教程的前置条件。未启用 Virtual Target 的普通 Private TX 使用表 4-20，普通响应使用表 4-21；开启 Vendor CCC 后还可能使用表 4-22。Virtual Target 使用 Format 2；§4.2.13.4 p.221 又明确 HDR-BT 的扩展 TX 也使用 Format 2。

> “If VIRT_TGT=1, the command and response structures ... are not used and not applicable.” — p.218，省略所引用的旧结构标题范围。

因此，下面的数值案例限定为：**普通 APB Target、SDR Private、无 Virtual Target、无 HDR-BT、无 PEC、资源足够**。实际功能参数不符合时应切换第 8 章的路径。

### 4.2 接收：ACK 是资源承诺

在可正常接受事务的 SDR 状态下，私有写地址 ACK 的两个资源条件是：RX 空闲位置达到 `RX_START_THLD`，且 Response Queue 有位置保存当前响应。ACK 后，硬件预期应用持续维持接收空间；不能把开始门槛理解为“这笔事务已经安全接收”。〔§4.2.5.2，p.196〕

> “Once the write address is acknowledged with ACK (accepted), the Target expects the RX FIFO space to be available until the end of transfer” — p.196，节选。

以 Controller 写入 8 字节为教学案例，应用处理顺序如下：

1. 确认 RX 和响应资源充足，已启用所需中断或数据服务。
2. Controller 寻址写；Target 硬件判断资源并 ACK，随后接收数据。
3. 若配置为可容纳整笔的小传输，可在完成后处理；长传输应在接收期间服务 RX 阈值/DMA，防止溢出。
4. 读取并保存响应，确认 `RX_RSP=1`、`ERR_STATUS=0`，按该格式的接收长度关联 RX 数据。
5. 取出有效 payload，交付应用；同时回收响应资源，准备下一笔事务。

第 3 步的两种服务方式由 p.196 明确给出，其余为依据响应表 pp.219–220 整理的操作流程。普通 FIFO 的字节端序和尾字节规则在当前 Target 章节未形成完整说明，实际搬运应核对 RM/UG，不直接移植 Controller DCT 的字节布局。

### 4.3 发送：TX Command 表示已准备回应读

普通 Private Read ACK 需要同时满足：

| 检查项 | 条件 | 不满足时的相关观察点 |
|---|---|---|
| 命令 | TX Command 有效 | `INTR_STATUS.READ_REQ_RECV_STS` 可提示无有效命令 |
| 数据 | TX FIFO 满足命令所需数据量或 `TX_START_THLD` | `CCC_DEVICE_STATUS.DATA_NOT_READY` |
| 响应 | Response Queue 非满 | 同样可能设置 `DATA_NOT_READY` |

完整来源：§4.2.5.3 pp.197–198。`DATA_NOT_READY` 在 NACK 后可能需要若干 SCL 时钟才设置，所以刚发生 NACK 的一次立即读取不一定看到最终状态。

> “requires a few SCL clocks to set after the Target NACK's the read transfer.” — p.198，针对 DATA_NOT_READY。

教学案例：准备 8 字节、TID=1。表 4-20 给出 DATA_LENGTH `[31:16]`、TID `[5:3]`、CMD_ATTR `[2:0]=0`。保留位在示例中取 0，命令字为：

`(8 × 2^16) + (1 × 2^3) = 0x00080008`

这是 **COMMAND_QUEUE_PORT 的写入内容**，不是地址。实际步骤为先确保足量 TX 数据已经可见，再提交匹配命令，随后等待 Controller Read。对普通路径，此顺序是减少读到达时资源竞态的工程安排；扩展 TX 路径在 pp.195、206、210 则有“先数据再命令”的明确建议。

<pre class="mermaid">
sequenceDiagram
    participant A as Target 应用
    participant T as Target IP
    participant C as Controller
    A-&gt;&gt;T: 准备 8 字节 TX 数据
    A-&gt;&gt;T: 提交长度 8、TID 1 的普通 TX Command
    C-&gt;&gt;T: Private Read 寻址
    T--&gt;&gt;C: 条件满足，ACK
    T--&gt;&gt;C: 发送已准备的数据
    T--&gt;&gt;A: 完成响应 / 本地事件
    A-&gt;&gt;T: 保存并解析响应，回收资源
</pre>

该图是 p.197 的软件与协议职责示意，省略总线位时序，不表示 TX Command 自己产生 START。

### 4.4 响应长度方向不同

普通表 4-21 的低 16 位，在 TID≠7 时：

- `RX_RSP=1`：Controller Write，即 Target 接收的长度。
- `RX_RSP=0`：Controller Read，即 Target 发送的**剩余长度**。

> “Data Length in bytes of Controller Write Transfer or the Remaining Length of Controller Read Transfer” — p.220，字段说明节选。

工程计算示例：若一笔无错误的普通 TX 已全部发送，剩余长度应为 0，而不是原始命令长度 8。成功普通响应可由 ERR_STATUS、RX_RSP、TID 和这个方向相关长度共同解释。TID=7 的 Vendor CCC 又复用部分位为 Defining Byte/8 位长度，不能统一掩出低 16 位就当字节数。〔表 4-21，pp.219–220〕

**发生错误后，不能保证该长度精确表示出错前实际传输的字节数。** 原书明确指出其不准确，并关联队列 flush 的恢复流程。故“发送长度减去错误响应剩余长度，继续发送尾部”的通用恢复算法没有本书依据。

> “does not accurately reflect the number of bytes sent or received before the occurrence of the error.” — p.220 Note。

**自检：** 同样读取到长度 0，成功 TX 与成功 RX 的含义相同吗？不同。必须先判事务方向、TID 和错误状态。

## 第 5 章：中断、FIFO 与 DMA 服务

### 5.1 中断有两层门控

`INTR_STATUS_EN` 决定对应事件是否产生状态；`INTR_SIGNAL_EN` 决定已有状态是否驱动中断输出。屏蔽 signal 层仍可保留轮询观察，但关闭 status 层就可能不再记录对应事件。〔§2.1.8.2 p.46，图 2-5 p.47〕

> “When any bit of INTR_STATUS_EN is set to 0, it disables the generation of interrupt in that specific INTR_STATUS register bit and hardware interrupt pin.” — p.46。

公共说明列出 `INTR_STATUS[4:0]`、`[20]` 自动清除，`[19:5]` 事件写 1 清除。但这不能替代所有 Target 扩展位的定义：扩展 TX 完成的 `EXT_CMD_SENT_STS` 在读取 `EXT_CMD_VLD_STAT` 后清除。驱动必须按实际字段分别处理，不能把整个状态值原样回写作为唯一 ISR。〔pp.46、207〕

> “INTR_STATUS.EXT_CMD_SENT_STS is cleared after a read from EXT_CMD_VLD_STAT register.” — p.207。

工程上的 ISR 顺序建议：先保存状态快照；优先服务会丢数据的 FIFO/响应；保存读清寄存器值；按事件类型更新软件状态；最后只对确认是 W1C 的已处理位执行清除。优先级和循环预算由应用决定，这不是手册规定的唯一顺序。

### 5.2 开始阈值与服务阈值

| 阈值 | 用途 | 软件误解的后果 |
|---|---|---|
| `RX_START_THLD` | 接收前是否有足够空闲位置 | 即使地址已 ACK，后续不消费数据仍会溢出 |
| `TX_START_THLD` | 是否已有足够发送数据接受读 | 达到开始门槛不等于后续整笔数据已备齐 |
| `RX_BUF_THLD` | 接收过程中触发软件/DMA 服务 | 过晚服务可能耗尽剩余空间 |
| TX 空闲阈值 | TX FIFO 空闲位置达到一定水平时补数 | 服务不及时会 underflow |

来源：pp.196–197；DMA 阈值与传输单位见 pp.36–38。实际编码不一定等于直接字节数；原书描述的单位包括 FIFO location，必须结合配置解码。

### 5.3 一个缓冲预算示例

p.43 用 SDR 12.5 MHz、每字节 8+1 个时钟举例，得到每字节 720 ns。以下仅按这个数据阶段模型推导，不计地址、起止、停顿及实现延迟。

假设应用收到服务条件时还有 **32 字节 RX 空间**，而 Controller 连续输入，则这部分空间约在：

`32 × 720 ns = 23.04 μs`

后耗尽。若应用最坏延迟超过此值，仅靠中断后再开始搬运无法保证不溢出。可以降低服务阈值、增加缓冲或采用 DMA，但方案须同时满足持续搬运吞吐量。不能把这段估算当作芯片给出的精确 IRQ 截止时间。

证据与推导边界：p.43 原文算式为 `“(8+1) * 80 ns = 720 ns.”`；32 字节是假设值，23.04 μs 是算术结果。适用场景为连续 SDR 数据阶段，非 HDR 或全事务吞吐量。

### 5.4 DMA 搬数据，应用仍管理事务

外部 SDMA 用于 TX/RX 数据，不负责取得 I3C 命令或把 I3C 响应写入内存。更关键的是，Target 模式下 TX 与 RX 的 flow controller 不同：TX 由外部 DW_ahb_dmac 控制块长度，RX 由 I3C IP 控制。不能复制 Controller 模式两个方向都由 DMA 控制的配置。〔§2.1.3.2 p.36〕

> “the external DW_ahb_dmac is the flow controller for the TX channel and DWC_mipi_i3c_v111_basic is the flow controller for the RX channel.” — p.36，Target 模式说明。

p.37 的 35 字节示例采用 32 位 APB 数据宽度，搬运 9 个 word 容器；最后一个容器只有 3 个有效字节。35 个线上字节与 36 字节容器空间不同。该例用于说明对齐，不能把 word 数直接当作总线 DATA_LENGTH。

> “DMA Single (Packed 3 bytes of data)” — 图 2-3，p.37；尾 DWORD 的 MSB 无效见 p.38。

扩展 TX 可通过 `DMA_CHANNEL_SEL` 映射通道。若 `IC_SLV_VEND_CCC_EN`、`IC_VIRT_TGT_EN` 或 `IC_SPEED_HDR_BT` 启用，p.38 对 DMA high-speed 与 Extended Command Structure 有额外限制。错误处理时，该页要求在 reset 数据 FIFO 前禁用 DMA high-speed 接口，在 resume 前重新启用。完整接口信号与寄存器设置需 User Guide/RM。

> “Disable the DMA high-speed interface before resetting the data FIFOs.” — p.38。

**自检：** DMA 已传完内存数据是否代表 I3C 已完成发送？不能据此判断。SDMA 与 I3C 命令响应职责不同，还要观察对应事务完成，依据 p.36 与 p.197。

## 第 6 章：Hot-Join、IBI 与读通知

### 6.1 Hot-Join 请求的是加入与分配地址

p.212 将首次 HJ 条件列为：配置支持 HJ、`HJ_EN=1`、动态地址无效、未被 DISEC 禁止、满足所编程的 bus-idle 条件。第一次失败或仲裁失败后，在后续 START 或 bus-idle 机会重试，仍需满足其列出的地址与事件条件。

特别注意：启用 HJ 时，本 Target **在 HJ 请求上总线之前不参与 ENTDAA**。所以发现不了设备，也可能是 HJ 前置条件未满足。〔§4.2.8 p.212〕

> “When Hot Join is enabled (SLV_EVENT_STATUS[HJ_EN] = 1), Target does not participate in ENTDAA until HJ request is sent on the Bus.” — p.212。

同页关于 `ADAPTIVE_I2C_I3C` 存在容易混淆的表述：主体段落说置 1 后等待识别 `0x7E` 再发送 HJ，末段又说要启用 HJ 则置 0。可确认的是它影响总线识别/发送条件；**不能简单概括为一个独立的 HJ 开关**。实际 HJ 策略须结合 p.178、p.212 及 User Guide 确认。本文保留该疑点，不给无条件设置值。

### 6.2 SIR：先准备通知内容，再提交请求

对于 p.213 的 APB Target 配置，IBI payload 支持需要 `IC_SLV_IBI` 与 `IC_SLV_IBI_DATA`。MDB 由 `SLV_INTR_REQ.MDB` 提供，额外 payload 至多 4 字节，来源为 `SLV_SIR_DATA`，`SIR_CTRL=00b` 选择该来源。此 4 字节限制不能推广到 Autonomous Target 的 AHB 供数路径。

> “can send up to four bytes of payload data along with the mandatory byte (MDB)” — p.213，节选。

机制级流程：

1. 确认动态地址有效、事件允许，且没有尚未完成的 SIR 请求。
2. 准备 MDB 和本次 payload；若启用时间戳，还需相应外部计数器配合。
3. 按该配置设置 `SIR_CTRL`，提交 `SLV_INTR_REQ.SIR`。
4. 等待 `INTR_STATUS.IBI_UPDATED_STS`，保存 `IBI_STS` 等结果。
5. 按成功、未尝试、提前终止分别处理，再允许下一次请求。

依据：pp.213–215、217。该顺序不包含待 RM 确认的位号和完整读清/W1C 操作。

> “the application should wait until IBI status is updated ... before asserting SIR/MR again.” — p.217，省略中间括注及寄存器重复说明。

当 Controller 提前结束 SIR payload，`SLV_IBI_RESP.SIR_RESP_DATA_LENGTH` 记录未发送字节；Target 进入 halt，原书要求清 TX FIFO 后 resume，并明确不支持剩余 payload 重传或之后让 Controller 再读残余数据。不要把同一 SIR payload 当作可自动续传的消息队列。〔p.215〕

### 6.3 IBI 和本地 IRQ 是不同事件

IBI 在 SDA/SCL 上通知 Controller；`IBI_UPDATED_STS` 则通知本地 Target 应用“这次 IBI 请求已有结果”。因而本地 ISR 触发不意味着对端已经接受全部 payload，应解析 `IBI_STS` 和相关结果。原文 p.214 分别说明 IBI generation status、IBI_UPDATED_STS 以及 SIR 位自动清除。

> “The status of the IBI generation is updated in the IBI_STS field” — p.214，节选。

### 6.4 Pending Read Notification

配置 `IC_TGT_PENDING_READ_NOTIFY=1` 后，手册要求应用将 MDB `[7:5]` 设置为 `101b`，并提供相应 payload。Controller 收到通知后**可能** ACK 并发起 Private Read；它不是 Target 可强制执行的下一条命令。通知前准备好读数据/命令，是避免对端立即读取却 NACK 的工程安排。〔§4.2.18 p.231；读 ACK 条件 p.197〕

> “the Controller might ACK the IBI and perform a Private Read request to the Target” — p.231，节选。

**自检：** 为什么应用应先备好数据，再发读通知？因为对端可能紧接着读，而普通 Target 读 ACK 取决于命令与数据是否已经就绪。

## 第 7 章：NACK、错误、禁用与恢复

### 7.1 先区分“尚未接受”和“接受后出错”

| 场景 | Target 行为/观察点 | 恢复方向 | 来源 |
|---|---|---|---|
| RX 开始空间不足 | NACK；`BUFFER_NOT_AVAIL` | 释放 RX 空间；不能直接归类为已发生 overflow | p.196 |
| 普通读没有 TX Command | NACK；`READ_REQ_RECV_STS` | 准备匹配命令和数据，配合 Controller 重试 | p.197 |
| TX 数据不足或 Response Queue 满 | NACK；`DATA_NOT_READY`，可能延迟几个 SCL 后可见 | 补数/回收响应，重新核对就绪状态 | pp.197–198 |
| SDR 接收中 overflow 或 parity/CRC 错误 | 设置错误，丢后续数据至终止，拒绝后续 Private | Controller GETSTATUS 配合，Target 应用 RESUME；失效队列处理见对应恢复资料 | p.196；p.220 Note |
| SDR 发送中 underflow | 终止传输，设置 `UNDERFLOW_ERR`，拒绝后续 Private | Controller GETSTATUS 配合，Target 应用 RESUME | p.197 |
| I2C 接收 overflow | NACK 终止当前传输 | Target 应用 RESUME | p.197 Note |
| I2C 发送 underflow | Target 不能终止当前传输；拒绝后续 Private | Target 应用 RESUME，不能套用 SDR 立即终止行为 | p.198 Note |
| 扩展读提前终止 | 记录错误/完成，可能留有扩展 TX 数据 | 清对应 EXT TX FIFO，再准备新命令 | pp.207、210 |

> “In I2C mode of operation, if an underflow error is encountered in a read transfer, Target cannot terminate the ongoing transfer.” — p.198。

工程诊断顺序应先观察事务是否已被地址 ACK，再判断是资源门槛未满足，还是中途损坏。将所有 NACK 都当成“地址错误”会忽略 FIFO、响应队列和历史错误状态；将所有 NACK 都用 reset 处理也会掩盖真实原因。

### 7.2 错误恢复不是一个通用 RESUME

在 pp.196–197 所述 overflow/underflow 路径中，读取 `CCC_DEVICE_STATUS` 的本地 APB 操作不能据此替代 Controller 在总线上发送 GETSTATUS：原书明确指定后者，并同时要求 Target 应用恢复。协作实现时应约定对端何时查询、应用何时准备好资源，以及失败事务是否由上层重试。

> “until the Controller reads the device status through a GETSTATUS CCC and the target application resumes the target operation” — p.196，节选。

HDR-BT 某些 header 错误在 GETSTATUS 后不要求 RESUME，data block 的恢复又有长度条件。PEC 的完整错误恢复另指向 User Guide。故本章矩阵不将未明确的分支扩展成统一代码；特殊模式见第 8 章及文末疑点表。〔pp.211–212、229〕

### 7.3 TE0–TE5 与应用观察

p.229 总述说 TE0–TE5 由 Target 状态机内部处理，随后列出不同动作：TE0/TE1 等待 HDR exit；TE2 的 CCC 更新无效而保留旧值，Private Transfer 又涉及响应错误；TE3/TE4 暂停 CCC 更新直至 START/STOP；TE5 不更新非法 CCC。

> “Handling of TE0-TE5 is internal to the Target and transparent to the application” — p.229，节选。

同页“not reported in the Response”总述与 TE2 的分项文字存在表述张力。因此可确认内部有协议恢复机制，但不能断言所有相关故障都没有应用可观察状态。TE6 在本 Target 中明确不支持。〔§4.2.16.1 p.229〕

### 7.4 SDA Read Abort

§4.2.16.2 p.229 描述读期间 SCL 不变化达到编程条件后，Target 将 SDA 释放为 High-Z，产生 `SDA_RELEASED_STS` 并进入 halt，要求应用处理 FIFO 并设置 RESUME。这解决的是读期间 SDA 被持续占用的特定场景，不是通用 bus-clear 算法。

> “releases the SDA to High-Z” — p.229。

该页的 `RELEASE_SDA_TIMING` 时基写法出现 `core_clk`，而 p.39 说 APB Target 无 core clock。当前不能据此可靠推导定时计数；应取得实际配置的 RM/UG 后落实时间单位。

### 7.5 禁用、主动 NACK 和复位分开处理

**正常禁用：** 清 `DEVICE_CTRL.ENABLE` 后轮询，直到读到 0。若正在收发/IBI，硬件等待当前事务完成再禁用；禁用后对地址匹配包括 `0x7E` NACK。待 SIR/MR 状态未回报时，不应禁用。再次使能可能等待 SCL。〔p.217〕

> “The Target IP should not be Disabled when Status for SIR/MR request is pending.” — p.217。

**主动 NACK：** 需要 `IC_SLV_NACK_EN`。应用在 bus idle 请求，观察 `SLV_NACK_CONF.NACK_CONF`：`01b` 表示接受，`11b` 表示仍忙，`00b` 表示尚未进入/请求未获接受。请求不被接受时应用仍应准备接收事务。支持范围包括 SDR、启用 flow control 的 DDR、HDR-BT；具体值和位号以 RM 为准。〔pp.217–218〕

**Target Reset：** RSTACT 描述复位动作，reset pattern 检测通知应用。常规 Target 提供 `TGT_RST_PATTERN_DET_STS`；禁用时可由 `debug_port[55]` 指示。真正执行外设级或整设备级复位仍需应用/系统配合，不能把 pattern detected 等同于整个芯片自动复位。Virtual Target 的特定 RSTACT 定义字节可被内部处理，需结合 pp.181、193 的例外。〔p.225〕

> “Target mode supports 'INTR_STATUS.TGT_RST_PATTERN_DET_STS' interrupt to notify the application” — p.225，节选。

**自检：** 清 ENABLE、强制 NACK、RESUME、Target Reset 是否可以互相替代？不能。分别对应停止参与、拒绝新请求、退出特定错误状态和执行复位动作。

## 第 8 章：按配置选用的 Target 扩展能力

### 8.1 HDR-DDR Flow Control 需要双方协商

配置 `IC_I3C_HDR_DDR_FLOW_CTRL=1` 使 Target 支持相应能力，但总线上还要通过 GETCAPS 发现、ENDXFER 协商。p.198 要求先发送 Defining Byte `0xF7` 的 ENDXFER 及附加数据，再以 `0xAA` 确认。只改本地某个字段而不完成协议协商，不能据此认为双方已经采用新行为。

> “needs to be followed with another broadcast/directed ENDXFER CCC with the defining byte 0xAA.” — p.198，节选。

| ENDXFER `0xF7` 附加数据位 | 含义 | 原文编码 |
|---|---|---|
| `[7:6]` | 提前终止后的 CRC word 指示 | `11b` 无 CRC word；`01b` 有 CRC word；其余保留 |
| `[5]` | Write Early Termination | 0 启用；1 禁用 |
| `[4]` | Write Command ACK/NACK | 0 启用；1 禁用 |
| `[3:0]` | 保留 | 不赋予自定义功能 |

来源：表 4-14，pp.198–199。特别注意 bit 5/4 是 **0 表示启用**，不能凭名称套用常见的“写 1 enable”。

启用相关能力后，Target 可根据地址、队列资源及命令 parity 决定命令 ACK/NACK；接收 overflow 或数据 parity 错误时可请求提前结束。p.196 的基础 HDR 描述说写数据无法 NACK，不能用它否定这里明确增加的 flow-control 能力。〔p.199〕

### 8.2 Format 2 改变命令与完成路径

Virtual Target 或 HDR-BT 使用 Extended TX Format 2。三个 word 分别对应 `EXT_CMD_REG_x`、`EXT_CMD_REG_x_2`、`EXT_CMD_REG_x_3`，这表示功能对应，不代表字节地址一定连续。〔§4.2.13.4，p.221〕

| Word | 关键字段 | 含义 |
|---|---|---|
| 1 | `ADDR_OFFSET`、`CCC`、`CMD_VLD`、`ERR_STATUS`、`FINITE_DL`、`EXT_DL_PRESENT` | 选择虚拟目标/读类型，发布命令，记录错误，选择有限长度 |
| 2 | 非 BT 的 `DATA_LENGTH`、Defining Byte、CCC/HDR header；BT 时复用为 4 个 command bytes | 根据传输模式解释，不能同时视为长度和 BT 命令 |
| 3 | `EXT_DATA_LENGTH[22:0]` | HDR-BT 有限读长度 |

来源：表 4-23，pp.222–223。本文未给出整个 Format 2 的可直接执行 MMIO 初始化，位号和访问副作用应以原表及实际 RM 落实。

扩展发送时，应先装填相应 TX FIFO，并等待缓冲状态反映数据，再发布 valid。命令完成后，硬件清命令有效位、设置对应完成状态；应用读取 `EXT_CMD_VLD_STAT` 保存完成集合，再从对应命令寄存器读取错误。**不能一律等待普通 Response Queue 中的 TX response。**〔pp.205–207、210〕

> “The error status for the finished command is read from EXT_CMD_REG_x.ERR_STATUS.” — p.207。

Format 2 接收响应可以分块，应用要处理 `FIRST/LAST`、`TARGET_ID`、`CMD_SIZE`。`CMD_SIZE` 指示 RX FIFO 首 word 是否含编码后的 CCC/HDR 命令，不能一律把首 word 当 payload。它是元数据；是否存在、有效字节数和本块数据长度应分别解析。〔pp.204–205；表 4-24 pp.224–225〕

> “No Command Word. First word from Rx FIFO is the first Data word of the transfer.” — p.204，仅针对 CMD_SIZE=0。

### 8.3 HDR-BT：Delay Byte 与有限长度

p.208 以 `IC_SPEED_HDR_BT=1` 为能力条件；pp.209、211 明确这里支持 Private HDR-BT，排除相应的 HDR-BT Write/Read CCC。不能因为扩展描述符里有 CCC 字段，就推断所有 BT CCC 都可用。

> “does not support HDR-BT read CCC transfers.” — p.211，节选。

有限长度读可在 Controller 允许 Delay 机制时插入 Delay Byte；p.211 指出控制条件是 HDR-BT header Transition bit `[2]=0`，且仅针对有限长度命令。Delay 计数到期后会以已取得的数据发送最后一块。因此 Delay 机制不是无限等待应用的保证。〔§4.2.7.2.1，p.211〕

> “supports Data Block Delay byte insertion for Finite Data length transfer commands only.” — p.211，节选。

BT 错误应按块类型区分：header 的特定错误经 GETSTATUS 后无需 RESUME；data block 的相关错误在已传输数据超过 32 字节时要求 RESUME；CRC block 错误一般说明要求 RESUME，而下一页对 CRC Verify Byte 特定 frame 错误又写无需 RESUME。不能将这些分支压成一个恢复动作，实际编程需核对配套 Error Recovery Flow。〔pp.211–212〕

### 8.4 PEC：按事务类型计算，避免重复附加

APB Target 的 PEC 用于 SDR CCC、Private 和 IBI；原书给出 CRC-8 多项式 `x^8 + x^2 + x + 1`，不适用于 HDR。应用收到相应 DEVCTRL 后设置 `DEVICE_CTRL.TARGET_PEC_EN`；若启用 `IC_TGT_XFER_PEC`，还要在使能前设置 `TGT_CAP_CTRL` 的分类控制。〔pp.225、227〕

| 事务部分 | 本书说明的 PEC 覆盖范围 | PEC 生成方 |
|---|---|---|
| Broadcast CCC | CCC code＋可选数据，不含 `0x7E/W` | Controller |
| Directed CCC 命令段 | CCC code | Controller |
| Directed CCC write 数据段 | Target address＋payload | Controller |
| Directed CCC read 数据段 | Target address＋payload | Target |
| Private Write | Target address＋payload | Controller |
| Private Read / IBI payload | Target address＋发送数据 | Target |

来源：§4.2.15.1–§4.2.15.5，pp.227–228。这里按原书所示格式整理；存在 Defining Byte 或额外协议扩展时需进一步核对对应帧格式，不能从简表外推所有组合。

> “The PEC is calculated only on the CCC code and optional data (and not for 7E/W).” — p.227。
>
> “the APB Target automatically appends the PEC byte at the end of the private READ transfer” — p.228，节选。

因此在硬件自动附加 PEC 的路径，应用数据准备不要再无条件追加同一个 PEC；收发双方必须事先约定长度，TX 命令长度表示约定的数据阶段，硬件在其后进入 PEC 阶段。发生提前结束或长度不一致时，应按该模式错误处理。〔p.227〕

### 8.5 虚拟 Target 与组地址

Virtual Target 在一组 SDA/SCL 上呈现多个逻辑 Target，各有身份/地址，但共享外设逻辑与部分资源。启用后普通命令结构不再适用，应用用 `TARGET_ID`、扩展命令目标选择等关联实际逻辑目标。虚拟实例不是自动得到完全独立的一套 FIFO。〔pp.199–207、218〕

> “behaves as though it has multiple I3C Targets.” — p.199，节选。

Group Address 让多个 Target 同时匹配组写；本 IP **对 Group Address 的读事务 NACK**。容量由 `IC_GRP_ADDR_CNT` 以及虚拟 Target 的 `IC_VIRT_TGT_GRP_ADDR_CNT` 限制；SETGRPA 超容量会 NACK，RSTGRPA/RSTDAA 可清组地址。〔pp.230–231〕

> “The DWC_mipi_i3c_v111_basic Target NACKs the Read transfer issued for a Group Address.” — p.230。

工程上组写的应用分发要按响应里的目标集合处理，而不是把一次收包必定绑定到一个目标。第一个响应的命令元数据、FIRST/LAST 及本块长度同样需要解析，依据 pp.204、224–225。

### 8.6 ASYNC Mode 0 的外部责任

可选时间戳功能不仅是一个本地 TS 位。APB Target 通过外部 17 位计数器取得 T_C1/T_C2 时刻的数据；应用在事件时启动计数，在 `async_ts_t_c1_sync_p` 后重新计数，并饱和到 `17'h1_0000`。计数溢出有专门的发送格式，不能将饱和值继续当作正常时间戳。〔pp.213–214〕

> “the application starts incrementing the 17-bit external counter ... and saturates at 17'h1_0000.” — p.214，省略计数器名称与起始值的节选；完整起始值为 0。

实际计数时钟、同步关系和时间戳精度需按配置核实，尤其注意第 1 章提到的 core_clk 命名疑点。

**自检：** 为什么不能仅凭功能清单就打开所有扩展？因为描述符格式、数据源、完成路径和协议协商均可能随配置变化。原书 p.22 明确提醒功能组合受配置限制。

## 第 9 章：Autonomous Target 的独立开发路径

### 9.1 自主的是数据路径，不是所有集成工作

Autonomous Target 用 Target-Lite 协议引擎、事务层和 AHB Manager 将 I3C 访问转换为应用侧寄存器/存储器访问。应用仍要提供帧格式、必要地址/长度、CCC channel 响应和可选事件信息。〔图 2-6 p.48；§4.3 p.232；pp.248–259〕

<pre class="mermaid">
flowchart LR
    C[Controller] &lt;--&gt;|I3C| P[Target-Lite 协议引擎]
    P &lt;--&gt; T[事务层 / 帧解释]
    T &lt;--&gt; F[TX / RX 缓冲]
    F &lt;--&gt; H[AHB Manager]
    H &lt;--&gt; M[应用寄存器或存储器]
    U[用户接口 / 配置 / CCC channel] &lt;--&gt; T
</pre>

这是机制示意图，不是可直接实例化的总线连接图。数据来源从 APB 软件搬运变为 AHB Manager 后，APB Target 的普通 TX Command 初始化不能直接照搬。

> “without software assistance” — §4.3 p.232，描述通过 AHB 接口集成寄存器/存储器的数据交换；不是对所有配置工作的豁免。

### 9.2 配置寄存器：先分清访问协议

I2C 模式下，p.248 用 static address #0/#1 区分数据路径和内部控制路径。这是两个静态地址槽位的编号，不能直接理解为总线地址数值 `0x00` 和 `0x01`。单寄存器访问及 little-endian 的限制在 pp.248–249 给出。

I3C 模式下，Controller 使用与 `reg_prgmg_dccc` 或 `reg_prgmg_bccc` 端口值匹配的 Vendor CCC，Defining Byte 选择配置项，4 个数据字节提供写入内容。具体映射已列在寄存器表（`i3c-target-registers.md`）第 5 节。〔pp.249–250〕

> “The defining byte functions as an address indicator” — p.249，节选。

这解释的是协议索引，不给出应用 CPU 的 APB 字节偏移。若先前有人提供 `TRANSFER_FORMAT_SEL=0x00`，必须进一步确认他说的是 Defining Byte 还是 APB offset。

### 9.3 帧格式决定地址和长度从哪里来

| 格式 | I3C 帧携带的信息 | 外部用户接口的角色 |
|---|---|---|
| Address | 应用侧访问地址 | 读路径补充所需数据长度等信息 |
| Address＋Data Length | 地址及数据长度 | 按帧配置直接启动相应 AHB 访问 |
| Command | 应用定义的 command bytes | 将命令翻译为 AHB 地址、长度和地址访问方式 |
| Default Pointer | 以数据/预先准备的访问上下文为主 | 提供地址、长度或触发；具体读触发条件须按信号说明核对 |

来源：§4.3.7.2，pp.252–254。p.252、p.255 的“三种”总述实际随后列到四类，本文按分项名称列出，不依赖数量措辞。

### 9.4 一次 Address＋Data Length 读的过程

教学场景：应用侧有一块可读数据，Controller 通过约定帧提供起始地址 A 和长度 N，且 N>0、资源和握手已就绪。A/N 为符号，不指定未经确认的线端字节序或字段宽度。

1. Controller 发起写阶段，发送符合 `TRANSFER_FORMAT_SEL` 的地址和长度字段。
2. 检测到 Repeated START 后，Target 将该访问解释为读路径。
3. AHB Manager 使用地址/长度取得应用数据，填充发送缓冲。
4. Target 在满足其接收读请求条件后向 Controller 返回数据。
5. 任何应用侧延迟都要计入缓冲预算；自主搬运不表示不会 underflow/overflow。

依据：pp.253–254，Address＋Data Length 读的 AHB 触发；p.252 的缓冲与错误处理。

> “In the Address and Data length format, the target initiates the AHB Read transfers on detection of Repeated START flag.” — p.254。

对 Address-only 或 Command 格式，Target 还需通过 `usr_if_rd_req` 等用户接口索取长度/地址，不能省略握手。p.253 的 `TX_FIFO_EMPTY! =0` 文字本身有歧义，本文不据此写布尔表达式或断言端口极性，实际 ACK 条件需核对信号定义。

### 9.5 接收、CCC、IBI 与复位

**接收：** AHB Manager 在收到达到 RX 阈值的数据后发起访问；若最终数据少于阈值，线上完成后也会启动搬运，因此短包不能只按“等阈值”建模。出现 AHB subordinate error 或协议错误会影响 Target 状态；p.252 的恢复流程要求 Controller GETSTATUS，没有照搬 APB Target 应用写 RESUME 的步骤。〔p.252〕

> “if the received Data is less than threshold amount of Data and transfer got completed on the line, then AHB initiates the transfer” — p.252，节选。

**Vendor CCC：** 对不内部消费的 CCC，应用侧 channel 有责任识别是否支持。p.248 说明 Directed Write 可通过 deassert `ccc_wr_data_rdy` NACK，Directed Read 可通过 deassert `ccc_rd_data_vld` NACK；不能将“不认识”默认为硬件一定拒绝。信号必须满足 §4.3.5 的时序，本文不只凭名称给出时钟周期实现。

> “The CCCs consumed internally are not passed through this channel.” — p.248。

**IBI：** 在 Controller ACK 后，Autonomous Target 可通过用户接口取得地址/长度，发起 AHB read 并发送取得的数据。因此不是 APB Target 的 `SLV_SIR_DATA` 四字节模型。p.258 将类型选择分配给 `sir_req_type`，p.259 却将一个二位编码写给 `sir_req_now`；这个端口名/编码冲突必须先解决，再实现带数据 IBI。〔pp.258–259〕

**使能：** `slv_lite_enable` 禁用后拒绝传输但保留内部寄存器，重新启用不因此必然要求再次地址分配；原文限定在 STOP 与 START 之间或 IDLE 改动，不能在传输中间切换。〔p.260〕

> “Enable or disable the Target-lite only between a STOP and a START OR in IDLE period and not in middle of the transfers.” — p.260。

**复位：** 通过 vendor CCC channel 解读 RSTACT，结合 `target_rst_pat_ongoing`、`tar_rst_pat_detected_t`、`start_det_t` 采取相应系统动作，不能把某个检测脉冲当作硬件已自动完成全部 reset。〔pp.260、265〕

### 9.6 PEC、Prefix Data 与资源限制

Autonomous Target 的 PEC 使用外部 `slv_pec_enable` 控制，应用应与 Controller 启用状态及事务边界一致。它不是 APB Target 写 `DEVICE_CTRL.TARGET_PEC_EN` 的同一接口。〔p.261〕

Prefix Data 可用于 Private Read 或 IBI，最大可配置字节数由 `IC_PREFIX_DATA_BYTE_CNT` 给出，取值为 1、2、4、8；每笔内容/数量通过 `prefix_rd_data`、`prefix_rd_data_cnt` 提供。IBI 中 prefix 在 MDB 和可选时间戳之后；启用 PEC 时 prefix 也参与相应计算。〔p.265〕

> “the prefix data bytes are appended after the MDB and the timestamp bytes” — p.265，节选，时间戳以功能启用为前提。

应用必须核对 p.27 的自主模式限制：最低 hclk 为 80 MHz；数据长度不能为 0；active transfer 期间不推荐 APB 写配置；固定地址模式低两位处理及 underflow 下未对齐起址可能造成数据丢失。AHB RETRY/SPLIT 的支持描述在 p.27 与 p.51 冲突，不能在未核实前依赖该能力。

> “The Autonomous Target supports a minimum hclk frequency of 80 MHz” — p.27。
>
> “The Autonomous Target does not support data length programmed to 0.” — p.27。

**自检：** 为什么“自主 Target”仍需要用户接口？因为某些帧只传命令或地址，实际 AHB 地址/长度及数据来源仍由应用提供。

## 附录 A：本书限制与待核实清单

| 项目 | 已知证据 | 当前处理 |
|---|---|---|
| APB 地址与完整位属性 | p.8 将 Register Descriptions 移至 RM | 地址缺项明确标记，详见寄存器表；无猜测 MMIO |
| 时钟和复位集成 | p.29 指向 User Guide；p.39 说 APB Target 无 core clock，pp.213、229 又引用该名 | 不推算 Target 时钟相关寄存器计数 |
| HJ adaptive 控制 | p.212 主体与末段用词不一致，p.178 又区分模式/滤波 | 分别观察 bus detection、DA、HJ 条件，不给无条件开关值 |
| TE0–TE5 可观察性 | p.229 总述与 TE2 分项不完全一致 | 保留内部处理与响应错误两类证据 |
| BT 错误恢复 | pp.211–212 的块级和 CRC Verify 特例不同，另有 PARITY_ERR/FRAME_ERR 混写 | 不生成统一 RESUME 算法；按 Error Recovery Flow 核实 |
| Autonomous IBI 编码 | p.258 类型在 sir_req_type；p.259 在 sir_req_now 写二位值 | 机制可解释，端口编码实施待 RM |
| Autonomous timestamp 请求 | p.259 同时出现 sir_ts_req 与 slv_ts_req | 不宣称是两个可互换端口 |
| Autonomous AHB RETRY/SPLIT | p.27 不支持，p.51 又写支持 | 不作为已确认能力 |
| Autonomous ACK 条件 | p.253 的 TX_FIFO_EMPTY 判断文本歧义 | 不复制其布尔表达式作为代码 |
| PEC 详细恢复 | §4.2.15.6 p.229 指向 User Guide | 不杜撰完整清队列/重启顺序 |
| 普通 Target 数据端口及队列 reset | 当前 Target 章节缺完整名称、地址和位属性 | 以功能名教学，待生成映射/RM 落实 |
| APB Target sub-address decoding | §1.5 p.27 将其列为限制 | 不假定 IP 自动实现应用寄存器地址协议；见附录 B |

明确排除：HDR-TSP/TSL、普通 Target 配置的 Automotive Safety。依据都是 §1.4 p.26，正文保留这些边界，不为其编写实现流程。

> “HDR-TSP/TSL feature is not supported. Any reference to HDR-TSP/TSL should not be considered as supported.” — p.26。
>
> “Automotive safety functionality is not supported in Target configuration.” — p.26。

面积/功耗若用于资源比较，应只比较目标配置及相同测量条件；附录 A 基于 7 nm 综合库，不代表当前 SoC 测量值。〔p.308〕

## 附录 B：把教程用于实际开发

### B.1 应用寄存器协议不是自动获得的功能

如果希望 Controller “先写寄存器地址，再读寄存器值”，应区分：IP 自身 APB CSR、应用定义的 I3C sub-address、Autonomous Target 解码后的 AHB 地址。这是三个不同地址空间。

p.27 将 APB Target 的 `“Sub-address/Register-address decoding”` 列为限制；pp.196–197 说明普通 Target 提供的是 RX 数据和预备 TX 能力。因此，普通 APB Target 的应用要自行实现相应协议解析/响应准备，或采用与 Controller 约定的提前准备/重试流程；不能默认硬件收到 sub-address 就自动读取某个 APB 寄存器。该结论是结合上述证据的工程推导。

尤其是在 Controller Write 紧接 Repeated START Read 的场景，应用可能来不及在两阶段之间解析地址并提交 TX Command。是否能满足要求取决于具体时序、预备数据策略和硬件配置；不能套用 I2C clock stretching 解决，因为本 Target 不支持该能力。〔pp.178、197〕

### B.2 建议的开发记录

建立一张工程表记录：实际角色/生成参数、寄存器图版本、SoC 基地址、时钟与 reset 接口、FIFO 容量、描述符类型、预期帧长度、IRQ/DMA 模式、支持 CCC、IBI 策略及错误恢复来源。每项绑定配置文件或配套手册位置，而不是只记录默认值。这是便于实现审查的工程方法。

### B.3 综合练习与预期观察〔尚未在硬件执行〕

| 练习 | 预期观察与依据 |
|---|---|
| 静态身份 Target 取得 DA | DEVICE_ADDR 动态状态更新；分配后不继续参与普通 DAA，pp.177–179 |
| Controller 写入一笔短 SDR 数据 | 成功 RX response 与有效数据关联，pp.196、219–220 |
| TX 数据已填但故意不提交普通 TX Command | 读 NACK，可观察 READ_REQ_RECV_STS，p.197 |
| 预备 8 字节和 TID=1 后读取 | 命令字 0x00080008；成功发送的响应长度按剩余量解释，pp.219–220 |
| 不回收响应而持续事务 | 资源满后可能 NACK；不能只监控数据 FIFO，pp.196–197 |
| 发送 SIR 后等待结果 | IBI_UPDATED_STS 后解析 IBI_STS，再发下一次，pp.214–217 |
| 注入一个可控制的 SDR 接收 overflow | 观察错误和后续拒绝，按 GETSTATUS＋Target 恢复流程处理，p.196 |
| Vendor Directed Read 连续两次 | 每次要重新准备扩展命令/数据，p.195 |
| 扩展读正常完成 | 保存 EXT_CMD_VLD_STAT，再读对应 ERR_STATUS；注意读清事件，p.207 |
| 自主模式短写不足 RX 阈值 | 线上完成后仍应触发尾部 AHB 搬运，p.252 |

以上是从手册推导的验证方案，不是已执行测试或硅上结果。故障注入应在可恢复的开发环境中按项目正常调试流程实施。

## 附录 C：章节阅读索引

| 本文章节 | 原书优先位置 |
|---|---|
| 1 架构 | §§2.1–2.2 pp.32–53；§4.1 p.176；§4.2.1 p.178 |
| 2 身份与上电 | §4.2 pp.177–179；§4.2.4.3/.5 pp.187–192；§4.2.11 p.217 |
| 3 CCC | §4.2.4 pp.179–195 |
| 4 收发 | §4.2.5 pp.195–199；§4.2.13 pp.218–225 |
| 5 IRQ/DMA | §§2.1.3/.7/.8 pp.36–47；pp.196–197；扩展完成 p.207 |
| 6 主动事件 | §§4.2.8–4.2.9 pp.212–215；p.217；§4.2.18 p.231 |
| 7 恢复 | pp.196–198；§§4.2.11–4.2.16 pp.217–230，排除 Secondary Controller 专属流程 |
| 8 扩展 | pp.198–211、213–214、218–228、230–231 |
| 9 自主 Target | §2.2 pp.48–53；§4.3 pp.232–265 |

本版已经展开九章教学内容；未解决的地址、时钟、位属性和文档冲突均保留在证据缺口中。下一次补充应以匹配配置的 Reference Manual/User Guide 为依据，逐项补齐，不通过猜测消除空白。


## 同专题阅读

- [MIPI I3C Basic v1.1.1 组内分享](/2026/11/25/i3c-basic-spec/)
- [两根线，一笔事务：I3C 控制器学习手记](/2026/11/25/i3c-controller-guide/)
- [Linux I3C 驱动视角学习笔记](/2026/11/25/i3c-linux-driver/)
