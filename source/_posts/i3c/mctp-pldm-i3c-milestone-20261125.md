---
title: MCTP/PLDM over I3C 移植里程碑：HOST 已就绪，首包停在地址 NACK
date: 2026-11-25
permalink: 2026/11/25/mctp-pldm-i3c-milestone-20261125/
categories: [嵌入式开发, I3C]
tags: [i3c, mctp, pldm, linux, baremetal, bringup, debugging]
toc: true
max_depth: 3
mermaid: true
description: 记录裸机端点独立移植包、i.MX93 slim 内核回移、上板部署和首笔 Get Endpoint ID 失败的全过程。当前枚举与驱动绑定通过，MCTP/PLDM 端到端尚未打通。
---

今天把 MCTP/PLDM 接到了一条已有回归基础的 I3C 链路上。i.MX93 已经启动新内核，能创建 `AF_MCTP` socket；Target 枚举成功，绑定了 `mctp-i3c`，`mctpi3c0` 也出来了。但第一条 `Get Endpoint ID` 没有拿到响应：HOST 向动态地址 `0x44` 发起 Private Write，在地址阶段遇到了 NACK。

**截至 2026-11-25 本轮记录结束，MCTP/PLDM over I3C 尚未 bring-up 成功。** 已完成的部署、离线测试和枚举各有证据；PEC、响应 IBI、Private Read、PLDM Base 以及真实传感器业务，还没有得到这套双端组合的联通结果。

<!-- more -->

这个停点值得留下来。否则过几天再看，很容易把“模块加载成功”“网口出现”“socket 发送返回”混成一句“协议已经通了”，然后从错误的位置继续排查。下面按今天实际走过的顺序，记下设计选择、代码移植、编译中碰到的问题、部署方法和最后的失败现场。

## 1. 先把目标讲明白：总线上的字节，怎样变成管理命令

最初的问题是：Current Proj 已有 I2C、I3C、SMBus 等接口，也做过读写、中断、DMA 验证，现在想让外部主机通过标准协议管理芯片。

可以先用一个很小的例子理解这件事。HOST 想问：“你的温度是多少？”

- I3C 负责完成总线上的寻址和数据传输。
- MCTP 负责把管理消息送到正确端点，处理消息类型、Tag 和分包重组。
- PLDM 负责规定“查询什么对象、请求怎样编码、响应怎样解释”。

到了实际实现里，这个问题还得拆开：谁拥有收发队列，谁知道本次实际收到了多少字节，响应准备好以后谁通知 HOST，什么时候才能释放 buffer？这些都需要在协议库和现有驱动之间落实。

首轮目标收敛为：**一个 Linux HOST、一个裸机 Target，先完成 MCTP Control 查询，再完成 PLDM Base 能力查询。** 温度、电压、PDR、FRU、固件更新等业务暂不接入。

### 1.1 几种角色先分开

| 层次 | i.MX93 HOST | Current Proj 芯片侧 |
|---|---|---|
| 运行环境 | Linux | 裸机固件 |
| 本轮管理角色 | 发起管理请求 | 处理并响应请求 |
| I3C 角色 | Controller | Target |
| MCTP 角色 | 本地端点；首轮手动配置拓扑 | 被管理端点，首轮固定 EID |
| PLDM 角色 | Requester | Responder |

前期讨论里还使用过 DUT，即 Device Under Test。后续统一写 HOST 和 Target，避免把“被测设备”“被管理设备”和总线角色混在一起。

MCTP Bus Owner 是端点配置管理角色，和 I3C Controller 属于不同层次。本轮先采用固定 EID，没有上线自动端点发现和 EID 分配服务。

### 1.2 为什么最后选了 I3C

最早讨论过 SMBus/I2C binding。它需要核对双方的主动发送、被寻址接收和仲裁能力，不能套用普通的“主机写寄存器，再读从机”模型。

随后确定首版使用 I3C，原因是现有 i.MX93 HOST 已经和 Target 做过实际 I3C 回归，可以直接复用 Controller、Target、Private Transfer 和 IBI 的底层基础。历史记录里有 HOST 侧 `33/33` 步通过；相应记录没有为这个 MCTP 新组合提供端到端通过证据。

这一选择把需要新增的工作集中到了三处：

1. Target 的设备身份与运行模式，要能被 MCTP 驱动识别。
2. Private Transfer 和 IBI，要承接完整 MCTP 包、PEC 和包生命周期。
3. HOST 精简内核，要恢复 MCTP 核心并接上 I3C transport。

## 2. 对外通信与本地采集，是两条路径

PLDM 能承载温度、电压、状态等管理信息，但这些数据从哪里来，由芯片的硬件和业务实现决定。

```text
对外管理路径：
i.MX93 → PLDM / MCTP / I3C → Current Proj 管理固件

本地采集或控制路径：
管理固件 → MMIO / I2C / PMBus / Mailbox / 其他本地接口
```

例如，HOST 查询一个电压 Sensor ID，Target 可以通过 PMBus 读取电源芯片，再把数值、单位、有效状态按 PLDM 定义返回。电源芯片不必理解 MCTP 或 PLDM。

| 管理对象 | 可能的本地来源 | 后续需要落实的内容 |
|---|---|---|
| 片内温度、健康状态 | MMIO、片内监测模块 | 原始值、换算、有效性、错误状态 |
| 电压、电流、功率 | PMBus 或器件专用接口 | 具体命令、单位、读取失败的表示 |
| 板上温度 | I2C、SMBus、SPI 等传感器接口 | Sensor ID 与实际器件的映射 |
| 子系统状态 | Mailbox、共享内存 | 状态一致性和超时 |
| 控制对象 | 本地驱动、平台控制接口 | 权限、边界、执行结果 |

AVSBus 可以参与本地电压控制；能读取哪些遥测信息，要继续看电源器件能力。当前没有建立上述业务 provider，也没有通过真实传感器读取验收。

## 3. 确定双端架构：Linux 用内核 MCTP，裸机接协议库

HOST 的方案是 Linux MCTP 核心加 `mctp-i3c`，用户态使用 `libpldm` 编码和解析 PLDM。Target 则提供独立的 `libmctp`、`libpldm` 移植起点，通过 I3C binding 对接现有驱动。

<pre class="mermaid">
flowchart TB
  subgraph H["i.MX93 HOST · Linux"]
    HA["用户态 requester / libpldm"] --> HS["AF_MCTP socket"]
    HS --> HC["内核 MCTP core"]
    HC --> HB["mctp-i3c binding"]
    HB --> HD["I3C core / SVC Controller"]
  end
  subgraph T["Current Proj · 裸机 Target"]
    TD["I3C Target 驱动 / BSP"] --> TB["I3C binding / PEC / TX 队列"]
    TB --> TC["libmctp / Control / 重组"]
    TC --> TP["libpldm / Base 命令分发"]
    TP --> APP["管理业务 provider · 后续接入"]
  end
  HD -->|"Private Write 请求"| TD
  TD -->|"IBI 通知，随后由 HOST 读取"| HD
</pre>

图中是职责分层。响应方向会沿对应层返回；CCC 在 I3C 层处理，MCTP Control 在 MCTP 层处理，并不经过 PLDM 业务层。

### 3.1 HOST 应用有哪些现成接口

Linux MCTP 的用户态收发接口是 `AF_MCTP`、`SOCK_DGRAM`：

```c
int fd = socket(AF_MCTP, SOCK_DGRAM, 0);
```

应用通过地址结构和消息内容指定 network、EID、类型、Tag 等信息。MCTP 核心负责寻址及分包，transport 处理 I3C 封装和 PEC。[Linux MCTP 文档](https://docs.kernel.org/networking/mctp.html)给出了 socket、接口、network 和路由模型。

本轮没有“启用内核 PLDM”这个步骤。PLDM 编解码位于用户态，首版只做一个小型 requester，没有安装整套 OpenBMC，也没有部署 `mctpd`。

### 3.2 Target 侧怎样调度

裸机不需要给每一层创建线程。计划由 ISR 记录必要硬件事件，再由主循环串行运行协议逻辑：

```text
I3C RX / DMA 完成
  → 保存实际长度、数据和硬件结果
  → 主循环校验 PEC
  → MCTP 接收及必要重组
  → Control 或 PLDM 命令处理
  → 响应排队
  → 准备硬件 TX 数据
  → 发 IBI 0xAE
  → 等待 HOST Private Read
  → 确认完整传输后出队
```

中断里仍要及时服务 FIFO、记录完成和错误；内存分配、重组和业务执行留在主循环。DMA buffer 的地址转换、缓存同步和有效期由平台层负责。

原有 `i3c_test` 保留为独立回归入口。它和管理服务要通过运行模式、设备所有权切换，不能同时消费同一组 FIFO、Response、IBI 和收发会话。

## 4. 地址与编号：今天固定了哪些值

本轮固定配置如下。它们分属不同层，恰好取值接近也不能互换。

| 名称 | 本轮值 | 含义 |
|---|---|---|
| I3C PID | `beee00000101` | Target 的设备身份，本 transport 用于邻居寻址 |
| I3C 动态地址 DA | `0x44` | 当前总线寻址所用的 7-bit 地址 |
| MCTP network | `1` | HOST 本地 MCTP 网络编号 |
| HOST EID | `8` | HOST 的 MCTP 端点地址 |
| Target EID | `9` | Target 首轮管理端点地址 |
| Target TID | `1` | 本轮 Target 配置的 PLDM Terminus ID |
| MCTP Message Tag | 运行时分配 | MCTP 交互关联标识，本轮未留存具体值 |
| Control / PLDM Instance ID | 由上层请求构造 | 命令层关联请求和响应的标识 |

特别是下面这条邻居命令：

```bash
./mctp neigh add 9 dev mctpi3c0 lladdr be:ee:00:00:01:01
```

`9` 是目的 EID，后面六字节是 PID。本轮 `mctp-i3c` 用 PID 找到设备，再取得当前动态地址去发总线事务。不能把 `lladdr` 直接填成 `44`。

这套固定配置用于缩小首轮问题范围。HOST 本地添加 EID、路由和邻居表，不会自动把 Target 的 EID 改成 9。Target 必须已经使用对应值。

## 5. 包长、PEC 与 IBI：binding 必须落实的约定

### 5.1 64、68、69 分别算到哪里

首版选择基线容量，三个长度口径为：

```text
64 B MCTP packet payload
  + 4 B MCTP transport header
  = 68 B MCTP packet / 本轮网络 MTU

68 B MCTP packet
  + 1 B I3C PEC
  = 69 B I3C 数据区容量
```

消息首包中的 Message Type 占用上述 payload，不再额外增加一份。地址/RW 阶段也不计入这 69B 数据区。

69B 是本轮 binding 必须承接的基线容量，实际短消息按真实长度传输。GetEID 请求只有 8B，不能为了凑满容量填充到 69B。

HOST 的网络 MTU 固定为 68，读上限固定为 69。Target 宣告 MRL/MWL 为 260，并不意味着本轮已经开放或验证更大的 MCTP packet。SVC 代码的 `RDTERM` 字段为 8 bit，也要求认真处理读取上限。

### 5.2 PEC 的职责和计算输入

本轮采用软件 PEC。接收时先按实际长度校验，再剥离 PEC 交给 MCTP；发送时根据本次 DA 和方向计算后追加。

```text
Write 校验输入：[(DA << 1) | 0] + MCTP packet
Read  校验输入：[(DA << 1) | 1] + MCTP packet
```

CRC-8 多项式为 `0x07`，初值、最终异或均为 0。地址改变以后，PEC 的地址输入也要更新。START、STOP、ACK、T-bit 不作为这些字节输入。

本轮使用的包封装、PEC 和通知流程对应 [DSP0233 v1.0.1 §5.2–§5.4](https://www.dmtf.org/sites/default/files/standards/documents/DSP0233_1.0.1.pdf)。实现细节仍以固定代码版本和本轮参数为准。

这里有三种不同校验，后面看日志时要分清：I3C SDR 的数据校验与结束机制、binding 的 CRC-8 PEC，以及 PLDM GetVersion 响应中版本列表的 CRC32。某一种通过，不能替代其他层。

### 5.3 IBI 是通知，响应由后续读取运走

Target 必须先把响应放进可供硬件读取的稳定 buffer，再提交 `MDB=0xAE` 的 IBI。HOST 接受通知后可以很快发起 Private Read。

因此，待发包的生命周期至少要区分：

```text
QUEUED → PREPARED → 等待 IBI / HOST Read → COMPLETE → 出队
```

IBI 被 ACK 时，包还可能没有读走。短读或异常结束时，本轮适配保留队头，清理硬件残留后从包头重新准备；重试节奏和 IBI 使能状态由平台管理。

## 6. Target 独立移植包：先交付组件，不接入原 baremetal

检查过的旧 Target 源码已有 I3C 驱动和回归入口，没有找到可直接使用的 MCTP/PLDM 管理服务。原 full run 的测试握手也不能充当标准管理协议。

这一阶段明确了一个边界：**新增代码先作为独立源码包交付，不加入现有 baremetal。** 后续芯片侧集成由 Target 维护者完成。

### 6.1 固定依赖与实际构建范围

| 组件 | 本轮固定版本 | 使用范围 |
|---|---|---|
| `libmctp` | `ab58506f08dbda562fccaa66340faad9c2e96fa4` | 核心、Control、分包重组及必要运行支撑 |
| `libpldm` | `v0.13.0`，`af271b8247d22935c062cacd578d74cc35b6ec45` | 所需 PLDM Base codec |
| 适配入口 | `src/chip_endpoint.c` | 单实例端点、I3C TX 队列、Base 命令处理 |
| 平台契约 | `include/chip_endpoint.h` | 初始化、RX、TX 准备及完成接口 |
| 本包兼容层 | `compat/` | 字节序、类型、ABI 宏、所需 libc 适配 |

库版本 `v0.13.0` 和对外报告的 PLDM Base 版本 `1.1.0` 是不同编号。

首版端点的配置是：单总线、最大重组消息 512B、两个重组上下文，16 个 TX 槽位。每个槽位保存最多 68B 的 MCTP 包副本；PEC 到实际准备发送时追加。

PLDM 部分只报告真实实现的 Base 能力：SetTID、GetTID、GetPLDMVersion、GetPLDMTypes、GetPLDMCommands。传感器、PDR、FRU、固件更新、SPDM 和完整 Discovery Notify 流程没有在这个包里完成。

### 6.2 六个入口怎样连接平台

| 接口 | 调用时机 | 关键契约 |
|---|---|---|
| `chip_endpoint_init()` | 主循环启动管理模式时 | 提供初始 EID、TID、单调毫秒计时；分配器可用 |
| `chip_endpoint_rx()` | 一次完整 Private Write 接收后 | 传实际数据长度，含 PEC，不含地址字节 |
| `chip_endpoint_tx_pending()` | 主循环检查待发响应 | 反映软件队列，不能代替硬件完成状态 |
| `chip_endpoint_tx_peek()` | 准备硬件 TX 之前 | 复制队头并追加本次 Read PEC；buffer 持续有效 |
| `chip_endpoint_tx_complete()` | Private Read 确认结束后 | 用实际长度和正常结束结果判定是否出队 |
| `chip_endpoint_deinit()` | 退出模式或复位重建时 | 平台先停止 IRQ、DMA 和硬件访问，再销毁实例 |

有一个容易忽略的返回值：若上一条响应还没发完，`chip_endpoint_rx()` 可以返回 `-EBUSY`。这只是软件 API 的结果，不能自动让已经发生的总线传输 NACK。平台需要在事件队列里保留已接收数据，并安排后续处理。

### 6.3 移植中真正遇到的几个问题

这部分没有一开始就编译通过。

**第一处是上游目录布局。** 按旧路径找 `src/base.c` 失败，固定版本的实现实际位于 `src/dsp/base.c`。最终按已固定源码调整构建输入，没有靠补一个同名空文件绕过。

**第二处是运行库差异。** 原生编译暴露了 `memmem` 声明问题；玄铁 newlib 交叉编译又缺少 `uchar.h`。兼容层局部提供本次路径所需的实现或类型，同时处理内部 errno、字节序和 ABI 标记。这里只满足已选模块，不能据此宣称实现了完整 C11 字符转换接口。兼容 include 也不能全局覆盖原 baremetal 的头文件。

**第三处是 binding 的发送存储。** 最初运行触发：

```text
mctp_register_bus: Assertion `binding->tx_storage' failed
```

代码随后补齐对齐的静态 `tx_storage`，在注册 bus 前设置 `binding.tx_storage`。协议库初始化成功，才有后续收发测试的意义。

**第四处是 RX packet 所有权。** 内存检查曾报告：

```text
AddressSanitizer: 1188 byte(s) leaked in 11 allocation(s)
```

这次外层远程命令虽然正常结束，内部记录却是 `SANITIZERS_EXIT=2`。需要沿着测试命令本身的返回值判断结果，不能把外层 shell 的返回 0 当成内存检查通过。

每次接收分配了 packet，按头文件注释理解为交给核心释放，但该固定版本的 `mctp_bus_rx()` 实际借用它。适配层在同步调用返回后补上释放，再重跑 ASan/UBSan：

```c
mctp_bus_rx(&ep.binding, pkt);
mctp_pktbuf_free(pkt);
```

TX 方向也有对应问题：上游 TX callback 返回以后，原 packet 会被释放。因而 `binding_tx()` 必须复制一份进自己的队列，不能把原 packet 指针留给稍后才发生的 IBI/Private Read。

这些结论针对固定版本。将来升级库时，需要重新审查所有权，不能无条件沿用“调用完就 free”。

### 6.4 当时验收到了哪里

原生内存协议测试、ASan/UBSan 检查和玄铁 RISC-V 静态库编译最终通过。工具链记录为 Xuantie V3.4.0、GCC 14.3.0、newlib。

```bash
make test
make all CC=riscv64-unknown-elf-gcc AR=riscv64-unknown-elf-ar
```

测试覆盖 PEC 已知向量、MCTP Get/SetEID、Base 命令、版本 CRC32、错误类型/版本、短读保留、69B 首包重组以及端点重建。

得到的 `libchip_endpoint.a` 是组件静态库，没有完成最终芯片 ELF/BIN 的链接和加载。交叉编译采用工具链默认架构/ABI，实际接入时仍要沿用目标固件的 `-march/-mabi`。这个包也没有配置真实 I3C HAL、IRQ 或 DMA。

## 7. Target 能力记录有一次重要更新

早期依据旧源码和回归入口，曾把普通 Target PIO 的 16B 限制列为移植约束。19:38，Target 维护者明确确认：**最新版本已取消这个限制，最新源码不再对本轮 HOST 工作区开放。**

后续设计据此更新，不能继续把旧 16B 限制当成现状。与此同时，信息来源要分开：

| 信息 | 来源与边界 |
|---|---|
| 普通 PIO 的 16B 限制已取消 | Target 维护者确认；未在最新源码中复查 |
| MRL/MWL 宣告 260B | 本轮枚举 CCC 日志可见 |
| 一笔 69B 包可完整处理 | 本轮仍需双端验证，声明上限不能替代收发结果 |
| Target 最新初始化与 RX arm 实现 | 当前不可访问，不能从旧代码推断 |
| 历史 IBI、长包、恢复能力 | 旧 I3C 回归证据，不能直接记为当前 MCTP 模式通过 |

历史上一次 69B Write 的 HOST `ret=0`，没有配套确认 Target 收包与 PEC 的完整证据，也不能补写成“MCTP 基线已通过”。

## 8. HOST 缺的不是一个开关：slim 树里裁掉了协议代码

当时运行的是 `6.6.36-slim-moduledev`。检查发现，精简源码中的 `net/mctp`、`drivers/net/mctp` 已被裁掉；留下相关头文件，并不能提供 socket 或 transport 功能。

I3C transport 选择回移 Linux v6.8 的实现，并适配当前 6.6.36 平台。MCTP 核心从与 slim 平台配套的 donor 恢复，同时补上 Kconfig/Kbuild 和 I3C bus 生命周期接口。

本轮工作隔离在独立分支和工作区，原已验证 Controller 路径尽量保持：

```text
工作区：worktrees/imx93-slim-mctp-20261125/
分支：codex/mctp-i3c-slim-20261125
平台基准：c58292616eeed014c605d572ae3f58a5c7095b43
MCTP core donor：8cbce8635c4ece729bba5d46c3e6cd26d1b3c47d
I3C transport 来源：Linux v6.8
```

以上是本轮实验版本记录。移植差异当时没有提交、合入 master，不能把它写成已从 master 重建并回归的正式基线。

### 8.1 内核配置与配套模块

关键配置为：

```text
CONFIG_LOCALVERSION="-slim-mctp"
CONFIG_MCTP=y
CONFIG_I3C=m
CONFIG_SVC_I3C_MASTER=m
CONFIG_MCTP_TRANSPORT_I3C=m
# CONFIG_INTELLIF_TARGET_RECOVERY is not set
```

新 release 为 `6.6.36-slim-mctp`。`CONFIG_MCTP` 在本次配置中内建，部署需要新 Image；不能只把 `mctp-i3c.ko` 插进原来的精简内核。

三个配套模块是 `i3c.ko`、`svc-i3c-master.ko`、`mctp-i3c.ko`，均经过完整 modpost，并核对 vermagic。原内核的符号版本或 release 不匹配时，不能靠修改文本伪装兼容。

### 8.2 为什么要单独做 mctp-lab profile

现有低速实验环境采用约 2kHz 的 PURE I3C 配置，有特定 SPI/GPIO 依赖限制。不能为了当前实验，改变同事使用的产品默认配置。

因此新增独立 `mctp-lab` profile 和 MCTP DTS，只有专用 DTB 增加 `mctp-controller`。原产品 DTB、原 lab DTS 和原 `module-debug.config` 保留；检查分别覆盖产品 SPI/GPIO 依赖以及实验频率配置。

构建入口以包含 `linux/` 的工作区根目录为基准：

```bash
./linux/build.sh --profile mctp-lab \
    --out ../imx93-mctp-out/kernel -j 8 all

bash linux/vendor/intellif/tools/build-mctp-host.sh \
    ../imx93-mctp-out/kernel ../imx93-mctp-out/package

(cd ../imx93-mctp-out/package/bundle && sha256sum -c SHA256SUMS)
```

这里的输出目录是说明构建入口的相对路径示例，本轮实际产物归档在开发工作区的 `build/mctp-slim-20261125/`。构建、打包本身不写启动介质，也不产生 I3C 事务。

## 9. 回移的关键适配：实际长度、IBI 和设备生命周期

### 9.1 请求长度不能拿来校验短响应

当前厂商 I3C API 保留 `i3c_priv_xfer.len` 为请求上限，把实际读取长度放进 `actual_len`。MCTP binding 必须沿用这个契约。

例如，HOST 允许读取最多 69B，Target 的 GetEID 响应实际只有 12B。若仍拿 69B 去定位末尾 PEC，就会把没收到的数据当作包内容，丢掉本来可能正确的响应。

本轮 `mctp_i3c_read()` 在 Private Read 完成后使用：

```c
xfer.len = xfer.actual_len;
```

随后检查是否越过申请容量、是否小于最小包长，按这个实际长度计算 PEC，最后裁剪 skb。

枚举时也看到了实际长度的重要性：GETCAPS 请求上限为 4，实收 3B，日志保留 `01 f1 18`，没有把第四字节假装成已接收。

### 9.2 绑定前检查真实能力

本轮 setup 检查 BCR 的 IBI/MDB 能力，以及 MRL、MWL 是否至少覆盖 69B。检查通过后申请 IBI slot，再启用 IBI。接收 handler 只把有效的 `0xAE` 通知交给后续 MCTP 读取路径。

这样可以尽早暴露模式不一致：若 Target 仍沿用测试 DCR 或没有正确声明 IBI，就停在绑定阶段，而不是等第一次管理请求再猜。

### 9.3 失败路径和卸载路径也要检查

本轮还处理了 bus notifier、设备加入/移除、`drvdata`、IBI 资源和发送线程的生命周期：

- setup 中途失败，要回收已申请的 IBI，移除链表项并清理 `drvdata`。
- 模块注册失败，要撤销已注册 notifier 和已经建立的 bus 资源。
- 发送查找、设备移除和发送线程停止，不能围绕全局锁互相等待。
- 卸载前要阻止新的设备引用，再等待在途发送并释放资源。

模型测试覆盖 setup 成功、失败、OOM，以及锁和对象清理。它证明这些被测试的代码路径在模型中符合预期，不代表已经做了实板在途卸载压力测试。

## 10. 用户态工具：把请求交给 socket，逐层核对返回

本轮交付两个静态 AArch64 工具：

| 工具 | 职责 |
|---|---|
| `mctp` | 通过 rtnetlink 查询、设置 link、EID、route 和 neighbour |
| `mctp-pldm-request` | 构造单次 Control/Base 请求，等待并核验响应 |

配置工具固定在 CodeConstruct `mctp` 的 `34900256b183811b25ff4046aa9702cef54befc9`；requester 的 Base 编解码使用前述固定 `libpldm`。

### 10.1 请求与响应怎样匹配

requester 的 `build_request()` 构造 Control 或 PLDM Base 内容。收包后，`matches()` 核对 network、源 EID、Message Type、Tag Owner 方向、Instance ID 和 Command；PLDM 还核对 Type。

SetEID 是特殊情况：成功响应可能已使用新源 EID。因此这个命令允许相应的新地址，但首轮查询没有默认执行 SetEID。

随后才检查完成码和响应结构。例如 Base 版本响应还验证 transfer handle、完成标志、版本字节数和 CRC32。收到几字节数据不能直接打印一个“通过”。

### 10.2 构建工具也有一次小返工

原生 selftest 一度已经打印 `SELFTEST_PASS`，整个 make 目标却返回失败。原因在后面的 `mctp help`：工具的帮助输出和退出码没有被测试规则完整接住。

构建规则随后同时收集帮助输出的两个流，并按帮助命令允许的退出结果处理。交叉构建还关闭了当前旧 UAPI 不具备的扩展项，如 `HAVE_STRUCT_MCTP_FQ_ADDR` 和 `HAVE_IFLA_MCTP_PHYS_BINDING`，避免将较新工具的结构体或属性假定带入这份 6.6 环境。

这个问题提醒我，记录命令结果时，要看完整目标的退出状态，而不只截取中间一行 PASS。

### 10.3 三种检查不能混记

| 操作 | 实际访问范围 | 通过意味着什么 |
|---|---|---|
| `--selftest` | 本地内存 | 编码、匹配、解码和校验的样例检查通过 |
| `--socket-check` | 当前运行内核 | 能创建并关闭 `AF_MCTP` socket |
| `control 1 9 get-eid` | 通过实际 binding 进入总线 | 需要真实请求与可匹配响应，才能判定此命令成功 |

selftest 日志里的示例 `tid=42` 是测试数据。Target 首轮配置是 `TID=1`，且今天没有通过真实 GetTID 查询确认它。

## 11. 离线验收与交付：哪些已经有结果

在上板以前完成了以下检查：

| 范围 | 本轮已有结果 | 证据边界 |
|---|---|---|
| 独立裸机端点包 | 原生测试、ASan/UBSan、RISC-V 静态库编译通过 | 没接真实 HAL，没有最终芯片固件验收 |
| HOST 内核 | 新 Image、DTB、三个模块构建通过 | 编译不产生总线事务 |
| 用户态工具 | 原生 selftest、内存检查、静态 AArch64 编译通过 | 不能替代实际端点响应 |
| transport 模型 | 短包/满包、异常长度、PEC、IO 错误、MDB 过滤通过 | 使用模型输入，未证明当前链路实包正确 |
| 生命周期模型 | setup 成功/失败/OOM、资源和锁清理通过 | 未覆盖所有并发和板级时序 |
| 原有 I3C 离线回归 | 12 项返回 0 | 未重新跑一轮完整双端硬件回归 |
| 产品/实验 DTB | 编译及相关属性检查通过 | 产品与低速实验场景分开保存 |

12 项离线回归分别涉及 Controller Read 前置与提前终止、地址槽、GETCAPS 长度、IBI enable/receive、DV 流程、模式节点生命周期、打开与枚举、Private Write、Hot-Join 恢复和 probe。

交付材料分了三类：

1. 裸机端点独立源码包，包含固定库版本、接口和离线验证记录。
2. HOST 移植差异与验证包，以及新的内核、DTB、模块和用户态 bundle。
3. 后来补齐的 HOST 完整离线材料包，约 176MiB，保留源码、工具链、构建依赖、部署与回滚资料。

完整包整理时还遇到了指向外部或已经断开的 `build/product-dtb/source` 符号链接。检查脚本拒绝直接交付这个状态，处理后再次验证了 19,898 个归档文件和 9 个内部符号链接。解压后应能够找到材料，而不是得到一串只在原工作区有效的链接。

这几类包是本地交付材料，本篇没有把完整源码、工具链或芯片固件公开打包上传。

## 12. 上板部署：先留退路，再切换内核

20:00 左右，HOST 仍未部署新内核。20:01 开始实际部署，新 release 的启动、模块加载和总线枚举被拆成不同阶段。

### 12.1 启动分区空间不足

部署前启动分区只剩约 1.4MiB，新 Image 无法直接放进去。先把旧的 `Image.backup-6.18.2` 完整迁移到本轮部署目录的 `boot-archive/`，核对迁移前后 SHA256，没有直接删除备份。

同时保存当时正在运行的 Image、DTB、内核配置、启动参数、Boot ID 和相关产物指纹。旧模块、工具目录保持可用，新组合使用独立部署目录。

### 12.2 切换过程中的检查

安装脚本先检查：旧 release 和 Boot ID 是否匹配，启动介质是否是预期分区，是否还有活动 I3C 测试，候选包和备份清单是否校验通过。

随后暂存新 Image/DTB，再按阶段切换；如果切换过程本身失败，脚本尝试恢复旧启动文件。新启动还带了五分钟的确认 guard，供无人确认的启动场景恢复旧文件。

实际重启以后检查：

```text
uname -r → 6.6.36-slim-mctp
Boot ID 发生变化
SSH 与以太网可用
运行 Image / DTB 与候选包一致
AF_MCTP_SOCKET_PASS
板端 SELFTEST_PASS
```

确认成功后解除 guard。**本轮没有真正触发一次回滚，不能把回滚机制写成已经实测通过。**

### 12.3 故意停在 Controller 尚未加载的状态

最初只加载 `i3c.ko` 和 `mctp-i3c.ko`，Controller 保持 unbound。这样可以先确认系统和用户态接口正常，等待 Target 管理模式准备好，再发起有线上影响的枚举。

部署记录里的 `svc_controller_loaded=false`、`target_bus_tested=false` 属于这个时间点。后续枚举和首包实验另有记录，不能用这个早期 JSON 把后续动作抹掉；也不能把它改成“部署时就已经全通”。

## 13. Controller 加载后：枚举和 MCTP 绑定通过

21:11，Target 维护者确认就绪。HOST 随后加载 Controller，执行本轮枚举和绑定。

在部署 bundle 目录中，实际关键命令为：

```bash
insmod ./svc-i3c-master.ko
./mctp link show
ls -l /sys/bus/i3c/devices/

DEV=/sys/bus/i3c/devices/0-beee00000101
cat "$DEV/pid"
cat "$DEV/bcr"
cat "$DEV/dcr"
cat "$DEV/dynamic_address"
readlink "$DEV/driver"
```

`insmod` 会进入 Controller probe，建立 bus、执行现有枚举序列和身份/能力查询。之后 I3C 设备匹配 `mctp-i3c`，setup 申请并启用 IBI；本轮日志可见 ENEC 成功。后面的 sysfs 和 link 查询是本地观察，不是再发一轮管理请求。

### 13.1 本轮 HOST 实测值

| 项目 | 记录 |
|---|---|
| release | `6.6.36-slim-mctp` |
| PID | `beee00000101` |
| BCR / DCR | `0x3E / 0xCC` |
| DA | `0x44` |
| GETMRL | `01 04 01`：声明 MRL=260，IBI 长度字段=1 |
| GETMWL | `01 04`：声明 MWL=260 |
| GETCAPS | 请求上限 4B，实收 3B：`01 f1 18` |
| 设备绑定 | `mctp-i3c` |
| IBI 配置 | 申请、ENEC 启用成功；尚未验收真实 `0xAE` 通知 |
| MCTP 接口 | `mctpi3c0`，MTU=68，刚建立时为 down |

当时的关键日志节选：

```text
I3C GETCAPS da=0x44 max=4 actual=3 bytes=01f118
I3C ENUM identity OK da=0x44 PID=beee00000101 BCR=3e DCR=cc
I3C IBI enable addr=0x44 bcr=0x3e rules=0x4
I3C CCC seq=8 ENEC(0x80) OK rc=0
```

这些是 Controller 的日志与 sysfs 结果。`hw_ack=ACK` 也是驱动根据硬件状态作出的判断，并非逻辑分析仪对线上每一位的采样结论。

### 13.2 Target 回传了什么

操作员提供的状态截图及会话中的解读记录显示，Target 使用 `EID=9`、`TID=1`，`pec_hw=0`、`pec_sw=1`，收发、待发、溢出等计数初始为零，服务等待接收。

本篇可以引用这份截图回传记录，但没有取得最新 Target 源码和完整硬件寄存器快照。因此，软件显示“等待接收”只说明软件状态，不能代替 DA_VALID、地址比较、Enable 和 RX 硬件资源已经全部就绪的证明。

## 14. 首笔 GetEID：本地配置正确建立，异步发送失败

21:20，在明确允许发送之后，HOST 配置本地通信路径，再只发一笔查询。

### 14.1 实际配置顺序

以下以本轮 HOST 部署 bundle 为工作目录，使用具有网络配置权限的 shell：

```bash
./mctp link set mctpi3c0 network 1 mtu 68 up
./mctp address add 8 dev mctpi3c0
./mctp route add 9 via mctpi3c0
./mctp neigh add 9 dev mctpi3c0 lladdr be:ee:00:00:01:01

MCTP_TIMEOUT_MS=30000 ./mctp-pldm-request control 1 9 get-eid
```

前四条修改本地网络表项；最后一条才构造并提交管理请求。现场已经存在这些表项时不能照抄重复 add，应先查看现状。

### 14.2 每条命令到底做了什么

| 命令 | 内部作用 | 是否发 I3C 管理请求 | 本轮结果 |
|---|---|---|---|
| `--socket-check` | 创建、关闭 AF_MCTP socket | 否 | 通过 |
| `link set ... up` | 配置 network、MTU、netdev 状态 | 否，本命令修改本地配置 | 接口 up |
| `address add 8` | 增加 HOST 本地 EID | 否 | EID=8 |
| `route add 9` | 建立目标 EID 路由 | 否 | 路由可见 |
| `neigh add 9 ...` | EID 对应 PID 邻居项 | 否 | 映射可见 |
| `control 1 9 get-eid` | 用户态构包，进入 AF_MCTP、路由、binding 和 SVC | 是，目标是 `0x44/W` Private Write | 地址 NACK；等待后超时 |
| `link show`、sysfs、计数、dmesg | 读取本地状态和已有日志 | 否 | 用于收尾保留现场 |

### 14.3 真正的发送链

```text
mctp-pldm-request: build_request()
  → sendto(AF_MCTP)
  → net/mctp：寻址、Tag、4B 头和输出
  → mctp-i3c 的发送队列 / TX 线程
  → mctp_i3c_xmit()：PID 找设备、追加 PEC
  → i3c_device_do_priv_xfers()
  → svc_i3c_master_priv_xfers()
  → svc_i3c_master_xfer()
```

socket 接受请求时，I3C 发送仍可能在后面的内核线程里失败。应用没有立刻得到一个可匹配响应，最终按 30 秒等待上限退出。这个过程不能简写成“应用发送成功，所以 Target 已经收到”。

### 14.4 首包失败的原始证据

```text
I3C XFER FAIL rc=-6 recovery_required=0
I3C XFER cmd=1 addr=0x44/W stage=address-nack rc=-6 stop_rc=0
status=0x9223 err=0x4 stop_status=0x1200 stop_err=0x0
```

用户态结果：

```text
response timeout
REQUEST_EXIT=3
```

随后读取的 netdev 计数：

```text
tx_packets=0
tx_errors=1
rx_packets=0
rx_crc_errors=0
rx_length_errors=0
```

`rc=-6` 对应 `-ENXIO`。SVC 代码在完成地址阶段等待后读取 `MERRWARN`，发现 NACK 位便进入这个错误出口；本轮没有进入成功的数据写入路径。STOP 返回 0，日志中的 `recovery_required` 为 0。

这里的 `rx_crc_errors=0` 只表示没有记录到 RX PEC 错误。`rx_packets` 也是 0，根本没有返回包可供验收，不能写成“PEC 通过”。

本次只发送一次，没有自动重发、重新枚举、卸载驱动或复位 Target，现场保持下来。

## 15. 对照寄存器路径：这次错误发生在数据之前

SVC 的 `svc_i3c_master_xfer()` 提供了一个清楚的阅读入口。下面只记录本轮与失败定位直接相关的操作；完整电气时序仍需要控制器资料和外部波形核对。

| 顺序 | 源码中的访问或判断 | 为什么与本轮有关 | 当前证据 |
|---|---|---|---|
| 1 | 向 `MSTATUS` 写 IBIWON 清除位 | 清理该路径中的旧事件；属于带写清语义的状态操作 | 源码可见 |
| 2 | 写 `MCTRL`：START/地址、transfer type、方向、地址和 RDTERM | 让 Controller 开始本次地址阶段 | 源码与本轮 `0x44/W` 日志对应 |
| 3 | 等待 `MCTRLDONE` | 先判断地址控制阶段有没有结束 | 源码可见；本轮进入地址结果判断 |
| 4 | 读取 `MERRWARN`，检测 `NACK` | 该宏为 bit 2，`err=0x4` 命中 | 源码判定与日志一致 |
| 5 | 设置 `stage=address-nack`，返回 `-ENXIO`，转 STOP | 后续 payload 写入被跳过 | 本轮日志直接可见 |
| 6 | STOP 和恢复状态记录 | 判断失败后 HOST 是否回到本路径可接受的结束状态 | `stop_rc=0`；不代替 Target 内部状态确认 |

源码中 `MCTRL.ADDR` 使用 bits 15:9，`RDTERM` 使用 bits 23:16。这里列的是这份驱动的字段定义和处理流程，没有凭寄存器名补写一份新的硬件手册解释，也没有解码 `0x9223` 中所有未用于本次判断的位。

对比 MCTP 写路径和既有 `i3c-host` 普通写路径，最终都进入同一套 SVC Private Transfer 实现。静态检查没有发现 MCTP 独有的地址发送实现。**这个结果缩小了差异范围，但不能证明两次运行时的使能、队列、广播头和总线状态完全一致。**

## 16. Target 没有收包：现在能确认什么，不能确认什么

失败后，操作员再次回传 Target 状态。会话中记录为：

```text
active=1  phase=2  error=0  wait_enec=0
DA=0x44
rx_packets=0  trace_count=0  irq_count=0
ibi_submitted=0
```

这和 HOST 的地址 NACK 相容：没有成功收进第一条请求，也没有响应包需要通知。此时 IBI 计数为 0，没有提供“IBI 发送失败”的证据。

<pre class="mermaid">
flowchart TD
  A["新内核和 AF_MCTP 已确认"] --> B["枚举、mctp-i3c 绑定和 ENEC 通过"]
  B --> C["本地 EID、路由、PID 邻居配置"]
  C --> D["首笔 GetEID · 0x44/W"]
  D --> E["地址阶段 NACK · 当前停点"]
  E --> F["保存双端状态，不自动重发"]
  D -.-> G["请求 payload / PEC · 未通过"]
  G -.-> H["Control 处理 / 响应准备 · 未验证"]
  H -.-> I["IBI 0xAE / Private Read · 未验证"]
  I -.-> J["PLDM Base / 真实业务 · 未验证"]
</pre>

下一步应先核对同一现场中的 Target 硬件条件：

1. 软件缓存的 DA 是否和硬件 DA、有效位一致。
2. Target Enable、地址匹配和协议模式是否仍有效。
3. HALT、禁用、主动 NACK 或错误状态是否阻止接收。
4. RX buffer/FIFO/Response 资源是否真正准备好，是否由管理服务独占。
5. 从枚举、ENEC 到首笔 Private Write 之间，Target 是否发生过复位或状态切换。
6. 用总线采样核对地址、方向、START/Repeated START/STOP，以及 NACK 的实际发生位置。

这些是待核对项，不是已经确定的根因。今天没有最新 Target 源码、完整硬件快照或外部总线抓波，所以不能进一步宣布是 RX arm、地址比较器、PEC 或某个初始化顺序的错误。

## 17. 把第一条 Get Endpoint ID 拆成字节

后面继续调试时，需要清楚知道原本准备传什么。**以下是构包结构和确定条件下的算例，不能当作本轮已成功写到 Target 的 payload。**

Get Endpoint ID 属于 MCTP Control。它没有进入 PLDM，也不是 I3C CCC。

### 17.1 请求的数据区为 8B

设 Message Tag 为 `T`，Control Instance ID 为 `I`：

```text
地址阶段：0x44 / Write → 地址/RW 字节 0x88

I3C 数据区：
01 09 08 [C8|T] 00 [80|I] 02 [PEC]
```

| 数据偏移 | 字节 | 含义 |
|---:|---|---|
| 0 | `01` | MCTP Header Version=1，相关保留位为零 |
| 1 | `09` | Destination EID=9 |
| 2 | `08` | Source EID=8 |
| 3 | `C8 \| T` | SOM=1、EOM=1、Seq=0、TO=1，加三位 Tag |
| 4 | `00` | IC=0，Message Type=MCTP Control |
| 5 | `80 \| I` | Request=1、Datagram=0，加五位 Instance ID |
| 6 | `02` | Get Endpoint ID 命令，无附加请求参数 |
| 7 | 计算结果 | I3C binding 的 PEC |

`0x88` 属于寻址阶段，不会成为 Target RX buffer 中的第一个业务字节。Tag 和 Instance ID 也要分别沿用：响应要对应 MCTP 交互，同时对应上层 Control 请求。

字段布局及 GetEID 的命令定义见 [DSP0236 v1.3.3，MCTP header、Control header 与 §12.5 / Table 15](https://www.dmtf.org/sites/default/files/standards/documents/DSP0236_1.3.3.pdf)。

### 17.2 一个能手算复核的 PEC 例子

假设 `Tag=0`、`Instance ID=0`，CRC 输入是：

```text
88 01 09 08 C8 00 80 02
```

可以用下面的独立脚本复算，不连接总线：

```python
def pec8(data):
    crc = 0
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ (0x07 if crc & 0x80 else 0)) & 0xff
    return crc

request_with_address = bytes.fromhex("88 01 09 08 C8 00 80 02")
assert pec8(request_with_address) == 0x25
print(f"PEC={pec8(request_with_address):02X}")
```

计算结果为 `0x25`。于是这个条件算例的数据区为：

```text
01 09 08 C8 00 80 02 25
```

本轮日志没有记录实际 Tag 和 Instance ID，因此不能把这串字节标注成实测发送内容。

### 17.3 预期响应与长度口径

在成功处理并采用相应返回字段的情况下，响应应交换源/目的 EID、清除 TO 和 Request，沿用相应 Tag/Instance ID：

```text
01 08 09 [C0|T] 00 [I] 02 00 09 [EndpointType] [MediumInfo] [PEC]
```

上述响应共 12B，包含 PEC。HOST 去掉 binding PEC 和 MCTP 头后，requester 看到的 Control 消息是 7B；当前工具按这个结构检查 GetEID 响应。

Target 正常处理请求时则先校验 8B 数据区末尾的 PEC，去除后得到 7B MCTP 包，再解出 Control 消息。不能把地址字节、PEC 或 MCTP transport header 重复交给上层。

### 17.4 一次正常交互应当出现的动作

<pre class="mermaid">
sequenceDiagram
  participant U as HOST requester
  participant H as HOST 内核与Controller
  participant T as Target管理服务
  U->>H: GetEID消息，目的EID=9
  H->>T: Private Write，请求包与PEC
  Note over H,T: 此处为设计预期，本轮停在写地址NACK
  T->>T: 校验并处理Control，准备响应
  T-->>H: IBI，MDB=0xAE
  H->>T: Private Read
  T-->>H: 实际长度的响应包与PEC
  H-->>U: 可匹配的Control响应
</pre>

## 18. 今天的验收矩阵

将每个阶段单独列出来，比一句“完成移植”更有用。

| 项目 | 截止本轮的状态 | 不能据此外推的结论 |
|---|---|---|
| 协议与硬件分层 | 已确定 | 不代表真实管理业务已实现 |
| 独立裸机端点包 | 已交付，离线和静态库编译通过 | 不代表最新 Target 固件就是这份包的原样构建 |
| 最新 Target 能力 | 有操作员确认和状态回传 | 没有最新源码级审查 |
| slim MCTP 核心及 transport | 代码、构建、离线检查通过 | 没有完整规范符合性结论 |
| 新内核部署 | 重启、SSH、指纹和本地接口通过 | 回滚路径未实触发 |
| I3C 枚举及身份 | HOST 日志通过 | 不保证后续 Private Write 条件仍成立 |
| MCTP 驱动绑定及 netdev | 通过 | 不代表端点请求响应通过 |
| IBI 申请和 ENEC | 通过 | 未验收真实 `0xAE` 通知和读取 |
| EID、路由、邻居 | 本地配置完成 | 未完成自动端点管理 |
| 首笔 GetEID | **失败：地址 NACK** | 无法评价 payload、PEC 和响应处理 |
| PLDM Base | 未执行真实端到端查询 | selftest 的 TID/版本数据不能代替实测 |
| 真实 Sensor/PDR | 未接入 | 无温度、电压等业务成功记录 |
| 连续、坏 PEC、复位恢复、性能 | 当前 MCTP 组合未验收 | 历史 I3C PASS 不自动继承 |

## 19. 接下来怎样继续，才不会把问题越调越散

第一步仍是保留当前地址 NACK 的现场，核对 Target 硬件状态与线上寻址。拿到新的证据后，再决定是否重新初始化、重新枚举或做一次受控重试。

等单笔 GetEID 真正通过，再按下面的顺序扩大验证。每一条都要能对齐双端计数和队列状态，前一条失败就停下来。

```bash
# 已执行过但失败的第一条；再次执行前需重新确认现场和Target准备状态
./mctp-pldm-request control 1 9 get-eid

# 以下尚未完成本轮真实端到端验收
./mctp-pldm-request control 1 9 get-version
./mctp-pldm-request control 1 9 get-types
./mctp-pldm-request base 1 9 get-tid
./mctp-pldm-request base 1 9 get-types
./mctp-pldm-request base 1 9 get-version
./mctp-pldm-request base 1 9 get-commands
```

`get-commands` 当前查询 Base 1.1.0；若 Target 报告其他版本，要先改查询参数，不能把不匹配的版本请求解释成链路故障。

基础命令通过后，再做串行连续请求、错误长度/未知命令、坏 PEC、69B 边界、多包重组、短读重发，以及复位和重新加入。普通 AF_MCTP 输出会自动生成 PEC，坏 PEC 用例需要专门注入路径，不能直接复用普通发送命令假装已经制造了错误包。

Target 的日志至少应把一笔请求串起来：RX 实际长度和 PEC 结果、源/目的 EID 与 Tag、上层 Instance ID/Type/Command、响应长度、TX_READY、IBI 提交/结果、TX_DONE。还要有包计数、短读、队列溢出和 pending 状态。

最终想得到的第一个小闭环很具体：Target 管理服务启动一次，HOST 连续完成 Control 和 Base 查询，双方能对应每次请求与响应，待发队列最后归零，下一次请求仍然可用。

## 20. 时间线：今天的决定和停点

以下按会话时间记录，时区为 Asia/Shanghai。板端日志可能使用不同的系统时钟，跨材料关联主要依赖 release、运行目录、Boot ID、产物指纹和阶段日志，不能只靠一个墙上时间。

| 时间 | 推进内容 | 留下的决定或结果 |
|---|---|---|
| 15:02–15:16 | 从直觉解释到协议和开源选型 | 先讲清 MCTP/PLDM 职责，再落到工程接口 |
| 15:25 | 建立硬件模型 | i.MX93 Linux 管理裸机 Target |
| 15:27 | 首版选择 I3C | 单 Controller、单 Target、SDR、IBI 通知 |
| 15:29–15:36 | 查历史回归和现有源码 | 复用底层；新增绑定、封装和协议服务 |
| 15:39–15:52 | 交付独立 Target 组件 | 不接入原 baremetal；修复构建和所有权问题，完成离线验证 |
| 17:47 | 划分管理链与采集链 | 本地 PMBus/MMIO 等业务后续接入 |
| 19:17–19:20 | 确定 HOST 用户态接口并开始 slim 回移 | AF_MCTP 在内核，PLDM 在用户态 |
| 19:36–19:38 | 更新 Target 能力边界 | 不再沿用旧 16B 限制；最新源码不可访问 |
| 19:50 | 约定服务契约和验证步骤 | 固定 EID；先 Control，再 Base |
| 20:00–20:15 | 部署新内核、检查并整理交付 | `6.6.36-slim-mctp` 启动；Controller 暂不加载；完整材料归档 |
| 20:22 | 明确逐阶段命令和停止条件 | 操作前确认 Target 就绪；失败不自动重发 |
| 21:11 | 加载 Controller、枚举和绑定 | DA=0x44，DCR=0xCC，`mctpi3c0` 建立 |
| 21:15 | 核对 Target 服务状态 | EID/TID、软件 PEC 和初始队列状态回传 |
| 21:20–21:22 | 首笔 GetEID | 地址 NACK；requester 等待超时，现场保留 |
| 21:29 | 核对失败后的 Target 状态 | 接收和 IRQ/trace 计数为零，根因未定 |
| 21:40–21:42 | 拆解请求包 | 明确 8B 请求、12B 条件响应和 PEC 算例，避免把理论包当作已发送字节 |

## 21. 源码入口、版本与证据索引

### 21.1 从哪里继续读代码

以下路径以各自工作区或组件包根目录为基准。表中只索引本篇追踪的协议入口和失败路径，不是整棵 Linux 源码的完整审计。

| 主题 | 路径及符号 | 读代码时要回答的问题 |
|---|---|---|
| Target 入口契约 | `include/chip_endpoint.h` | 实际长度、调用上下文、缓冲区何时失效？ |
| Target binding | `src/chip_endpoint.c`：`binding_tx`、`chip_endpoint_rx`、`chip_endpoint_tx_peek/complete` | 谁复制包，谁算 PEC，谁决定出队？ |
| Target 分发 | 同文件 `on_message` | Control 和 PLDM 分别交给谁，能力位图是否真实？ |
| Target 核心所有权 | `vendor/libmctp/core.c`：`mctp_bus_rx` | 固定版本到底借用还是接管 packet？ |
| HOST requester | `linux/vendor/intellif/tools/mctp-host/mctp-pldm-request.c`：`build_request`、`matches`、`print_response` | 请求怎样编码，返回包怎样关联，退出码代表什么？ |
| MCTP 核心 | `linux/net/mctp/af_mctp.c`、`route.c` | socket 消息怎样进入路由，Tag 和 transport header 从哪里来？ |
| I3C binding | `linux/drivers/net/mctp/mctp-i3c.c`：`setup`、`read`、`xmit`、IBI handler、设备/总线清理 | 69B 上限、实际长度、PEC、PID 寻址和生命周期怎样实现？ |
| I3C 调用边界 | `linux/drivers/i3c/master.c`：`i3c_device_do_priv_xfers` | binding 怎样进入 Controller，`actual_len` 怎样回传？ |
| HOST 地址失败 | `linux/drivers/i3c/master/svc-i3c-master.c`：`svc_i3c_master_priv_xfers`、`svc_i3c_master_xfer` | 哪一次状态读取把本笔判成 NACK，payload 是否执行？ |
| 构建隔离 | `linux/build.sh`、`linux/vendor/intellif/configs/mctp-lab.config`、独立 MCTP DTS | 产品配置与低速实验配置是否分开？ |

### 21.2 版本记录不能只留一个分支名

本轮 HOST 平台基准为 `c58292616eeed014c605d572ae3f58a5c7095b43`，实验分支为 `codex/mctp-i3c-slim-20261125`。移植差异当时未提交，归档 patch 的 SHA256 为：

```text
944ee818506c26e903381dcfbb6dbd9a678d942edd1ea104677d92f36a613430
```

实际部署的新 Image 与 DTB 指纹为：

```text
Image:
b261ad2a2af758325d14f00a97850ea24abeff6ce305178063ba68a5c128842f

MCTP DTB:
60fd181073b3081440b3c70469578d0fe55e9b16c83a66314c65b81958fe3fe6
```

Target 最新版本没有可访问的源码指纹，不能把历史 baremetal commit 补到本轮当前固件名下。后续想形成正式可复现基线，还要补齐双方实际版本、已合入 master 的提交、匹配构建与双端回归材料。

### 21.3 本地证据怎样对上

| 材料 | 相对开发工作区的归档位置 | 使用时注意 |
|---|---|---|
| 移植与离线检查 | `worktrees/imx93-slim-mctp-20261125/evidence/mctp/` | `RESULT.md` 保留了部署前状态，先看其时间点 |
| 部署 | `build/mctp-slim-20261125/deploy-run01/` | `DEPLOYED.md`、确认记录、运行配置和 DTB；并非端到端成功记录 |
| 枚举 | `build/mctp-slim-20261125/validation/enumeration-Nv9954/` | 原始内核日志、sysfs、接口与结果记录 |
| 首笔失败 | `build/mctp-slim-20261125/validation/get-eid-11WZoC/` | 配置、request、退出码、前后内核日志及计数 |
| 完整 HOST 交付件 | `build/mctp-host-complete-20261125/` | 源码、工具链、构建和回滚材料；本地保存 |
| Target 截图回传 | 本轮会话中的状态截图及解释 | 本篇没有附上 Windows 侧原图，不补写不可见字段 |

文章写作阶段只读取这些材料、核对源码和整理协议算例，没有为补充记录而重新操作开发板、重发首包或修改驱动。上述硬件结果均归属于今天原来那一轮执行。

今天先把这个里程碑停在这里：系统和枚举基础已经搭好，首笔管理请求卡在地址应答。下一轮先解释清楚这个 NACK，再沿着请求接收、响应准备、IBI 和读取，一段一段把链路接完。

## 参考与延伸阅读

- [DMTF DSP0233 v1.0.1：MCTP I3C Transport Binding](https://www.dmtf.org/sites/default/files/standards/documents/DSP0233_1.0.1.pdf)，包封装、IBI 通知和 PEC 参见 §5.2–§5.4。
- [DMTF DSP0236 v1.3.3：MCTP Base](https://www.dmtf.org/sites/default/files/standards/documents/DSP0236_1.3.3.pdf)，Get Endpoint ID 参见 §12.5 / Table 15。
- [Linux MCTP 用户接口](https://docs.kernel.org/networking/mctp.html)。
- [OpenBMC libmctp](https://github.com/openbmc/libmctp)、[OpenBMC libpldm](https://github.com/openbmc/libpldm)。本轮固定版本见正文，不用当前仓库首页代替版本记录。
- [I3C 控制器学习手记](/2026/11/25/i3c-controller-guide/)、[Linux I3C 驱动视角学习笔记](/2026/11/25/i3c-linux-driver/)。
- [本轮关键证据摘录](/2026/11/25/mctp-pldm-i3c-milestone-20261125/evidence.txt)。
