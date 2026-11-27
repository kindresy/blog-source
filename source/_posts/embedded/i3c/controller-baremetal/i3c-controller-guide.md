---
title: 两根线，一笔事务：I3C 控制器学习手记
date: 2026-11-25
permalink: 2026/11/25/i3c-controller-guide/
categories: ["嵌入式开发","I3C","控制器与裸机开发"]
tags: ["i3c","controller","rtl","baremetal","testcase","tutorial"]
toc: true
max_depth: 3
mermaid: true
description: 从测试用例和一笔收发事务出发，理解 I3C Target 的队列、寄存器、完成条件和恢复流程。
---

> 从 29 个 testcase 出发，读懂协议、当前 RTL 和裸机驱动之间的关系。
>
> 面向已有驱动开发基础的读者。依据截至 **2026-09-12** 的工作区资料。

<!-- more -->

学一个控制器，最有用的问题是：**一笔数据怎样进入，又凭什么算完成？**

I3C 的难处常在两个地方：同一个词，在总线和寄存器里意思不同；同一个寄存器，读一次与读两次结果不同。把身份、数据、完成和恢复分清，代码就有了方向。

本文先从测试反推知识，再沿一笔事务走过硬件。正文适合顺读；逐项用例、源码索引与证据清单放在核对附录（`i3c-controller-learning-evidence.md`）。

**阅读约定：** [M] 是 MIPI I3C Basic 1.1.1（`mipi_I3C-Basic_specification_v1-1-1.pdf`），[D] 是 DWC databook（`DWC_mipi_i3c_v111_basic_databook.pdf`），[R] 是当前 RTL 配置及分析回传记录（`i3c_rtl_configuration.md`）。M 使用印刷页码，PDF 阅读器页序需加 18；D 的两种页码一致。[R] 属于用户提供的配置与外部 RTL 分析回传，**本次未直接核对 RTL 源码，也未上板验证**。本文将“源码现状”“回传流程”“学习推导”分开写。

## 1. 从 testcase 出发：我们究竟要证明什么

原始依据是 `i3c_testcase.xlsx` 的 `sheet1`，第 2～30 行，共 29 项。以下编号省略前缀 `Current Proj-D0-I3C-`。

### 1.1 把 29 项收成七个问题

| 问题 | 对应用例 | 要证明的事 | 因此必须学的知识 |
|---|---|---|---|
| 软件碰到的是正确硬件吗？ | 001、002 | 寄存器属性正确；共享 IO 识别、归属和恢复正确 | APB、MMIO、复位、PAD 复用、读写副作用 |
| Host 知道我是谁吗？ | 003、004、005、014 | 初始状态、动态枚举、旧地址隔离、兼容边界 | PID、BCR、DCR、SA/DA、ENTDAA、地址代际 |
| 一笔数据能完整走完吗？ | 006～012 | IRQ、短包、连续包、未就绪、PIO/DMA 两方向正确 | 第九位、字节序、FIFO、Response、扩展命令、完成条件 |
| 总线管理和电气行为正确吗？ | 013、015 | CCC 互操作；OD/PP 阶段与驱动归属正确 | 广播/定向 CCC、START/Repeated START、仲裁、ACK |
| Target 能主动通知 Host 吗？ | 016～019 | IBI 内容/边界；Hot-Join 后完整业务恢复 | MDB、IBI、Hot-Join、事件使能、重新枚举 |
| 出错之后还能继续吗？ | 020～023 | 提前终止、压力、线上复位后，下一笔业务正确 | HALT、错误归因、队列清理、RESUME、GETSTATUS |
| 一次成功能延续多久？ | 024～029 | 长稳、并发、反复加入、变频、复位循环、带宽 | 序号/CRC、服务预算、时钟域、资源所有权、性能口径 |

最后一列就是学习范围。协议百科可以很大，当前需要先掌握的是这些测试背后的因果。

### 1.2 为什么短包偏偏选 1、3、4、5 字节

我们的 APB 数据端口一次访问 32 位，也就是 4 字节。

- **1 字节**：只用最低 byte lane，检验最小长度。
- **3 字节**：最后一个 word 有填充，检验有效字节边界。
- **4 字节**：恰好一个完整 word。
- **5 字节**：跨两个 word，第二个 word 只有一个有效字节。

用非对称图样 `11 22 33 44 55`，才能看出颠倒、重复、漏字节。全零图样很安静，也很容易藏错。

**byte lane** 指一个 32 位 word 内的四个字节位置。**payload** 指业务有效数据，不含为了凑齐 APB word 而填入的无效字节。[R §13.4、§14.4]

### 1.3 为什么“第二笔正确”出现得这么多

020 故意让 Host 少读；021 制造真实服务压力；019、023 检查重新加入或复位后的业务。它们共同追问：旧数据、旧响应、旧完成事件，是否混进了新事务？

所以验证记录至少要能区分：**哪一代设备、哪一笔事务、请求多少、完成多少、发生什么错误、下一笔是否正确。** “最终看到 PASS”不足以解释中间丢过什么。

这里的**代际 generation** 是软件为每次复位/重新枚举建立的上下文编号，不是 I3C 线上字段。同一个数值 DA 重新分配后可以合法使用；要隔离的是旧上下文，不能规定新 DA 必须换一个数字。

### 1.4 原用例也需要读懂，不能照抄

当前资料已经暴露几处差异：

- 003、004、014 带有“上电先 I2C、DAA 后切 I3C”的旧前提；当前回传为 **I3C-only、Dynamic-only，`mode_i2c=0`、静态地址使能为 0**。004 的枚举仍要验证，不能据此声称验证了 I2C 模式切换；014 的原始正向要求仍待修订。
- 006 写了 `IC_CLR_INTR/IC_CLR_*`。当前 DWC 窗口并没有这套已确认接口；事件要按各自的 W1C、读清、条件解除语义验证。
- 019 原表依赖 017，实际必须先完成 018 的 Hot-Join；023 原表写 021，实际复位前置对应 022。
- 001 原要求覆盖完整寄存器属性；当前实现只验证两个 RW 字段并记录少量只读信息，属于**部分覆盖**。

完整 29 项与知识点的映射见附录第一节（`i3c-controller-learning-evidence.md#1-29-项-testcase-与学习问题`）。[R §11、§13；全用例计划 §4]

## 2. 先把角色和词义摆正

### 2.1 我们叫它“控制器”，它在线上却是 Target

日常说的“I3C 控制器”可以指整块 IP。协议里的 **Controller** 则有明确职责：掌握总线时钟、分配动态地址、发起常规读写。

当前系统里：

| 对象 | 角色与职责 |
|---|---|
| i.MX93 Host | 总线 Controller；当前项目记录确认使用 **SVC / `silvaco-i3c-master`** provider |
| Current Proj I3C0 IP | 总线 **Target**；接受寻址、收数据、返回数据、请求 IBI/Hot-Join |
| Current Proj 裸机 CPU | 通过 APB 配置 Target、服务队列、处理结果 |
| SCL | 串行时钟线；I3C 下由 Controller 驱动 |
| SDA | 串行数据线；按事务阶段交接驱动权 |

**Host Write = 本端 RX；Host Read = 本端 TX。** 后文所有 RX/TX 都从 Current Proj Target 看。[M §4.2、§5.1.2 p37；R §26]

### 2.2 三个容易带偏的词

**Private transfer，私有传输。** 传输业务数据，内容由两端约定；“Private”不表示加密。通用寄存器地址、命令号、业务包头是否存在，取决于上层协议，I3C 不替应用统一规定。[M §4.1、§5.1.2；D §4.2.5]

**Response，控制器内部响应项。** 本文指 IP 写给软件的事务/分片描述，可能含长度、目标、错误和首尾标记。它不等于 SDA 上返回给 Host 的业务数据。RX FIFO 装“收到什么”，Response FIFO 记“这批数据属于什么”。[D §4.2.13.5 p224～225]

**Command，软件给 IP 的执行描述。** TX 命令告诉 Target：哪些数据可以回应下一次匹配的 Host Read。写命令不会让本端自行发起常规 Private Read；SCL 和读请求仍来自 Host。[D §4.2.6.9 p205～207]

### 2.3 几个贯穿全文的底层词

| 术语 | 本文中的意思 |
|---|---|
| **IP，Intellectual Property core** | 可集成进芯片的硬件设计模块；这里指 DWC I3C 模块 |
| **RTL，Register Transfer Level，寄存器传输级** | 描述寄存器、组合逻辑及其时序关系的硬件设计层次；综合配置决定实际保留哪些通路 |
| **APB，Advanced Peripheral Bus** | CPU 一侧访问外设寄存器的片上总线；当前实例数据宽度 32 位 |
| **MMIO，Memory-Mapped I/O，内存映射IO** | 用地址读写访问外设；访问可能推动硬件状态，不能按普通RAM理解 |
| **SDR，Single Data Rate，单数据率模式** | 本文验证的基础传输模式；普通数据 word 使用八位数据加第九位 |
| **HDR，High Data Rate，高数据率模式** | I3C 的另一组传输模式；当前综合包含HDR-DDR，但本轮不展开其专项验证 |
| **entry / word / threshold** | 队列项 / 数据字 / 触发服务的阈值；必须分别注明数量单位与占用、空闲方向 |
| **ISR，Interrupt Service Routine** | 中断服务例程；应与轮询代码约定谁消费队列和读清状态 |
| **CRC，Cyclic Redundancy Check，循环冗余校验** | 用于发现数据错误的校验方法；算法、覆盖范围、初值等共同决定结果，不能只看“CRC”三个字 |
| **provider** | Host软件里真正操作控制器的驱动实现；当前项目记录为SVC，不能套用DWC Master寄存器 |

协议术语对应 M §2、§5.1、§5.2；实例接口与能力对应 R §11。后文首次使用地址、队列、事件和恢复术语时，再解释它们在本工程中的具体职责。

## 3. 协议主线：先有身份，再有对话

### 3.1 两种驱动方式，各有时机

**OD，Open-Drain，开漏：** 设备输出 0 时拉低，输出 1 时释放，由上拉形成高电平。多个参与者同时拉低不会形成高低电平硬冲突，因此适合仲裁。

**PP，Push-Pull，推挽：** 主动驱动高、低电平，适合较快的数据传输；前提是驱动权已经明确。[M §5.1.2.2、§5.1.2.3、§5.1.3.1]

I3C 地址仲裁采用“低占优”：某设备想发 1，却采到 0，就知道自己输了，退出这一轮。较低地址在 IBI 仲裁中通常优先。[M §5.1.2.2.1 p45；§5.1.6.1 p82]

读波形时，应区分三件事：

1. START 之后的**可仲裁地址头**采用开漏仲裁；规范另有地址仲裁优化，需辨认实际流程。
2. **Repeated START 之后，发给 I3C Target 的地址及 R/W 位使用推挽**，此处不再进行地址仲裁；后面的 ACK/NACK 仍是开漏。
3. SDR 数据主要使用推挽，但读方向第九位还涉及 SDA 交接，不能把整笔事务涂成一个驱动模式。

**START** 是开始条件；**Repeated START（Sr）** 是不先 STOP 就开始下一条消息；**STOP（P）** 结束本次总线帧。SCL 高时 SDA 的相应跳变用于表达这些条件，具体合法时机依协议阶段而定。[M §5.1.2 p37、§5.1.2.2.1～2.4 p45～49]

因此，015 的“OD 地址、PP 数据”适合作为入门提纲，不能充当所有地址阶段的逐位断言。逻辑分析仪能显示电平和协议；要独立证明 PAD 的 OD/PP 模式，还需输出使能/驱动控制信号或适当的电气测量证据。

### 3.2 第九位：外形像 I2C，职责已经变了

**ACK/NACK** 是应答/不应答。地址后仍用它表示是否接受寻址；数据后的第九位则要看方向。

| 阶段 | 第九位由谁负责 | 含义 |
|---|---|---|
| 普通寻址的地址头之后 | 接收地址的一方 | ACK 或 NACK |
| SDR Host Write 数据之后 | Host | **奇校验位**，不是每个字节等待 Target ACK |
| SDR Host Read 数据之后 | Target 发出并按规则交接 SDA | **T-bit**：结束或继续，Host 也可在规定时机提前结束 |

写方向的计算是 `parity = XOR(data[7:0]) XOR 1`，让八位数据加校验位合起来含奇数个 1。例如 `0x11` 有两个 1，校验位为 1。[M §5.1.2.3.3 p55]

读方向，Target 的 T-bit 为 0 表示结束；为 1 表示还有数据，并在规定边沿释放 SDA，允许 Host 继续或通过规定的 Repeated START 提前终止。[M §5.1.2.3.4 p56]

这直接解释两个测试现象：

- 020 中，Host 按协议提前结束 Read 可以是合法线上动作；Target 内部仍可能以 `0xA` 报告“发送未按原请求走完”，软件需要清理。
- Host 少写了几个完整字节，硬件可能没有检测到协议错误。是否满足业务长度，仍由 testcase 判断。

### 3.3 DAA：先报身份，再领地址

这组术语要连起来记：

| 术语 | 含义 | 本工程观察位置 |
|---|---|---|
| **PID，Provisioned ID** | 48 位预置身份，用于发现与区分设备 | `SLV_MIPI_ID` 与 `SLV_PID`；Host 枚举信息 |
| **BCR，Bus Characteristics Register** | 8 位总线特征，如角色、IBI 能力 | `SLV_CHAR` 的 `[7:0]` |
| **DCR，Device Characteristics Register** | 8 位设备类型描述；不是唯一序列号 | `SLV_CHAR` 的 `[15:8]` |
| **SA，Static Address** | 静态地址，使用与否受配置控制 | `DEVICE_ADDR` 的 SA 与有效位 |
| **DA，Dynamic Address** | Host 分配的 7 位运行地址 | `DEVICE_ADDR[22:16]` 与 `[31]` 有效位 |
| **DAA，Dynamic Address Assignment** | 动态地址分配过程 | 两端日志与总线帧 |
| **ENTDAA** | 让符合条件的无 DA Target 进入动态地址分配的广播 CCC | Host 发出，Target 硬件参与 |

一次 ENTDAA 的关键顺序如下。这里写的是字段顺序，省略部分交接时序；看精确波形请对照 M 图 25、图 37。

1. Host 发送 `7'h7E/W` 和 ENTDAA 命令。
2. Host 发 Repeated START，再发送 `7'h7E/R`，进入身份发现。
3. 符合参与条件的 Target 以开漏方式送出 **48 位 PID、8 位 BCR、8 位 DCR**，从高位开始仲裁。
4. Host 为本轮胜出的设备发送 **7 位 DA 加奇校验位**；Target 校验并 ACK。
5. Host 继续发现其余参与者；本端记录 DA 有效，后续使用新 DA 通信。

`7'h7E/W` 与 `7'h7E/R` 中的 `7'h` 表示“7 位十六进制地址”，不要把地址值与包含 R/W 的 8 位线上字节混用。[M §5.1.4.1～4.2 p68～72；§5.1.9.3.4 p129]

当前回传默认 PID 为全 0，BCR/DCR 为 `0x3E/0x00`。正式测试应使用项目指定的合法唯一 PID；全 0 不提供唯一身份保障，也不能据此断言单设备 ENTDAA 一定失败。若 PID 用 IP 寄存器覆盖，复位会重新装载 strap，必须在最后一次复位之后配置。[R §13.1]

**strap** 是 IP 在复位时采样的顶层配置输入。它给出初始值；软件后续是否能覆盖，要看具体字段。

### 3.4 CCC：管理总线的公共语言

**CCC，Common Command Code，公共命令码**，用于地址、能力、事件与状态管理。广播 CCC 面向总线上相关 Target；定向 CCC 在公共命令后选定目标。命令有必选、条件必选、可选之分，不能用“规范列了”证明本实例和 Host 都已支持。[M §5.1.9 p109、表16]

| 类别 | 当前学习重点 | 要观察什么 |
|---|---|---|
| 地址管理 | `ENTDAA`、`RSTDAA`；按配置判断 `SETDASA/SETAASA` | DA 有效性、身份一致性、旧上下文隔离 |
| 身份/能力查询 | `GETPID`、`GETBCR`、`GETDCR`、`GETMRL/GETMWL` | Host 返回值与本端实际配置一致 |
| 事件控制 | `ENEC/DISEC` | 允许/禁止哪些事件；不是打开 CPU 的 IRQ 总开关 |
| 状态与恢复 | `GETSTATUS` | 正确读取当前协议状态；恢复时对 SCL 域的作用 |
| 复位动作 | `RSTACT` 与 Target Reset Pattern | 选择动作、检测图样、执行复位分别有证据 |

**MRL/MWL，Maximum Read/Write Length**，分别是 Host Read/Write 的长度属性；当前预置 `0x104 = 260`。它们不同于 FIFO 容量，也不同于当前 Host 客户端一次允许的长度。[R §2、§11]

普通 `GETSTATUS` 是定向命令 `0x90`，返回两字节、先 MSB 后 LSB。其协议 Format 1/2，与 DWC 内部命令/响应的 Format 2 是两件事。[M §5.1.9.3.15 p144～149]

## 4. 当前 RTL：先认清这块 IP 的形状

以下为当前配置与最新回传的汇总，不是板上实测值。[R §11～15、§25]

| 项目 | 当前实例 | 对驱动的直接影响 |
|---|---|---|
| 角色 | Target-only、I3C-only、Dynamic-only 默认配置 | 常规总线事务由外部 Host 发起；不能默认 DAA 前支持 SA 正向访问 |
| CPU 接口 | APB3，32 位数据；non-HCI | 使用本实例的 Synopsys 寄存器映射 |
| 地址 | 系统基址 `0x101005703000`；IP 内部窗口 4 KiB | 完整地址要用 `uintptr_t`；窗口内并非每个偏移都有效 |
| Virtual Target | 综合启用，另有 2 个 VT；本轮只用主 Target 0 | VT1/VT2 保持关闭，仍须使用 Format 2 |
| RX | 普通 RX FIFO：64 个 32 位 entry，256 B | Host Write 数据从 `0x014` 读取 |
| RX Response | 8 个 32 位 entry；默认分片阈值 32 B | 数据与响应并行消费，不能只等最后一条 |
| TX | 4 个扩展队列，每队列 4 个 word，即 16 B | 本轮用 queue0；长包边发边补 |
| 普通 TX | 配置表有 64 entries，但当前生成通路关闭 | 不能等待普通 TX/CMD 空闲量来发送 |
| DMA | 接口启用，2 路 TX 接口 | 能力存在；队列映射、AXIDMA 协作仍需实现 |
| PEC | 硬件支持；分类开关预置开启 | 本轮 PIO 全局 PEC 关闭，不能因分类位为 1 就认为线上附带 PEC |
| HDR-DDR | 综合支持 | 当前 29 项主线按 SDR 展开，不添加 HDR 专项 |

**HCI，Host Controller Interface**，是软件可见的主控制器接口规范；当前选择 non-HCI。**Format 2** 则是这块 DWC IP 的内部命令/响应格式，由相关综合能力决定。二者不是同一个开关。

**Virtual Target，虚拟目标**，是在同一物理接口上提供额外逻辑身份。关闭运行时 VT1/VT2，没有重新综合硬件，也就不会切回旧 Response 格式。[D §4.2.13 表4-19 p218]

![当前 RTL 回传：RX 与扩展 TX 两条数据通路](/2026/11/25/i3c-controller-guide/images/data-paths.png)

*图 1：数据流示意。RX 的数据与 Response 分别排队；虚线表示描述关系，不表示软件从 RX 数据里生成硬件 Response。全部容量取自当前回传。*

## 5. 从复位到可通信：先把地基搭稳

![当前实例从平台准备到枚举和数据服务的流程](/2026/11/25/i3c-controller-guide/images/lifecycle-current-proj.png)

*图 2：完整通信所需的生命周期。当前 CLI 只运行寄存器测试；图中枚举与收发属于后续通信流程。enable ack 可能依赖 Host 提供 SCL，不能让 Host 与 Target 相互等待。*

### 5.1 当前 BSP 真正做了什么

入口是 i3c_bsp.c（`../../components/cmd/src/drivers/i3c/i3c_bsp.c`） 的 `i3c_bsp_prepare()`：

1. 只接受实例 0、`INSTANCE_SLAVE` 和当前 I3C0 基址。
2. 只读上游 MUX/DIV，算出时钟配置快照；保留编码返回错误。
3. 保存原门控状态，再开启 I3C0 门控。
4. 对共享 `SMBUS0_SCL/SDA` PAD 做 RMW，仅把 `FUNC_SEL[11:8]` 设为 **1**。0 是 SMBus，2 是 SMBus/I3C 监测路径。
5. RMW 置位 I3C0 软件复位，`udelay(10U)`，再 RMW 清位释放。
6. 设置实例物理基址。

**RMW，Read-Modify-Write**，读出原寄存器，只改目标字段，再写回；目的是保留同一寄存器里其他外设或 PAD 的配置。

当前策略不轮询门控写后状态，也不轮询 core/APB 复位输出。复位控制寄存器的读取用于 RMW，不属于状态等待。`i3c_bsp_finish()` 只按保存值处理时钟门控，不恢复 PAD，也不能撤销已发生的 IP 复位。

所以 `prepare` 不是可在业务中随意重入的检查函数。**每调用一次，都是一次会清掉硬件上下文的初始化动作。** 旧 README 和旧代码学习笔记中的“不主动复位、不改 IOMUX”已落后于当前源码。[源码；R §17、§18、§22]

### 5.2 通信初始化还需要哪些动作

以下按回传描述完整流程，当前 `i3c regtest` 不执行这些通信步骤：

1. 在停用状态下，只选择主 Target；VT1/VT2 关闭。
2. 配置项目指定 PID；按可写属性确认 DCR，保留 BCR 能力位。完整 `SLV_CHAR` 可写掩码仍未由回传提供，不能整字凭空配置。
3. PIO 最小流程保持 DMA 关闭、全局 PEC 关闭。
4. 按已确认字段准备接收/事件策略，再请求 `DEVICE_CTRL.ENABLE=1`，有界等待 enable ack。
5. Host 执行适当的启动流程和 ENTDAA；Target 检查 DA_VALID 与 DA，Host 核对 PID/DA。
6. 将初始化阶段的 CCC/事件与首笔 Private 区分开，进入收发服务。

**enable ack** 是硬件真正完成使能的确认。当前 `DEVICE_CTRL[31]` 写的是请求，读的是 SCL 域确认；不能把它当成普通配置镜像。跨时钟域更新可能需要 SCL；Host 必须配合提供必要时钟，纯 APB 空轮询不会创造 SCL。[D §4.2.11 p217；R §13.1、§23.1～23.2]

启动还要明确 Hot-Join 策略：DWC 在 `HJ_EN=1` 的相应流程下，发出 HJ 请求之前不参与 ENTDAA。首次直接枚举与运行中 Hot-Join 应分别设计握手，不能让两个前置条件互相等待。[D §4.2.8 p212；R §11.3]

## 6. Host Write：收到数据，还要收齐它的来历

**PIO，Programmed I/O**，指 CPU 直接通过 MMIO 搬数据。轮询与 IRQ 决定“什么时候服务”，PIO 与 DMA 决定“谁搬数据”；它们是两个维度。

### 6.1 先看两只队列

RX 有数据队列，也有 Response 队列：

| 操作 | 前提 | 副作用 |
|---|---|---|
| 读 `0x050[23:16]` | IP 可访问 | 看 RX 占用 word 数，不消费数据 |
| 读 `0x014` | RX level 大于 0 | 弹出一个 32 位 word |
| 读 `0x04C[15:8]` | IP 可访问 | 看 Response 占用 entry 数 |
| 读 `0x010` | Response level 大于 0 | 弹出一条 Response |

**FIFO，First In First Out，先进先出队列。** **level** 是当前占用量或空闲量，方向由寄存器定义；不能看见一个数字就默认它表示“还剩多少”。空 FIFO 的端口读取可能等待，日志也不应随意读取数据端口。[R §12.2、§13.5]

### 6.2 一条 Format 2 Response 怎么读

| 字段 | 当前回传位置 | 含义 |
|---|---|---|
| `DATA_LENGTH` | `[31:24]` | 这一条响应描述的数据字节数 |
| `TARGET_ID` | `[23:16]`，回传宽度 | 目标位图；本轮要求字段值 `0x01` |
| `CMD_SIZE` | `[15:12]` | FIRST 时，首个 FIFO 命令 word 的有效命令字节数；Private 为 0 |
| `ERR_STATUS` | `[11:8]` | **只在 LAST=1 时作为最终错误码解释** |
| `CCC` | `[7]` | 区分 CCC 与 Private |
| `FIRST / LAST` | `[4] / [3]` | 首个/最后一个响应 |
| `ATTR` | `[2:0]` | 当前回传固定为 0 |

**FIRST/LAST 描述事务边界；FIFO word 描述搬运粒度。** 两种边界不能混用。主 Target 的 TX 选择编号是 0，RX 目标位图却是 1：一个是编号，一个是集合中的 bit0。[R §12.3、§13.4]

D 表4-24 p224 将 TARGET_ID 写为 `[20:16]`，回传写为 `[23:16]`；完整拼接证据仍未在本工作区提供。主 Target 字段值 1 在两种描述下相同。这里按当前回传讲解，同时保留差异；不能推广成所有高位都已验证有效。

### 6.3 用四个字节走一遍

条件：DA 已有效，主 Target 独占本次服务，DMA/全局 PEC 关闭，初始化遗留事件已辨明。Host 写入：

```text
11 22 33 44
```

短包示例按以下顺序接收：

1. 等 Response level 大于 0，读一次 `0x010` 并保存。
2. 检查 FIRST=LAST=1、CCC=0、ERR=0、CMD_SIZE=0、TARGET_ID=1、LENGTH=4。
3. 等 RX level 至少为 1，读一次 `0x014`。
4. 按低 byte lane 在先拆成四字节，比较内容。

在上述字段前提下，**推导**的 Response 为 `0x04010018`，数据 word 为 `0x44332211`。它们是学习示例的期望值，不是板上采样。[R §13.4～13.5]

拿这个响应去读旧代码，问题会立刻显形：

```text
正确 LENGTH             = 0x04 = 4
旧代码 response & 0xff   = 0x18 = 24
改成 response & 0xffff   = 0x0018，仍是 24
```

旧 `dwc_i3c_read_rx_fifo()` 取的是低 8 位，会把 FIRST/LAST 当长度。把掩码加宽不能解决格式错配；需要按整个 Format 2 模型重新解释。[源码 `src/dwc_i3c_common.c:dwc_i3c_read_rx_fifo`]

**Little Endian，小端组织**，这里表示 32 位端口的低字节对应先收到的字节；不改变协议在线上的位序。读 `GETSTATUS` 的 MSB/LSB 顺序时，也不能套用 APB word 的小端习惯。

### 6.4 长包为什么必须边收边整理

当前 RX 容量 256 B，Response 队列只有 8 项，默认每 32 B 产生一个接收分片描述。总事务可以大于单条 Response 的 8 位长度范围；MRL/MWL 预置 260 也不因 FIFO 256 B 而自动无效。

长包服务需要同时推进两件事：把 RX 数据搬进软件 buffer；把 Response 保存下来，依据 FIRST/LAST、长度、类型组包。不要等 LAST 才开始搬数据，也不要只排空数据而积压响应。[R §13.2、§14.1]

以合法的 260 B Private Write 为**学习推导**：在默认 32 B 分片条件下，可按八段 32 B 加一段 4 B 理解，合计需要九条数据分片响应，已经超过八项队列容量；边界时刻额外结束响应的具体实现仍需波形/RTL核对。重点是：**队列必须在传输途中被服务，容量不是包长的替代品。**

发生错误时，长度计数可能停止正常增长，已入 FIFO 的数据也不会自动全部消失。只有“收齐、类型正确、最终无错、业务长度和内容正确”，才能交给业务层。

## 7. Host Read：先备货，再发布，最后核对交付

### 7.1 当前 TX 不走旧代码那条路

旧 `dwc_i3c_slave_send_msg()` 向普通 `0x014` 写数据，再向普通 `0x00C` 写命令。当前回传明确普通 TX 通路关闭，本实例使用：

- `EXT_TX_DATA_PORT_0 @ 0x130`：queue0 数据入口。
- `EXT_CMD_REG_0_2 @ 0x168`：长度及匹配信息。
- `EXT_CMD_REG_0 @ 0x164`：命令属性与发布。
- `EXT_CMD_VLD_STAT @ 0x1C8`：完成 pending，**读清**。

**pending** 是尚未消费/处理的事件记录，不等同于当前还在传输。[D §4.2.6.9 p205～207；R §12～15]

### 7.2 四字节发送的准确顺序

同样发送 `11 22 33 44`，仍限定主 Target、PEC off、PIO、queue0 无旧命令且完成事件有明确归属。

| 顺序 | 操作 | 为什么 |
|---|---|---|
| 1 | 等 `0x154[2:0] >= 1` | 至少空出一个 32 位 entry |
| 2 | 写 `0x130 = 0x44332211` | 数据先可用 |
| 3 | 写 `0x168 = 0x00040000` | 声明请求长度 4 B |
| 4 | 写 `0x164 = 0x00010001` | `CMD_ATTR=1`、Target 0、`CMD_VLD=1`，最后发布 |
| 5 | 通知 Host 已 READY，由 Host 发起 4 B Read | READY 是两端测试约定，不是 I3C 新协议字段 |
| 6 | 有界等 `CMD_VLD` 清 0 | 命令已经结束，但还没有证明成功 |
| 7 | 读一次 `0x1C8` 并保存，检查本笔新 bit0 | 避免把旧 pending 当新完成 |
| 8 | 保存 `0x164` 错误与 `0x168[31:16]` 剩余长度 | 要求 ERR=0、remaining=0 |
| 9 | Host 核对实际长度和四字节内容 | 完成两端交付验收 |

正常四字节完成不需要 reset queue0。正常采样顺序按 R §14.2；故障快照必须在任何清理/reset 之前保存，见第 9 节。

这里有两个必须记牢的版本差异：

**第一，当前 bit6 不实现 `FINITE_DL`。** D 表4-23 p222 有这个字段，当前 RTL 回传说写入无效、读 0；本实例由 DATA_LENGTH 是否非零决定有限长度。`length=0` 会选无限长度，不能当普通零字节请求发布。[R §15]

**第二，有限命令完成后的 `0x168[31:16]` 是剩余未发长度。** 正常请求 4 B、全部读完，值应为 0；旧资料中的 `actual==4` 已由最新回传废止。[R §25]

仅在同一笔有限命令、计数未被 reset/新命令覆盖且 `remaining <= requested` 时，才能按 `requested - remaining` 推导计数意义上的发送进度。异常边界的该计数与 Host 实际收到多少，仍需双方证据核对。

### 7.3 长包的陷阱，是等一个永远不会空出来的位置

queue0 只有 16 B。若想先填完一个 64 B 包，再发布命令，CPU 填满 16 B 后就会等空间；Host 又因命令尚未发布而不能正常取数，双方互等。

正确思路是：预填已有空间，发布完整请求长度与命令，Host 读取期间持续按 free-level 补数；同时观察命令是否提前结束，结束后停止补旧数据。[R §14.4]

尾部不足 4 B 时仍写一个 word，但只取源 buffer 的有效字节，其他 lane 明确填充；线上长度由命令控制，填充不计入 payload。

**Underflow，欠载**，表示发送供给未满足硬件所需条件；**Overflow，溢出**，表示接收或响应资源无法继续接纳。当前 TX 欠载有具体终止与有限长度条件，不能把“某一拍 FIFO 空”直接等同于必报 `0x6`。[R §20.3]

## 8. IRQ、IBI、Hot-Join：三种通知，三个方向

### 8.1 IRQ 通知本端 CPU

**IRQ，Interrupt Request，中断请求**，此处是 IP 给本端 CPU 的 `ic_intr`。单根组合中断可以汇集多个原因，软件必须保存状态并逐项服务。

`INTR_STATUS @ 0x03C`、`INTR_STATUS_EN @ 0x040`、`INTR_SIGNAL_EN @ 0x044` 分别涉及状态、状态使能和输出使能。屏蔽输出不等于清除来源。[源码；R §23.3～23.4]

| 语义 | 操作会发生什么 | 典型误用 |
|---|---|---|
| **W1C，Write One to Clear** | 写 1 清对应已确认事件；写 0 不清 | 把未核实的整个状态字写全 1 |
| **读清** | 读出同时清记录，例如 `0x1C8` | 日志先读一次，业务再读一次，完成“消失” |
| **条件型状态** | FIFO 占用/空闲满足条件时成立 | 只清状态而不搬数据，IRQ 反复进入 |
| **FIFO pop** | 读取拿走一个队列项 | 把队列端口当成无副作用寄存器反复看 |

这就是 006 真正应该验证的内容：指定原因消失，其他原因仍在；全部可清原因处理后，下一轮数据和通知仍正确。完整 W1C 位表尚缺，不能凭旧 `IC_CLR_*` 文本造接口。

当前 `dwc_i3c_register_intr()` 和回调是“未支持”的日志占位；通用 handler 中 RX/Response 服务分支还被注释，`dwc_mipi_i3c_init()` 将输出使能设为 0。组合 IRQ 的完整链路尚未接入。

### 8.2 IBI 通知外部 Host

**IBI，In-Band Interrupt，带内中断**，复用 SCL/SDA 通知 Host，不另外占一根中断线。Target 请求并发送自身 `DA/R`，Host 接受后读取事件信息。[M §5.1.6 p82～85]

**MDB，Mandatory Data Byte，强制数据字节**，是带数据 IBI 的第一个说明字节。BCR[2] 指示相应能力；接受此类 IBI 后，Host 需要接收 MDB，后面才是可选附加 payload。MDB 的高 3 位为事件组标识，低 5 位为具体事件标识；实际取值应遵守项目采用的定义。[M §5.1.6.2.1 p84]

因此 017 的“payload=0”可以表示没有附加 payload，**不等于省略必须存在的 MDB**。Host 接收 buffer 的长度口径也必须说明是否包含 MDB。

016～017 至少需要四层条件：硬件支持；Target 当前允许；Host 已申请并允许接收；两端同意 MDB、长度和 PEC 策略。旧 `dwc_i3c_slave_issue_ibi()` 只有无数据 SIR/MR 路径，不能证明带 payload IBI 已实现。SlaveLite 虽有带数据函数，却使用另一套寄存器与 RAM 接口，不能搬来直接用。[源码；D §4.2.9 p213～215]

### 8.3 Hot-Join 通知“我现在可以加入了”

**Hot-Join，热加入**，用于总线已运行后出现的 Target。它使用保留地址 `7'h02/W` 请求加入，没有普通 IBI 的 MDB/payload；Host 随后安排 ENTDAA。[M §5.1.5 p76～79]

018 需要证明线上出现 HJ 请求、Host 接受、后续枚举成功；人工触发一次重新 DAA 不能替代 HJ 证据。019 进一步要求新代际的 CCC、Write、Read、IBI 都成功。通知被听见，只是开始；业务恢复才是落点。

## 9. 错误恢复：把旧事务收好，才有下一笔

![当前 RTL 回传的 RX/TX 恢复与 Host 配合流程](/2026/11/25/i3c-controller-guide/images/recovery.png)

*图 3：恢复主线。活动期间先留初始快照，旧事务结束后再保存最终结果；图中的“原始故障快照”必须早于队列 reset。所有等待都有超时，不能仅凭图中某一个寄存器条件放行。*

### 9.1 错误码需要上下文

当前回传中的主要 SDR 情况如下。[R §19～21]

| 场景 | 编码 | 应怎样理解 |
|---|---|---|
| RX parity 错误 | `0x2` | 在 LAST 响应中归因；已入队数据未必回滚 |
| RX FIFO/Response 资源问题 | `0x6` | 看两个 level 和服务历史，区分数据拥塞与响应拥塞 |
| RX PEC 错误 | `0xC` | 前提是实际启用了相应 PEC；不是 HDR CRC 的 `0x1` |
| TX finite underflow | `0x6` | 内部 `0x7` 映射为外部 `0x6`；检查触发条件 |
| Host 提前结束 TX | `0xA` | 可能有残留待发数据，必须按 TX 恢复处理 |
| 活动事务 SDA-release 超时 | `0x8` | 是释放机制的错误结果；不能仅凭 SDA=0 认定 |
| 无硬件错误 | `0x0` | 仍需核对业务长度、内容和新完成事件 |

**HALT** 指内部因错误停止推进正常业务的状态。它与“物理总线空闲”“FIFO 已空”“CPU 不再访问”分别描述不同事实。

RX 与 TX 还共用内部错误状态。EXT queue reset 可能重新采样这份状态，覆盖 TX 错误/长度，却不产生一次新的正常 TX completion。必须保存清理前的原始结果，将 reset 采样另作记录。RX 响应若带 `0xA`，不能解释成“Host 少写了几个字节”；需要追查先前 TX 和共享状态历史。

### 9.2 正常恢复先保持 ENABLE

当前回传没有实现通用 `DEVICE_CTRL[29] ABORT`。写 ENABLE=0 是请求停用，不能保证立即打断正在进行的事务；若 SCL 停止，跨域确认也可能无法推进。[R §23.1～23.2]

正常恢复建议保持 ENABLE=1：停止发布新命令和补旧 TX 数据，让 Host 结束旧事务并暂停新的 Private。保留 pclk/core clock，停止其他 PIO/ISR 消费者，再处理队列。

**shadow** 是软件保存的“原本打算配置什么”的副本。控制寄存器读回可能包含 ack、自清请求和状态，不能无条件拿读回值重建配置。RESUME 时要保留已知有效配置，不要只写一个 `0x40000000`，顺带把 ENABLE 等字段清掉。

### 9.3 RX 与 TX 的清理不同

**RX：** 旧事务终止后，反复检查 level，交替保存/消费 Response 与 RX 数据。Response 队列满时，腾出空间后还可能出现延迟错误响应，不能按第一张快照读完就结束。最终 LAST 不出现，应记录不完整事务并有界失败。

确需丢弃残留时，回传给出普通 RX/Response reset：向 `0x034` 写 `0x14`，仅 bit4、bit2，保持 core soft reset bit0 为 0。以有序访问核对实际 level；**不能用 `read(0x034)==0` 作为该普通队列 reset 的完成判据**。[R §23.4]

**TX queue0：** 原始错误、剩余长度、completion 已保存，旧事务已终止，且无并发写入之后：

1. 用发布配置 shadow 写 `0x164`，清 KEEP_CMD[17]，CMD_VLD[16] 写 0，不发布新命令；写 0 本身不撤销旧命令。
2. 向 `0x1C4` 写 1，只 reset queue0。
3. 有界等 `0x1C4[0]=0`、`0x164[16]=0`、`0x154[2:0]=4`。
4. 读取并缓存清理阶段的 `0x1C8`，与原始 completion 分开保存。

EXT reset 的等待条件与普通队列不同。局部 queue reset 不应修改 DA/PID/PEC，也不会自动解除 HALT；跨 SCL 域的清理仍可能需要 Host 提供时钟。[R §24]

### 9.4 RESUME 和 GETSTATUS 必须接得上

**RESUME** 是请求硬件恢复推进的控制动作；**GETSTATUS** 是 Host 在线上发出的公共状态查询。

Target 请求 RESUME 后，应允许 Host 向当前 DA 发普通两字节 GETSTATUS。它既提供 SCL，也按协议/实例规则清理影响后续 ACK 的错误锁存。**APB 读 `CCC_DEVICE_STATUS` 不能替代线上 GETSTATUS。** 不能先等 RESUME 自清，再允许 Host 给时钟，形成互等。[M §5.1.9.3.15 p144～145；D §4.2.5.2 p196；R §23.4]

最终要同时检查：

- `DEVICE_CTRL.RESUME=0`。
- `CCC_DEVICE_STATUS @ 0x058` 的停止/错误锁存 bit9 为 0。
- `PRESENT_STATE[12:8]=0`，即当前回传的 IDLE。
- 旧 RX/Response/EXT 结果已归档和清理；仅清已保存、已服务的 W1C 事件。
- 使用**不同的新 payload** 再做最小收发，核对新响应/新 completion、内容、队列和 DA/PEC。

第一个条件是“请求处理了”，最后一个条件才回答“业务活过来了”。[R §23～25]

Host 现有恢复客户端据项目记录支持暂停自身新 Private、1～16 B 短传输和 GETSTATUS。`getstatus` 成功后仍保持暂停，等待 Target 核对恢复后手动放行；它不能强制终止所有总线参与者，也不能读到 Target 的全部 APB 恢复条件。[R §26；Host 配合计划]

### 9.5 四种 reset，不要混成一件事

| 动作 | 主要影响 | 验证重点 |
|---|---|---|
| 普通/EXT 队列 reset | 局部数据、命令、指针 | 残留清掉了没有；HALT 是否另行恢复 |
| IP 内部 core soft reset | 由本实例 reset 逻辑决定 | 不能等同板级硬复位，范围需单独确认 |
| SoC I3C0 模块复位 | 当前 BSP 的 SWRST 路径 | 身份重装、DA/事务上下文重建，对应 005、028 |
| 线上 Target Reset Pattern + RSTACT | Host 通过总线触发支持的动作 | 波形、检测、动作与恢复分别验证，对应 022、023 |

M §5.1.11.3 p222 规定 Target Reset Pattern 包含 SCL 保持低时的 14 次 SDA 跳变，再以 Repeated START 和 STOP 触发动作。当前配置支持 Pattern 检测，不意味着所有 RSTACT 动作或整芯片复位都已支持。[D §4.2.14 p225；R §7.1、§11.3]

若 Host 不能结束事务或提供必要 SCL，已有资料不保证纯 APB、保留 DA 的强制恢复。应按有界失败处理，升级到双方/板级恢复；不能无限循环，也不能自动 reset 后抹掉失败证据。

## 10. 时钟、DMA、PEC、带宽：把边界算清

### 10.1 三种频率，三个口径

**模块时钟**驱动 IP 内部逻辑；**SCL**是 Host 给出的串行时钟；**EMU 真实时间频率**是仿真平台时间缩放后的值。它们不能互相代替。

当前 `i3c_bsp_get_clock_info()` 读取 management CPU MUX 和三级 DIV：

```text
source = MUX 0/1/2 对应 100/1000/1200 MHz；编码 3 非法
divisor = (DIV[2:0]+1) × (DIV[5:3]+1) × (DIV[8:6]+1)
nominal_hz = source_hz / divisor
emu_real_hz = source_hz × 7 / (divisor × 6000)
```

函数最后取整，且只读共享时钟配置。例：MUX=0、三级除数 1、2、2，标称为 25 MHz，当前 EMU 换算约 29.166667 kHz，整数接口返回 29166 Hz。它是配置推算，不是测频，也不证明门控实际生效。[源码 `i3c_bsp_get_clock_info`；R §18]

旧“固定 300 MHz”结论已撤销。027 要验证全部合法频点，每频点 10000 轮；现有 BSP 只读解码，尚未实现该用例的受控切频。更不能为了测 I3C，随手改共享 CPU/bus 分频。

I3C SCL 由 Controller 驱动，Target 不具备 I2C 式任意拉低 SCL 等软件的机制。压力下看到长 SCL Low，要核对 Host stalling、故障或板级状态，不能直接写成“Target 正常 clock stretching”。[M §5.1.2 p37、§5.1.2.5 p59～63]

### 10.2 DMA 只替你搬数据，不替你结束事务

**DMA，Direct Memory Access**，由硬件在内存与 FIFO 端口之间搬运。当前 SoC 编号以 dma_handshake.h（`../../components/cmd/src/drivers/axidma/dma_handshake.h`） 为准：

| 本端方向 | 枚举/编号 | 数据路径 |
|---|---|---|
| TX | `AXIDMA_HS_I3C_TX = 23` | 内存到扩展 TX 数据端口 |
| RX | `AXIDMA_HS_I3C_RX = 24` | RX 数据端口到内存 |

旧 AXIDMA 映射文档中的方向与当前枚举有差异，不能混用。**I3C 两路 TX 接口、四个 EXT queue、AXIDMA channel、SoC handshake ID** 属于不同编号空间。[R §5.1]

009/012 的成功条件包含 DMA 和协议两边：RX 仍要消费全部响应并看到 LAST；TX 仍要核对扩展命令的新完成、错误和剩余长度。DMA done 最多说明它负责的搬运完成，不能独立证明 Host 已收到。

**缓存一致性**指 CPU 与 DMA 看见同一份最新内存。CPU 准备 TX buffer 后、DMA 写完 RX buffer 后，都要遵守平台的同步策略；数据端口固定地址、内存递增、burst、尾字节和请求结束方式也要匹配。当前完整 DMA 队列映射与尾包路径尚未接通，先理解双完成条件，再设计 009/012。[D §2.1.3 p36～38；R §5.2]

### 10.3 PEC 与测试 CRC 分属两层

**PEC，Packet Error Check**，在当前 DWC 功能中使用 CRC-8，生成多项式为 `x^8+x^2+x+1`，适用于所描述的 SDR CCC、Private 与 IBI。覆盖范围按事务类型定义，不能只拿 payload 做 CRC 就假定等于线上 PEC。[D §4.2.15 p225～228]

本实例全局开关是 `DEVICE_CTRL[26]`，分类开关位于 `TGT_CAP_CTRL @ 0x110`。当前最小 PIO 策略保持全局关闭，即使分类位预置为 1，示例也不附加 PEC。[R §12.3]

**测试 CRC**则是业务 payload 中为了发现丢包、错序、内存破坏而设计的校验。它不能替代 PEC；SDR 每字节奇校验、PEC、HDR-DDR CRC、RAM parity、业务 CRC 也各有层次。学习时先问“它覆盖哪里”，再问“算法叫什么”。

### 10.4 队列容量，其实也是服务时间

在稳定 SDR 数据阶段，每字节占 8 个数据位加 1 个第九位。忽略地址、起止和间隙，纯 payload 的理想速率约为 `fSCL / 9` B/s。[由 M §5.1.2.3 推导]

若 **SCL 假设为 12.5 MHz**，理想数据阶段约 1.39 MB/s。由此得到两个只用于理解的量级：

- 空的 256 B RX FIFO，从空到满约 184.32 μs。
- 满的 16 B EXT TX FIFO，从满到空约 11.52 μs。

这不是当前 EMU 的实测吞吐，也不是允许 IRQ 延迟；实际剩余空间、阈值、Host 间隙和内部流水会改变预算。但它解释了为什么长 TX 补数比看到“64 深度 FIFO”时想象的紧迫得多。

029 应分别记录方向、PIO/DMA、payload 长度、次数、实际有效字节数和起止时间：

```text
有效带宽 = 正确交付的业务字节总数 / 对应测量时长
```

先定义测量是否包含调用、等待和帧间间隔；错误样本另报，不能悄悄丢弃。长包减少固定开销占比，因此通常更接近数据阶段上限，但 Host 长度边界、FIFO 服务和 EMU 时间缩放仍限制实际结果。当前 Host 恢复客户端的 1～16 B 限制不是 I3C 协议最大包长。[R §26；xlsx 029]

## 11. 回到代码：哪些能学，哪些不能照用

### 11.1 当前真正接通的路径

从 CLI 看：`i3c_command()` 调用 `i3c_test_regtest()`；后者调用 BSP prepare、`i3c_regtest_run()`、BSP finish。寄存器访问经过 `dwc_i3c_read_reg/write_reg()` 和 HAL。

001 当前检查停用、DMA/ABORT/RESUME 位、内部 reset、IRQ mask 与 Target 角色；记录版本、能力等六个寄存器；对 `DEVICE_ADDR.STATIC_ADDR[6:0]` 和 `SLV_PID_VALUE.SLV_INST_ID[15:12]` 两个字段做写读恢复。[源码 `i3c_test.c`；D §4.2 p177、§4.2.4.5 p191～192]

它能证明有限寄存器访问路径，不能证明静态地址业务有效，也没有遍历完整 001 要求的 reset/RO/保留位/IRQ/FIFO 属性。当前 CLI 没有 007、010 等收发 case 分发。

### 11.2 建议的源码阅读顺序

| 顺序 | 文件/函数 | 带着什么问题读 |
|---|---|---|
| 1 | `i3c_test.h`、`i3c_cmd.c`、`i3c_test.c` | 当前公开入口到底做了什么，PASS 覆盖到哪里？ |
| 2 | `i3c_bsp.h/.c` | 基址是否完整，PAD 谁拥有，何时发生复位，频率如何算？ |
| 3 | `inc/dwc_i3c_common.h`、`src/dwc_i3c_common.c` | 对象、寄存器、事件与 buffer 如何表示；哪里还停留在旧格式？ |
| 4 | `src/dwc_i3c_slave.c` | 收发/IBI 的原接口意图是什么，与当前 RTL 差在哪里？ |
| 5 | `tests/regtest_host.c`、`tests/bsp_host.c` | 失败路径和副作用如何被验证？ |
| 6 | `src/dwc_i3c_master.c` | 选读命令构造，理解 Controller 侧如何描述事务；当前本端不用它驱动总线 |
| 7 | `src/dwc_i3c_slavelite.c` | 只作为另一类 IP 接口的参考，识别不同寄存器/RAM 模型 |

上述路径统一位于 components/cmd/src/drivers/i3c（`../../components/cmd/src/drivers/i3c`）。全文件与逐函数定位见附录；不用把头文件里的所有宏先背下来。

### 11.3 最影响理解的几个实现缺口

| 源码现状 | 学习时应怎样处理 |
|---|---|
| RX 旧低 8 位长度解析；`msg` 没有独立容量字段；负错误通过无符号返回类型传递 | 用 Format 2 与显式容量/错误契约重新理解，不能把大返回值当收到了很多字节 |
| TX 使用普通 FIFO/Command，等待没有完整 deadline | 参考接口意图，具体数据通路以第 7 节为准 |
| 旧通用 handler 错误分支消费 Response 后调用 Master 错误处理；还复用了状态变量 | 不能拿它作为当前 Target IRQ 恢复模板，Response 必须单一消费者 |
| `dwc_mipi_i3c_init()` 有固定 timing 值、全开 STATUS_EN、普通 Target 初始化 | 是保留的通用实现，不是当前 Format 2 端到端初始化证明 |
| HAL 检查空地址、对齐、加法溢出，但未限制 4 KiB 偏移范围 | 地址类型正确与寄存器访问合法性是两层检查 |
| Master/SlaveLite 源码参与编译，BSP 只接受当前 Target 实例 | 编译存在不等于当前硬件支持/当前路径可用 |

## 12. 用五道小题检验自己

1. **Response=`0x04010018`，为什么旧函数想读 24 B？** 低字节是标志组合；真正长度在高字节。先解格式，再取字段。
2. **Host 想读 20 B，queue0 只有 16 B，先填完再发布行吗？** 不行。先预填、发布，再随空间补数，同时监控提前结束。
3. **`CMD_VLD=0`、remaining=0，为什么还不能报成功？** 可能是旧状态或 reset 采样；还需本笔新 completion、ERR=0、Host 长度与内容正确。
4. **RESUME 一直不清，多等一会儿一定好吗？** 不一定。SCL 域可能缺时钟，Host GETSTATUS/总线恢复需要介入，等待必须有界。
5. **001 测静态地址字段通过，为什么 014 仍不能报 PASS？** 寄存器可写证明存储/访问属性，不证明当前默认配置的 DAA 前 SA 业务成立。

练习时可以先遮住答案，画出“谁提供数据、谁产生结果、谁消费结果”。能把三个角色画清，就开始真正读懂控制器了。

## 13. 证据与本次验证

这份手记核对了 testcase 全部 29 行，核对当前 I3C 模块源码、主机测试、SoC 关联定义，检索两份 PDF 并查看关键字段表原页；同时合并 RTL 记录到第 26 节的最新结论。未把旧笔记中已废止的时钟、PAD、复位、TX 长度解释作为当前事实。

执行了已有主机测试：

```sh
bash components/cmd/src/drivers/i3c/tests/run_host_tests.sh
```

结果：`I3C host tests: PASS`、`I3C BSP host tests: PASS`。前者覆盖寄存器故障注入和真实 CLI 分发，后者覆盖当前 BSP/HAL、时钟解码、PAD 编码和复位顺序。**未运行板级 DAA、PIO、IRQ、DMA、IBI 或故障恢复；未重新做固件交叉编译。**

尚需实际确认的主要边界：完整寄存器属性/W1C mask、回传字段宽度差异、DMA 内部队列映射与尾包、Host 高级异常刺激、共享 IO 自动识别路径、合法频点/运行契约。它们不妨碍建立学习模型，但决定后续哪些 testcase 可以完整验收。

图中展示的是流程示意，不是 RTL 原理图或实测波形。逐项 testcase、源码位置和寄存器访问索引见《控制器学习核对附录》。


## 同专题阅读

- [MIPI I3C Basic v1.1.1 组内分享](/2026/11/25/i3c-basic-spec/)
- [DWC MIPI I3C Target 开发学习文档](/2026/11/25/i3c-target-study/)
- [Linux I3C 驱动视角学习笔记](/2026/11/25/i3c-linux-driver/)
