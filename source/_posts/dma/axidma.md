---
title: axidma硬件介绍与软件实现
date: 2026-03-17
toc: true
max_depth: 3 
mermaid: true
categories: life_record
tags: [life]
---

## 通用特性

核心时钟与通道配置​
独立的核心、从接口和主接口时钟​
Independent core, slave interface and master interface clocks
最多支持 32 个通道​
Up to 32 channels, one per source and destination pair, configurable in coreConsultant
数据传输单向性​
Data transfers in one direction only (each channel is unidirectional)
最多两个 AXI 主接口​
Up to two AXI master interfaces, configurable in coreConsultant
用于多层支持（Two master interfaces for multilayer support）
多个 AXI 主接口通过允许外设直接连接不同 AXI 互连，提升总线性能（Multiple AXI masters increase bus performance by allowing direct connection of peripherals on different AXI interconnects）
支持不同 AMBA 层上的不同 ACLK（Support for different ACLK on different AMBA layers）
DMA 传输类型与内存接口​
支持多种 DMA 传输模式​
Memory-to-memory, memory-to-peripheral, peripheral-to-memory, and peripheral-to-peripheral DMA transfers
独立的外部内存接口（每通道）​
Separate external memory interface (per channel) to connect SRAM or Register File based memories to Channel FIFO
接口标准与配置​
AMBA 3 AXI / AMBA 4 AXI 兼容的主接口​
AMBA 3 AXI/AMBA 4 AXI-compliant master interface
AHB / APB4 从接口用于编程 DMA 控制器​
AHB/APB4 slave interface for programming the DMA controller
仅支持 SINGLE 传输（hburst = 3'b000）（AHB slave interface supports only SINGLE transfers (hburst = 3'b000)）
AXI 主接口数据总线宽度最高 512 位​
AXI master data bus width up to 512 bits (for both AXI master interfaces), configurable in coreConsultant
端序模式可静态或动态选择​       ---> 似乎有点强大，动态静态选择端序模式？
Endian mode can be selected statically or dynamically for AXI master interfaces, configurable in coreConsultant
动态选择端序的输入引脚​
Input pin to dynamically select endianness
高级功能与控制​
主接口链表访问端序的独立控制​
Independent control for endianness of linked list access on master interfaces, configurable in coreConsultant
可选的标识寄存器​
Optional identification register, configurable in coreConsultant
通道锁定支持​
Channel locking support
支持在不同传输层级上锁定主总线接口的内部通道仲裁（Supports locking of the internal channel arbitration for the master bus interface at different transfer hierarchy）
DMAC 状态指示输出​
DMAC status indication outputs
空闲/忙指示（Idle/busy indication）
DMA 保持功能​
DMA hold function       --------> 这个有点意思，重点聊一下
输出引脚指示 DMA 事务级别的最后写传输​
Output pin indicates the last write transfer at DMA transaction level
多级 DMA 传输层级​
Multiple levels of DMA transfer hierarchy


## 1.3.2 通道缓冲
每个通道配备单个先进先出（FIFO）​
FIFO深度可在 coreConsultant中配置
自动打包/解包数据以适配FIFO宽度

## 1.3.3 通道控制
每个通道支持可编程传输类型（内存到内存、内存到外设、外设到内存、外设到外设）
支持单次或多次DMA事务​
每个通道支持可编程的多事务大小​
每个通道支持可编程的最大AMBA突发传输大小（可在 coreConsultant中配置）
通道禁用时无数据丢失​
通道挂起与恢复​
通道优先级可编程​
锁定不同传输层级下主总线接口的内部通道仲裁
**支持通过链表、连续地址、自动重载、影子寄存器方法实现可编程多块传输​
链表动态扩展​**
源（SRC）/目的（DST）多块传输类型独立配置​
每个通道的源和目的各配备一个状态机​
数据和链表项（LLI）访问采用独立状态机​
控制信号（如缓存、保护）可按每个DMA块编程​
传输长度（块长度）可编程​
错误状态寄存器，便于在错误事件中进行调试

## 1.3.4 Flow Control（流控制）
可编程的DMA传输级流控制​
若在DMA初始化前已知块传输大小，DMA控制器作为DMA块传输级的流控制器。
若在DMA初始化前未知块传输大小，源或目标外设作为不定长度（需求模式）DMA块传输的流控制器。

## 1.3.5 Handshaking Interface（握手接口）
面向非内存外设的可编程软件和硬件握手接口​
最多支持**64**个硬件握手接口/外设，可在coreConsultant中配置
**支持启用/禁用单个握手接口**​
**外设与通道之间的可编程映射；支持多对一映射**（同一时间仅一个外设激活）
提供内存映射寄存器，用于在软件握手模式下控制DMA传输
## 1.3.6 Interrupt Outputs（中断输出）
支持合并中断输出和独立中断输出​
中断触发条件包括：
**DMA传输完成（DMA transfer completion）
块传输完成（Block transfer completion）**
单个或多个事务完成（Single or multiple transaction completion）
错误条件（Error condition）
通道挂起或禁用（Channel suspend or disable）
支持中断使能和屏蔽（Interrupt enabling and masking）

以上是文档中“Flow Control”“Handshaking Interface”“Interrupt Outputs”章节的所有功能点，涵盖了流控制策略、握手接口的配置能力、中断输出的类型与触发条件等核心特性。

## 术语
文档中对DMA相关核心术语进行了定义（部分关键术语示例，结合上下文推测完整定义方向）：
Channel (通道)：DMA内用于管理源-目的地数据传输的独立单元，每个通道对应一对源和目标，最多支持32个通道，可配置。
Transaction (事务)：AXI总线上的单次数据传输操作，DMA通过多个事务完成一个块（Block）传输。
Burst (突发传输)：AXI总线中连续传输多个数据的模式，DMA支持配置突发长度（如AHB从接口的hburst = 3’b000表示单事务突发）。
Flow Controller (流控制器)：在DMA块传输中负责控制数据流动的角色——若块大小已知，由DMA控制器自身作为流控制器；若块大小未知（需求模式），由源/目标外设作为流控制器。
Handshaking Interface (握手接口)：用于DMA与外设之间同步数据传输的接口，分为软件握手（通过寄存器配置）和硬件握手（通过引脚交互），最多支持64个硬件握手接口，可配置外设与通道的映射关系。
Interrupt (中断)：DMA完成任务或出现异常时输出的信号，分为合并/独立中断，触发条件包括传输完成、块完成、错误、通道挂起/禁用等，支持中断使能和屏蔽。


文档中对DMA相关核心术语的定义如下（按原文顺序）：
Transaction（事务）​
基本DMA传输单元，由硬件或软件握手接口决定。仅当**传输发生在DW_axi_dmac与非内存外设之间**时，事务才有意义。事务分为两类：
Single transaction（单事务）：长度为1，转换为INCR类型的AXI突发传输（burst length = 1）。
Burst transaction（突发事务）：突发长度由程序配置到DW_axi_dmac中。突发事务会被转换为一系列AXI突发传输，其长度受程序控制，通常与源/目标外设的FIFO大小相关，且属于DW_axi_dmac与源/目标外设之间的传输。
Block（块）​

DW_axi_dmac数据的一个集合，数据量由流控制器（flow controller）决定：
若传输发生在DW_axi_dmac与内存之间：块会被直接拆分为一系列突发事务（burst transactions）。
若传输发生在DW_axi_dmac与非内存外设之间：块会被拆分为一系列DW_axi_dmac事务，这些事务又会被转换为一系列AXI事务。
DMA transfer（DMA传输）​
软件控制DW_axi_dmac传输中的块数量。一旦DMA传输完成，硬件会自动禁用通道并可生成中断以通知“DMA传输完成”。通道随后可被重新配置以执行新的DMA传输。

Single-block DMA transfer（单块DMA传输）：由一个块组成。

**Multi-block DMA transfer**（多块DMA传输）：由多个DMA块组成。多块DMA传输通过以下方式支持：
块链接（linked list chaining，启用LLI时）；
- 自动重加载（auto-reloading）通道寄存器；
- 影子寄存器（shadow registers）；
- 连续块（contiguous blocks）。
源和目标可以独立选择使用哪种方法。

Linked lists (block chaining)（链表/块链接）​
链表指针（LLP）指向系统内存中下一个链表项（LLI）的位置。LLI是一组描述“下一个块（块描述符）”和“下一个LLI”的寄存器。DW_axi_dmac在块链接使能时，会在每个块的开始时获取LLI。LLI访问总是使用突发大小（arsize/awsize），且该大小与数据总线宽度相同（不能更改或编程为其他值）。突发长度（awlen/arlen）可根据数据总线宽度设置，以确保访问不会跨越一个完整的LLI结构（64字节）。若突发长度不受其他设置限制，DW_axi_dmac会将整个LLI（40字节）在一次AXI突发中传输。
Auto-reloading（自动重加载）​
DW_axi_dmac在每个块传输结束时，自动从影子寄存器的内容中重新加载通道寄存器。
Contiguous blocks（连续块）​
当地址选择“后续块是前一个块的延续”时，使用连续块模式。
Shadow register（影子寄存器）​
DW_axi_dmac在当前块传输进行时，自动将通道寄存器加载为“与下一个块传输对应的值”（这些值由软件在影子寄存器中预先编程）。
Channel locking（通道锁定）​
软件可将通道编程为“锁定AXI主接口的仲裁”，持续时间覆盖：
一次DMA传输；
一次块传输；
一次事务（单事务或突发事务）。


## Functions

### 2.13.2 多块传输的 4 种方式

当 DW_axi_dmac 配置为多块传输时，在连续块之间需要重新编程 CHx_SAR（源地址）和 CHx_DAR（目的地址）寄存器。有四种方法可以实现：

#### 1. Contiguous Address（连续地址模式）

**工作原理：**
- 连续块之间的地址选择为前一块结束地址的延续
- 地址自动递增，无需软件干预

**控制方式：**
- 通过 CHx_CTL.SRC_MLTBLK_TYPE 和 CHx_CTL.DST_MLTBLK_TYPE 寄存器字段控制
- 限制：CHx_SAR 和 CHx_DAR 不能同时选择为连续模式

**实现技巧：**
- 如果需要源地址和目的地址都连续，可以使用 Linked List 间接实现
- 设置下一个块描述符的 LLI.CHx_SAR 地址为前一块结束地址 +1
- 设置下一个块描述符的 LLI.CHx_DAR 地址为前一块结束地址 +1

**注意事项：**
如果启用连续地址模式的多块传输，完整的 DMA 传输必须至少有两个块，否则会导致不可预测的行为。


### channel suspend-disable-abort

#### 2.18.1 Channel Suspend（通道挂起）

**挂起通道的步骤：**

| 步骤 | 操作 |
|------|------|
| 1 | 软件向 DMAC_ChEnReg 的 CH_SUSP 位写入 1 |
| 2 | DW_axi_dmac 在完成所有已发起的 AXI 源端传输后，优雅地停止所有来自源端外设的传输 |
| 3 | DW_axi_dmac 将 CHx_IntStatusReg.CH_SRC_SUSPENDED 置 1，表示源数据传输已挂起，并产生中断（如果未屏蔽） |
| 4 | DW_axi_dmac 将通道 FIFO 中的所有数据传输到目的端外设 |
| 5 | DW_axi_dmac 清除通道锁定，并重置 CHx_CFG 寄存器中的通道锁定设置 |
| 6 | DW_axi_dmac 将 CHx_IntStatusReg.ChLock_Cleared 置 1，表示通道锁定已清除 |
| 7 | DW_axi_dmac 将 CHx_IntStatusReg.CH_SUSPENDED 置 1，表示通道已挂起 |
| 8 | DW_axi_dmac 产生 CH_SUSPENDED 中断（如果未屏蔽） |

**注意事项：**
- 如果通道 FIFO 已满且目的端外设未请求数据传输，DW_axi_dmac 无法在相应的主接口上接收更多数据，可能导致死锁
- 在 CHx_CTL.SRC_TR_WIDTH < CHx_CTL.DST_TR_WIDTH 的情况下，挂起时 FIFO 中可能仍有数据但不足以形成单次目的端传输，这些数据会在通道恢复后继续传输

#### 2.18.2 Channel Suspend and Resume（通道挂起与恢复）

**挂起后恢复的步骤：**

| 步骤 | 操作 |
|------|------|
| 1 | 执行 Channel Suspend 的步骤 1-4 |
| 2 | 软件向 DMAC_ChEnReg.CH_SUSP 位写入 0 |
| 3 | DW_axi_dmac 从挂起点恢复 DMA 传输 |

**重要限制：**
一旦软件启动挂起操作（写入 1 到 CH_SUSP），在 CHx_IntStatusReg.CH_SUSPENDED 置 1 之前，软件不能通过写入 0 来恢复通道。DW_axi_dmac 会忽略此写入操作。

#### 2.18.3 Channel Suspend and Disable（通道挂起后禁用）

**挂起后禁用的步骤：**

| 步骤 | 操作 |
|------|------|
| 1 | 执行 Channel Suspend 的步骤 1-4 |
| 2 | 在 CHx_IntStatusReg.CH_SUSPENDED 置 1 后，软件向 DMAC_ChEnReg.CH_EN 写入 0 来禁用通道 |
| 3 | DW_axi_dmac 将 CHx_IntStatusReg.CH_DISABLED 置 1 |
| 4 | DW_axi_dmac 产生 CH_DISABLED 中断（如果未屏蔽） |
| 5 | DW_axi_dmac 清除 DMAC_ChEnReg.CH_EN 位为 0 |

**数据丢失警告：**
当 CHx_CTL.SRC_TR_WIDTH < CHx_CTL.DST_TR_WIDTH 时，FIFO 中剩余的数据不会被传输到目的端外设，数据会丢失。

#### 2.18.4 Channel Disable without Suspend（不挂起直接禁用）

**直接禁用通道的步骤：**

| 步骤 | 操作 |
|------|------|
| 1 | 软件向 DMAC_ChEnReg.CH_EN 写入 0 |
| 2 | DW_axi_dmac 在完成所有已发起的 AXI 源端传输后，优雅地停止所有来自源端外设的传输 |
| 3 | DW_axi_dmac 将通道 FIFO 中的所有数据传输到目的端外设 |
| 4 | DW_axi_dmac 清除通道锁定，并重置 CHx_CFG 寄存器 |
| 5 | DW_axi_dmac 将 CHx_IntStatusReg.ChLock_Cleared 置 1 |
| 6 | DW_axi_dmac 将 CHx_IntStatusReg.CH_DISABLED 置 1 |
| 7 | DW_axi_dmac 产生 CH_DISABLED 中断（如果未屏蔽） |
| 8 | DW_axi_dmac 清除 DMAC_ChEnReg.CH_EN 位为 0 |

**重要限制：**
一旦软件启动禁用操作（写入 0 到 CH_EN），在 CHx_IntStatusReg.CH_DISABLED 置 1 之前，软件不能通过写入 1 来重新使能通道。DW_axi_dmac 会忽略此写入操作。

#### 2.18.5 Abnormal Channel Abort（异常通道中止）

**中止不是推荐的操作！** 仅在以下情况使用：
- 软件想要禁用通道但不想重置整个 DW_axi_dmac
- 例如：某个通道由于未收到握手接口的响应而挂起

**建议：** 在中止之前，应先尝试禁用通道。

**中止通道的步骤：**

| 步骤 | 操作 |
|------|------|
| 1 | 软件向 DMAC_ChEnReg.CH_ABORT 写入 1 |
| 2 | DW_axi_dmac 在完成所有已发起的 AXI 源端/目的端传输后，优雅地停止所有传输 |
| 3 | 通道 FIFO 中的数据被刷新，会丢失 |
| 4 | DW_axi_dmac 清除通道锁定，并重置 CHx_CFG 寄存器 |
| 5 | DW_axi_dmac 将 CHx_IntStatusReg.ChLock_Cleared 置 1 |
| 6 | DW_axi_dmac 将 CHx_IntStatusReg.CH_ABORTED 置 1 |
| 7 | DW_axi_dmac 产生 CH_ABORTED 中断（如果未屏蔽） |
| 8 | DW_axi_dmac 清除 DMAC_ChEnReg.CH_EN 位为 0 |

**三种操作对比：**

| 特性 | Suspend | Disable | Abort |
|------|---------|---------|-------|
| 目的 | 暂时暂停传输 | 正常停止传输 | 紧急停止传输 |
| FIFO 数据 | 保留，可继续传输 | 尝试传输，剩余丢失 | 全部丢失 |
| 恢复能力 | 可恢复 | 不可恢复 | 不可恢复 |
| 推荐使用 | ✓ | ✓ | ✗ (仅紧急情况) |
| 中断状态 | CH_SUSPENDED | CH_DISABLED | CH_ABORTED |


### multi blocks transfer(重点)

4种多块传输模式：
- 连续地址模式
- 自动重载模式
- 影子寄存器模式
- LLI模式

#### 连续地址模式

**工作原理：**
- 连续块之间的地址选择为前一块结束地址的延续
- 地址自动递增，无需软件干预

**控制方式：**
- 通过 CHx_CTL.SRC_MLTBLK_TYPE 和 CHx_CTL.DST_MLTBLK_TYPE 寄存器字段控制
- 限制：CHx_SAR 和 CHx_DAR 不能同时选择为连续模式

**实现技巧：**
- 如果需要源地址和目的地址都连续，可以使用 Linked List 间接实现
- 设置下一个块描述符的 LLI.CHx_SAR 地址为前一块结束地址 +1
- 设置下一个块描述符的 LLI.CHx_DAR 地址为前一块结束地址 +1

**注意事项：**
如果启用连续地址模式的多块传输，完整的 DMA 传输必须至少有两个块，否则会导致不可预测的行为。

#### 自动重载模式

**工作原理：**
- 在每个块传输完成时，通道传输控制寄存器用它们的初始值重新加载
- 这些重新加载的值用于新的块传输

**可重载的寄存器：**

| 寄存器 | 说明 |
|--------|------|
| CHx_SAR | 源地址寄存器 |
| CHx_DAR | 目的地址寄存器 |
| CHx_BLOCK_TS | 块传输大小寄存器 |
| CHx_CTL | 通道控制寄存器 |

具体重载哪些寄存器取决于为源和目的外设选择的多块传输类型（参见 Table 2-4）。

**同步机制：**
如果块传输完成中断未屏蔽，DW_axi_dmac 会等待软件向 DMAC_IntClear_Reg 寄存器写入 1 来清除中断，然后才会继续下一个块传输。

**注意事项：**
如果启用自动重载模式的多块传输，完整的 DMA 传输必须至少有两个块，否则会导致不可预测的行为。

#### 影子寄存器模式

**工作原理：**
- 在每个块传输完成时，通道传输控制寄存器从对应的阴影寄存器加载
- 这些值用于新的块传输

**可加载的寄存器：**

| 寄存器 | 说明 |
|--------|------|
| CHx_SAR | 源地址寄存器 |
| CHx_DAR | 目的地址寄存器 |
| CHx_BLOCK_TS | 块传输大小寄存器 |
| CHx_CTL | 通道控制寄存器 |

**关键特性：**
- **无独立内存映射**：阴影寄存器没有独立的内存映射
- **软件写入方式**：软件总是写入 CHx_SAR, CHx_DAR, CHx_BLOCK_TS 和 CHx_CTL 寄存器
- **硬件内部路由**：如果使用阴影寄存器模式，DW_axi_dmac 内部将数据路由到对应的阴影寄存器
- **读取行为**：读取这些寄存器始终返回当前块的传输数据，而不是阴影寄存器内容（对应下一个块）

**有效性检查：**
- CHx_CTL.ShadowReg_Or_LLI_Valid 位指示阴影寄存器内容是否有效
  - 0 = 无效
  - 1 = 有效
- 如果在获取阶段读取为 0，DW_axi_dmac 会：
  a. 丢弃阴影寄存器内容
  b. 产生 ShadowReg_Or_LLI_Invalid_ERR 中断
  c. 等待软件向 CHx_BLK_TFR_ResumeReqReg 寄存器写入任意值，表示有效数据已就绪

**配置参数：**
如果 DMAX_CHx_SHADOW_REG_EN 参数设置为 0，阴影寄存器模式被禁用。


#### LLI 模式

LLI 模式本质上是 `linked-list-based multi-block transfer`。软件把每个 block 的参数写到内存中的链表项里，DMA 在 block 边界按链表继续取下一个描述符，而不是每次都由 CPU 重新编程通道寄存器。

**1. 基本使能条件**

根据手册的 programming flow，`CHx_CFG` 中的 `SRC_MLTBLK_TYPE` 和/或 `DST_MLTBLK_TYPE` 需要配置为 linked list 模式，也就是 `2'b11`。随后软件需要把第一个 LLI 的基地址，以及该 LLI 所在的 master interface，写入 `CHx_LLP` 寄存器。

**2. LLI 的作用**

LLI 中保存的是“下一个 block 的执行参数”。DMA 在取到 LLI 后，会把其中的内容复制到当前通道执行寄存器中，也就是：

- `CHx_SAR` 和/或 `CHx_DAR`
- `CHx_BLOCK_TS`
- `CHx_CTL`

因此在 LLI 多块传输模式下，`CHx_CTL` 可以在不同 block 之间变化，而软件在传输过程中通过 slave interface 直接改 `CHx_CTL` 是无效的。

**3. LLI 结构和对齐**

从手册描述来看，LLI 访问不会跨越一个完整的 64B LLI 结构；如果不受其他设置限制，DMA 会在一次 AXI burst 里取回其中约 40B 的有效内容。因此更稳妥的理解是：

- 软件侧通常按 64B 对齐来组织 LLI
- DMA 实际取用的是其中的有效字段，而不是“必须恰好只有 40B 的 C 结构体”

常见有效字段包括：

| 字段 | 作用 |
|------|------|
| `SAR` | 下一块的源地址 |
| `DAR` | 下一块的目的地址 |
| `BLOCK_TS` | 下一块的 block 大小 |
| `LLP` | 下一个 LLI 的地址 |
| `CTL` | 下一块的传输控制参数 |

**4. 结合已验证驱动来看链尾语义**

这里最容易误解。根据手册，DMA 判断“当前取到的 LLI 是否为最后一个 block”，看的不是“`llp` 指针是否为空”，而是取到的 LLI 中 `CHx_CTL.ShadowReg_Or_LLI_Last` 位。

结合 `dma_code/axidma.c` 里的已验证驱动实现，还能进一步看出：驱动在每个非尾描述符里都会把 `ctrl_h[30]` 清 0，而只在最后一个描述符里把 `ctrl_h[30]` 置 1。也就是说，驱动实际上把 `ctrl_h[30]` 当作“最后一个 block”标志在使用。

- `ShadowReg_Or_LLI_Last = 1`：当前 block 是本次 DMA transfer 的最后一个 block
- `ShadowReg_Or_LLI_Last = 0`：后面还有 block，DMA 会继续取下一个 LLI

从文章表达上，更稳妥的说法应该是：

- `llp_l/h` 用来给出“下一个描述符地址”
- `ctrl_h[30]` 用来给出“当前描述符是否为最后一个 block”

因此链尾语义应当建立在 `ShadowReg_Or_LLI_Last` 这类“last 标志”上，而不是把它简化成“看 `llp`”。

**5. 动态扩链的关键位**

手册明确提到，软件既可以一次性把整条链表都准备好，也可以动态扩展 linked list。动态扩链依赖两个位：

- `ShadowReg_Or_LLI_Valid`
- `ShadowReg_Or_LLI_Last`

其中：

- `ShadowReg_Or_LLI_Valid = 1` 表示当前取到的 LLI 有效
- `ShadowReg_Or_LLI_Valid = 0` 表示当前 LLI 暂时无效，DMA 可能上报 `ShadowReg_Or_LLI_Invalid_ERR`

如果 DMA 在取 LLI 时看到 `ShadowReg_Or_LLI_Valid = 0`，它会等待软件写 `CHx_BLK_TFR_ResumeReqReg`，然后再尝试下一次 LLI 读取。这正是动态扩链能成立的基础。

**6. LLI fetch 的 AXI 访问特点**

手册还有一个很重要的限制：LLI fetch 使用的 `arsize/awsize` 与数据总线宽度绑定，不能单独编程成其他值。burst length 则会被选择为“不跨越一个完整 64B LLI 结构”。

这意味着：

- LLI 访问方式和普通数据搬运访问方式并不完全等价
- LLI fetch 的总线行为受 master data width 影响明显
- 分析性能问题时，LLI fetch 也需要单独考虑

**7. 建议的理解顺序**

可以把 LLI 模式理解成下面这条主线：

1. 软件在内存中准备好一个或多个 LLI
2. 软件把第一个 LLI 地址写入 `CHx_LLP`
3. DMA 取回 LLI，并把其中的地址、长度、控制信息复制到通道寄存器
4. 当前 block 执行完成后，DMA 根据 `ShadowReg_Or_LLI_Last` 和 `ShadowReg_Or_LLI_Valid` 判断是结束、继续还是等待软件补链

这样理解会比单纯把它看成“desc[0] -> desc[1] -> desc[2]”更接近硬件真实行为。

**8. 当前驱动实际实现的是哪一种 LLI**

结合 `source/_posts/dma/dma_code/axidma.c` 可以看到，这套已经验证正确的驱动实现的是“静态 LLI 链”：

- 软件在启动前一次性把整条描述符链准备好
- 每个描述符都提前写好 `sar/dar/block_ts/ctrl/llp`
- 通过 `CH_LLP` 只把首个描述符地址交给硬件
- 传输过程中没有使用 `CH_BLK_TFR_ResumeReqReg`
- 也没有实现运行时补链的 `ShadowReg_Or_LLI_Valid` 流程

所以如果当前目标是理解这套驱动，那么可以先把 LLI 模式理解为“静态 block chaining”；动态扩链属于 databook 支持、但这份驱动暂未使用的高级能力。


### 中断

#### 中断类型

DW_axi_dmac 支持以下几大类中断：

**1. 传输完成类中断（Channel Interrupts）**

| 中断名称 | 触发条件 | 寄存器位 |
|----------|----------|----------|
| Block Transfer Done | 块传输完成 | CHx_INTSTATUS.Block_Tfr_Done |
| Transaction Done | 事务传输完成 | CHx_INTSTATUS.Tran_Done |
| Source Transaction Done | 源端事务完成 | CHx_INTSTATUS.Src_Tran_Done |
| Destination Transaction Done | 目的端事务完成 | CHx_INTSTATUS.Dst_Tran_Done |
| DMA Transfer Done | DMA 传输完成 | CHx_INTSTATUS.DMA_Tfr_Done |
| Channel Disable Done | 通道禁用完成 | CHx_INTSTATUS.Ch_Disabled |

**2. 错误类中断（Error Interrupts）**

| 中断名称 | 触发条件 | 寄存器位 |
|----------|----------|----------|
| AXI Bus Error | AXI 总线错误（源端） | CHx_INTSTATUS.Src_AXI_Bus_Err |
| AXI Bus Error | AXI 总线错误（目的端） | CHx_INTSTATUS.Dst_AXI_Bus_Err |
| LLI Fetch Error | LLI 取指错误 | CHx_INTSTATUS.LLP_Fetch_Err |
| LLI Write-back Error | LLI 回写错误 | CHx_INTSTATUS.LLP_Writeback_Err |

**3. ECC 保护类中断（ECC Protection Interrupts）**

| 中断名称 | 触发条件 | 寄存器位 |
|----------|----------|----------|
| FIFO ECC Correctable Error | FIFO 单比特错误（已纠正） | CHx_INTSTATUS.ECC_PROT_CHMem_CorrERR |
| FIFO ECC Uncorrectable Error | FIFO 多比特错误（不可纠正） | CHx_INTSTATUS.ECC_PROT_CHMem_UnCorrERR |
| AXI Master ECC Correctable Error | AXI 主接口单比特错误 | DMAC_COMMONREG_INTSTATUS.MXIF_ECC_CorrERR |
| AXI Master ECC Uncorrectable Error | AXI 主接口多比特错误 | DMAC_COMMONREG_INTSTATUS.MXIF_ECC_UnCorrERR |

**4. 奇偶校验错误中断（Parity Error Interrupts）**

| 中断名称 | 触发条件 | 寄存器位 |
|----------|----------|----------|
| Common Register Parity Error | 公共寄存器写奇偶校验错误 | DMAC_COMMONREG_INTSTATUS.SLVIF_CommonReg_WRPARITY_ERR |
| Channel Register Parity Error | 通道寄存器写奇偶校验错误 | CHx_INTSTATUS.SLVIF_WRPARITY_ERR |

**5. 通用中断（Common Interrupts）**

| 中断名称 | 触发条件 |
|----------|----------|
| Common Register Interrupt | 任意通道中断汇总 |
| Combined Interrupt | 所有中断的综合输出 |

#### 中断生成机制

**中断生成流程图：**

```
传输事件/错误发生
       ↓
更新 CHx_INTSTATUS  ← 状态寄存器置位
       ↓
CHx_INTSTATUS_ENA (Interrupt Mask)  ← 通道中断使能检查
       ↓ AND
CHx_INTSIGNAL_ENA  ← 中断信号使能检查
       ↓ AND
DMAC_CFG.INT_EN  ← 全局中断使能检查
       ↓
生成中断输出信号 → intr / intr_ch[x] / intr_cmreg
```

**中断使能层次结构：**

| 层级 | 名称 | 寄存器/位 |
|------|------|-----------|
| Level 1 | 全局中断使能 | DMAC_CFG.INT_EN (bit 1) |
| Level 2 | 通道中断使能 | CHx_INTSTATUS_ENA |
| Level 3 | 中断信号使能 | CHx_INTSIGNAL_ENA |
| 输出 | 中断输出 | → 中断输出 |

#### 中断寄存器映射

**3.1 通道中断寄存器（每通道）**

| 寄存器 | 偏移 | 功能 |
|--------|------|------|
| CHx_INTSTATUS | 0x088 | 中断状态寄存器（R/W） |
| CHx_INTSTATUS_ENA | 0x080 | 中断使能寄存器（R/W） |
| CHx_INTSIGNAL_ENA | 0x090 | 中断信号使能寄存器（R/W） |
| CHx_INTCLEAR | 0x098 | 中断清除寄存器（W） |

**3.2 通用中断寄存器**

| 寄存器 | 偏移 | 功能 |
|--------|------|------|
| DMAC_COMMONREG_INTSTATUS | 0x050 | 通用中断状态 |
| DMAC_COMMONREG_INTSTATUS_ENA | 0x040 | 通用中断使能 |
| DMAC_COMMONREG_INTSIGNAL_ENA | 0x048 | 通用中断信号使能 |
| DMAC_COMMONREG_INTCLEAR | 0x038 | 通用中断清除 |
| DMAC_INTSTATUS | 0x030 | 综合中断状态输出 |

#### 中断处理流程

**4.1 轮询模式处理流程**

```c
// 1. 配置传输
axidma_reg_write(ch_base + CH_SAR_L, src_addr & 0xffffffff);
axidma_reg_write(ch_base + CH_DAR_L, dst_addr & 0xffffffff);
axidma_reg_write(ch_base + CH_BLOCK_TS, len);
axidma_reg_write(ch_base + CH_CTL_L, ctl_val);

// 2. 使能中断
axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);      // 使能 Block 和 Transfer 完成中断
axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);

// 3. 启动通道
axidma_reg_write(base + DMAC_CHEN_L, (1 << (chann-1)) | (1 << (chann-1+8)));

// 4. 轮询等待中断状态
while(1) {
    val = axidma_reg_read(ch_base + CH_INTSTATUS);
    if((val & 0x1) == 0x1) {  // Block_Tfr_Done
        axidma_reg_write(ch_base + CH_INTCLEAR, val);  // 清除中断
        break;
    }
}
```

**4.2 中断模式处理流程**

```c
// 1. 注册中断处理函数
axidma_handler.irq_id = AXIDMA1_IRQ_NUM;
axidma_handler.handler = axidma_irq_handler;
intc_request_irq(&axidma_handler);
intc_enable_normal_irq(axidma_handler.irq_id);

// 2. 使能全局中断
axidma_reg_write(base + DMAC_CFG, val | (0x01 << 1));  // INT_EN

// 3. 使能通道中断
axidma_reg_write(ch_base + CH_INTSTATUS_ENA, 0x03);
axidma_reg_write(ch_base + CH_INTSIGNAL_ENA, 0x03);

// 4. 启动传输
axidma_reg_write(base + DMAC_CHEN_L, enable_val);

// 5. 等待中断处理完成
while(1) {
    if(g_interrupt_done == 1) {
        g_interrupt_done = 0;
        break;
    }
}

// 中断服务程序
void axidma_irq_handler(unsigned int irq_id) {
    // 读取中断状态
    val = axidma_reg_read(base + DMAC_COMMON_INTSTATUS);

    // 清除通用中断
    axidma_reg_write(base + DMAC_COMMON_INTCLEAR, val);

    // 清除通道中断
    for(i=1; i<9; i++) {
        ch_base = base + 0x100 + (i-1) * 0x100;
        val = axidma_reg_read(ch_base + CH_INTSTATUS);
        axidma_reg_write(ch_base + CH_INTCLEAR, val);
    }

    g_interrupt_done = 1;
}
```

#### 错误处理流程

**5.1 AXI 总线错误处理**

当发生 AXI 总线错误时：

**1. 硬件自动操作：**
- DMA 传输优雅停止
- 通道自动禁用
- CHx_INTSTATUS 错误位置位
- CH_DISABLED 状态位置位

**2. 软件处理流程：**
```c
// 检查错误中断
val = axidma_reg_read(ch_base + CH_INTSTATUS);
if(val & SRC_AXI_Bus_Err) {
    // 源端 AXI 错误处理
    printf("Source AXI Bus Error!\n");
}
if(val & DST_AXI_Bus_Err) {
    // 目的端 AXI 错误处理
    printf("Destination AXI Bus Error!\n");
}

// 清除错误中断
axidma_reg_write(ch_base + CH_INTCLEAR, val);

// 重新初始化传输（如需重试）
```

**5.2 ECC 错误处理**

```c
// 读取 ECC 错误状态
val = axidma_reg_read(DMAC_COMMONREG_ECCCTLSTATUSREG);

if(val & ECC_CORR_ERR_MASK) {
    // 单比特错误 - 已自动纠正
    printf("ECC Correctable Error detected\n");
    // 读取错误计数器
    corr_count = val & ECC_CORR_COUNT_MASK;
}

if(val & ECC_UNCORR_ERR_MASK) {
    // 多比特错误 - 数据损坏
    printf("ECC Uncorrectable Error detected!\n");
    // 需要重新传输
}

// 清除错误计数器
axidma_reg_write(DMAC_COMMONREG_ECCCTLSTATUSREG, val | ECC_ERR_COUNT_RST);
```

#### 中断输出配置 & 中断位定义

**六、中断输出配置**

DMAC 支持三种中断输出配置模式（通过 DMAX_INTR_IO_TYPE 参数配置）：

| 模式 | 值 | 中断输出 |
|------|-----|----------|
| COMBINED_ONLY | 0 | 单一综合中断输出 |
| CHANNEL_AND_COMMONREG | 1 | 分离的通道中断 + 通用中断 |
| ALL_INTERRUPT_OUTPUTS | 2 | 所有中断独立输出 |

**七、中断位定义（CHx_INTSTATUS）**

| 位 | 名称 | 说明 |
|----|------|------|
| Bit 0 | Block_Tfr_Done | 块传输完成 |
| Bit 1 | Tran_Done | 事务传输完成 |
| Bit 2 | Src_Tran_Done | 源事务完成 |
| Bit 3 | Dst_Tran_Done | 目的事务完成 |
| Bit 4 | DMA_Tfr_Done | DMA 传输完成 |
| Bit 5 | Ch_Disabled | 通道禁用完成 |
| Bit 6 | Src_AXI_Bus_Err | 源 AXI 总线错误 |
| Bit 7 | Dst_AXI_Bus_Err | 目的 AXI 总线错误 |
| Bit 8 | LLP_Fetch_Err | LLI 取指错误 |
| Bit 9 | LLP_WriteBack_Err | LLI 回写错误 |
| Bit 10 | ECC_PROT_CorrERR | ECC 可纠正错误 |
| Bit 11 | ECC_PROT_UnCorrERR | ECC 不可纠正错误 |
| Bit 12 | SLVIF_WRPARITY_ERR | 写奇偶校验错误 |

#### 实际代码实现

```
void axidma_irq_handler1(unsigned int irq_id)
{
	int i;
	unsigned int base;
	unsigned int ch_base;
	unsigned int val;

	//printf("%s %d\n",__func__, irq_id);
	base = AXIDMA1_BASE_ADDR + (axidma_handler1.irq_id -AXIDMA1_IRQ_NUM)* 0x10000;

	//intc_disable_normal_irq(axidma_handler1.irq_id);

	val = axidma_reg_read(base + DMAC_COMMON_INTSTATUS);
	axidma_reg_write(base + DMAC_COMMON_INTCLEAR, val);

	for(i=1;i<9;i++)
	{
		ch_base = base + 0x100 + (i-1) * 0x100;
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		axidma_reg_write(ch_base + CH_INTCLEAR, val);
	}
	
	g_interrupt_done = 1;
}
```

#### 最佳实践

**八、中断配置最佳实践**

**1. 传输前配置：**
- 清除所有 pending 中断
- 配置中断使能寄存器
- 使能全局中断

**2. 中断服务程序：**
- 首先读取中断状态
- 根据中断类型执行相应处理
- 及时清除中断状态

**3. 错误处理：**
- 使能错误中断
- 在中断中记录错误信息
- 必要时重新初始化传输

### ECC特性
TODO:

## berametal implement

### LLI 

#### 描述符说明 & 结构实现

**1.1 硬件描述符格式（64 字节对齐）**

```c
struct axidma_desc {
    unsigned int sar_l, sar_h;    // Source Address Low/High (源地址)
    unsigned int dar_l, dar_h;    // Destination Address Low/High (目的地址)
    unsigned int block_ts;        // Block Transfer Size (块传输大小)
    unsigned int reserved;        // 保留
    unsigned int llp_l, llp_h;    // Linked List Pointer Low/High (下一个描述符地址)
    unsigned int ctrl_l, ctrl_h;  // Control Low/High (控制寄存器)
    unsigned int sstat;           // Source Status (源状态)
    unsigned int dstat;           // Destination Status (目的状态)
    unsigned int llp_status_l, llp_status_h;  // LLI Status (LLI 状态)
    unsigned int reserved2, reserved3;        // 保留
};
```

**1.2 描述符字段说明**

| 字段 | 宽度 | 功能 |
|------|------|------|
| SAR | 64 位 | 源数据地址 |
| DAR | 64 位 | 目的数据地址 |
| BLOCK_TS | 20 位 | 块传输大小（单位：传输宽度） |
| LLP | 64 位 | 指向下一个描述符的指针（bit[5:0] 必须为 0，64 字节对齐） |
| CTRL_L | 32 位 | 传输宽度、burst 大小等 |
| CTRL_H | 32 位 | 中断使能、LLI 结束标志等 |


#### 核心配置函数实现

**2.1 函数原型**

```c
void __axidma_config(struct axidma_config config, struct axidma_desc *desc)
```

**2.2 配置流程**

```c
void __axidma_config(struct axidma_config config, struct axidma_desc *desc)
{
    // 1. 计算传输宽度
    transfer_val = config.width;  // 0=8bit, 1=16bit, 2=32bit, 3=64bit, 4=128bit
    switch(transfer_val) {
        case 0: transfer_width = 8; break;
        case 1: transfer_width = 16; break;
        case 2: transfer_width = 32; break;
        case 3: transfer_width = 64; break;
        case 4: transfer_width = 128; break;
    }

    // 2. 验证参数
    if(config.nums > MAX_SG_NUMS) { /* 错误处理 */ }

    // 3. 配置通道寄存器
    base = AXIDMA1_BASE_ADDR + (config.id -1)* 0x10000;
    ch_base = base + 0x100 + (config.chann -1) * 0x100;

    // 3.1 配置传输类型：Memory to Memory
    val = axidma_reg_read(ch_base + CH_CFG_H);
    val &= ~(0x03);  // TT_FC = MEM_TO_MEM_DMAC
    val |= config.prior << (49-32);  // 通道优先级
    axidma_reg_write(ch_base + CH_CFG_H, val);

    // 3.2 配置多块传输类型：Linked List
    val = axidma_reg_read(ch_base + CH_CFG_L);
    val |= 0x0f;  // SRC_MULTBLK_TYPE = DST_MULTBLK_TYPE = LINKED_LIST
    axidma_reg_write(ch_base + CH_CFG_L, val);

    // 4. 构建描述符链表
    i = 0;
    for(n = 0; n < config.nums; n++) {
        while(config.len[n]) {
            // 4.1 计算本次传输大小（最大 0x40000）
            transfer_size = config.len[n] * transfer_width/8 > MAX_BLOCK_TRANSFER_SIZE
                          ? MAX_BLOCK_TRANSFER_SIZE
                          : config.len[n] / (transfer_width/8);

            // 4.2 配置源地址和目的地址
            desc[i].sar_l = (config.src_addr[n] + offset) & 0xffffffff;
            desc[i].sar_h = ((config.src_addr[n] + offset) >> 32) & 0xffffffff;
            desc[i].dar_l = (config.dst_addr[n] + offset) & 0xffffffff;
            desc[i].dar_h = ((config.dst_addr[n] + offset) >> 32) & 0xffffffff;

            // 4.3 配置块传输大小
            desc[i].block_ts = transfer_size - 1;  // 硬件需要减 1

            // 4.4 配置控制寄存器低 32 位
            // [12:11] DST_TR_WIDTH, [9:8] SRC_TR_WIDTH
            val = transfer_val << 11 | transfer_val << 8 | 0x0 << 6 | 0x0 << 4 | 0x0 << 2 | 0x0;
            desc[i].ctrl_l = val;

            // 4.5 配置控制寄存器高 32 位
            val = axidma_reg_read(ch_base + CH_CTL_H);
            val |= 0x01U << 31;     // 驱动中该位始终置 1
            val &= ~(0x01 << 30);   // 非最后一个描述符：ctrl_h[30] = 0
            if(config.intr_en == 1)
                val |= 0x1 << 26;   // IOC_BlkTfr 中断使能
            else
                val &= ~(0x1 << 26);
            desc[i].ctrl_h = val;

            // 4.6 配置链表指针（指向下一个描述符）
            desc[i].llp_l = (uint32_t)(((uint64_t)(&desc[i+1])) & 0xffffffff);
            desc[i].llp_h = (uint32_t)((((uint64_t)(&desc[i+1])) >> 32) & 0xffffffff);

            i++;
            offset += transfer_size * transfer_width/8;
            config.len[n] -= transfer_size * transfer_width/8;
        }
    }

    // 5. 标记最后一个描述符
    desc[i-1].ctrl_h |= 0x01 << 30;  // 最后一个描述符：ctrl_h[30] = 1

    // 6. 加载链表头指针到硬件
    axidma_reg_write(ch_base + CH_LLP_L, (uint32_t)(((uint64_t)&desc[0] & 0xffffffff) & (~0x3f)));
    axidma_reg_write(ch_base + CH_LLP_H, (uint32_t)((((uint64_t)&desc[0]) >> 32) & 0xffffffff));
}
```

**关键寄存器配置：**

**CH_CFG_L 寄存器：**
- **[3:0] SRC_MULTBLK_TYPE / DST_MULTBLK_TYPE**
  - 0b0000 = CONTIGUOUS（连续地址）
  - 0b0001 = RELOAD（自动重载）
  - 0b0010 = SHADOW（阴影寄存器）
  - 0b1111 = LINKED_LIST（链表）

**CH_CTL_H 寄存器：**
- **[31]** - 在当前驱动实现中始终置 1
- **[30]** - 当前驱动中被实际用作“最后一个描述符”标志位
- **[26] IOC_BlkTfr** - Block 完成中断使能

这里要特别注意：上面的位语义是“结合已验证驱动行为后的文章化总结”。如果只看 databook，应优先按 `ShadowReg_Or_LLI_Last` / `ShadowReg_Or_LLI_Valid` 这套语义来理解。


### 测试用例



- 基础 LLI 测试 --- 2 段链表

- 基础 LLI 测试 --- 5 段链表

- 多 SG 列表测试

- 多通道 + 优先级 + LLI 测试

- 压力测试



**测试用例汇总表：**



| 测试函数 | 功能 | 描述符数量 | 通道数 | 特性 |
|----------|------|------------|--------|------|
| dmac_memcpy_lli_func04_2 | 基础 LLI 测试 | 2 | 1 | 验证基本链表功能 |
| dmac_memcpy_lli_func04_3 | 多段 LLI 测试 | 5 | 1 | 验证长链表功能 |
| dmac_memcpy_lli_func05 | SG 列表测试 | 10+ | 1 | 验证分散传输 |
| dmac_memcpy_lli_func06 | 多通道 + 优先级 | 15×2 | 2 | 验证通道仲裁 |
| dmac_stress_lli_func06 | 压力测试 | 多段×8 | 8 | 验证满载性能 |

#### 多 SG 列表测试
该测试用例主要特点在与：SRC/DST的地址都不连续:

```
int dmac_memcpy_lli_func05(unsigned int id, unsigned int chann, unsigned int width)
{	
	int i,n;
	
	unsigned int val;
	unsigned int base;
	unsigned int ch_base;
	unsigned int error_flg;         // 潜在bug，显式赋值0比较好
	struct axidma_config config;
	unsigned int transfer_width=0;
	
	switch(width)
	{
		case 0:
			transfer_width = 8;
			break;
		case 1:
			transfer_width = 16;
			break;
		case 2:
			transfer_width = 32;
			break;
		case 3:
			transfer_width = 64;
			break;
		case 4:
			transfer_width = 128;
			break;
		default:
			break;
	}
	

	//for(i=0;i<0x800;i++)
	//	*(unsigned char *)(0x200000000+i) = i;
	
	config.id = id;
	config.chann = chann;
	for(i=0;i<10;i++)
	{
		config.len[i] = 0x1000;         // ------> 长度 0x1000
		config.src_addr[i] = 0x210000000 + i*config.len[i]*2;   // -------> 两块地址之间间隔 0x2000
		config.dst_addr[i] = 0x210700000 + i*config.len[i]*2;
		memset((unsigned char *)config.src_addr[i], i+1, 0x1000);
	}
	config.nums = 10;
	config.width = width;
	config.prior = 1;
	config.intr_en = 1;
	__axidma_config(config, desc1);
	dmac_start(id,chann);
	
	base = AXIDMA1_BASE_ADDR + (config.id -1)* 0x10000;
	ch_base = base + 0x100 + (config.chann -1) * 0x100;
	
	while(1)
	{
		val = axidma_reg_read(ch_base + CH_INTSTATUS);
		//printf("CH_INTSTATUS:0x%x=0x%x in %s\n",ch_base + CH_INTSTATUS, val, __func__);
		if((val & 0x3) == 0x3)
		{
			break;
		}
	}
	
	for(n=0;n<10;n++)
	{
		for(i=0;i<config.len[n] * transfer_width/8;i++)
		{
			if(*(unsigned char *)(config.src_addr[n]+i) != *(unsigned char *)(config.dst_addr[n]+i))
			{
				error_flg++;
				printf("n=%d, i=%d, src_addr=0x%x != dst_addr=0x%x\n",n, i, *(unsigned char *)(config.src_addr[n]+i), *(unsigned char *)(config.dst_addr[n]+i));
			}
		}
	}
	
	
	if(error_flg == 0)
		return 0;
	else
		return -1;
}
```

#### 内存布局图示

**源内存区域 (DDR) 与 目的内存区域 (DDR) 映射：**

```
源内存区域 (DDR)              目的内存区域 (DDR)
0x210000000                   0x2107000000
┌─────────────────┐          ┌─────────────────┐
│ Segment 0       │          │ Segment 0       │
│ data=0x01       │ 0x1000   │ data=0x01       │
│                 │ bytes    │                 │
└─────────────────┘          └─────────────────┘
     间隔 0x1000                   间隔 0x1000
┌─────────────────┐          ┌─────────────────┐
│ Segment 1       │          │ Segment 1       │
│ data=0x02       │ 0x1000   │ data=0x02       │
│                 │ bytes    │                 │
└─────────────────┘          └─────────────────┘
     间隔 0x1000                   间隔 0x1000
┌─────────────────┐          ┌─────────────────┐
│ Segment 2       │          │ Segment 2       │
│ data=0x03       │ 0x1000   │ data=0x03       │
│                 │ bytes    │                 │
└─────────────────┘          └─────────────────┘
       ...                          ...
┌─────────────────┐          ┌─────────────────┐
│ Segment 9       │          │ Segment 9       │
│ data=0x0A       │ 0x1000   │ data=0x0A       │
│                 │ bytes    │                 │
└─────────────────┘          └─────────────────┘
0x210024000                   0x210724000
```

**说明：**
- 每段间隔 = 0x1000 (未使用)
- 每段大小 = 0x1000 (4KB)
- 总传输数据 = 10 × 4KB = 40KB

#### 静态描述符数组

**静态分配的描述符数组 (desc1[MAX_DESC_NUMS])：**

```
desc1[0]
├── sar_l/h    = 0x210000000
├── dar_l/h    = 0x210700000
├── block_ts   = 0xFFF (0x1000-1)
├── ctrl_l     = 传输宽度配置
├── ctrl_h     = bit31=1, bit30=0, IOC=1
└── llp_l/h    = &desc1[1]

desc1[1]
├── sar_l/h    = 0x210002000
├── dar_l/h    = 0x210702000
├── block_ts   = 0xFFF
├── ctrl_l     = 传输宽度配置
├── ctrl_h     = bit31=1, bit30=0, IOC=1
└── llp_l/h    = &desc1[2]

desc1[2]
├── sar_l/h    = 0x210004000
├── dar_l/h    = 0x210704000
├── block_ts   = 0xFFF
├── ctrl_l     = 传输宽度配置
├── ctrl_h     = bit31=1, bit30=0, IOC=1
└── llp_l/h    = &desc1[3]

...

desc1[9] (最后一个)
├── sar_l/h    = 0x21001C000
├── dar_l/h    = 0x21071C000
├── block_ts   = 0xFFF
├── ctrl_l     = 传输宽度配置
├── ctrl_h     = bit31=1, bit30=1（最后一个 block）
└── llp_l/h    = 0 (不需要)

↑
CH_LLP_L/H 寄存器加载此地址
```

#### 软硬件执行流程图

**软件配置流程：**
```
1. 分配描述符数组 (64 字节对齐)
   struct axidma_desc desc[MAX_DESC_NUMS]

2. 填充描述符内容
   desc[0]: sar, dar, block_ts, ctrl, llp -> desc[1]
   desc[1]: sar, dar, block_ts, ctrl, llp -> desc[2]
   ...
   desc[n-1]: sar, dar, block_ts, ctrl (ctrl_h[30]=1，表示最后一个 block)

3. 配置 CH_CFG_L = LINKED_LIST (0x0f)

4. 加载链表头指针到 CH_LLP_L/H

5. 使能通道 DMAC_CHEN_L
```

**硬件执行流程：**
```
DMA 引擎取指 desc[0] → 执行传输 → 完成中断 (可选)
       ↓
根据 LLP 取指 desc[1] → 执行传输 → 完成中断 (可选)
       ↓
       ...
       ↓
取指 desc[n-1] → 执行传输 → 完成中断 → 通道禁用
(ctrl_h[30]=1，当前描述符为最后一个 block)
```

## 关键参数



**AXI DMAC 关键参数汇总表**



| 参数名称 | 最小值 | 最大值 | 默认值 | 决定因素 | 相关寄存器/位 |
|----------|--------|--------|--------|----------|---------------|
| Transfer Width (TR_WIDTH) | 8 bit | 128 bit | - | 软件可编程 | CHx_CTL.SRC_TR_WIDTH[14:12], CHx_CTL.DST_TR_WIDTH[10:8] |
| Burst Size (MSIZE) | 1 beat | 32 beats | - | 软件可编程 | CHx_CTL.SRC_MSIZE[28:26], CHx_CTL.DST_MSIZE[24:22] |
| 最大通道数量 | 1 | 32 | 1 | 配置参数 DMAX_NUM_CHANNELS | - |
| 同时并行工作通道数 | 1 | 配置值 | - | 由仲裁器和配置决定 | CHx_CFG_H.CH_PRIOR |
| 最大 Block Size (BLOCK_TS) | 1 | 0x40000 (256K) | - | 硬件固定限制 | CHx_BLOCK_TS[17:0] |
| 硬件握手接口数量 | 0 | 64 | 2 | 配置参数 DMAX_NUM_HS_IF | - |
| AXI 主接口数量 | 1 | 2 | 1 | 配置参数 DMAX_NUM_MASTER_IF | - |
| AXI 数据总线宽度 | 32 bit | 512 bit | - | 配置参数 DMAX_M_DATA_WIDTH | - |
| 通道优先级 | 0 | DMAX_NUM_CHANNELS-1 | - | 软件可编程 | CHx_CFG_H.CH_PRIOR[44:42] |
| FIFO 深度 | 8 | 可配置 | - | 配置参数 DMAX_CHx_FIFO_DEPTH | - |
| FIFO 宽度 | 32 bit | 128 bit | - | 配置参数 DMAX_CHx_FIFO_WIDTH | - |
| LLI 描述符大小 | - | 64 bytes | - | 硬件固定 | - |
| LLP 对齐要求 | - | 64-byte | - | 硬件固定（低 6 位必须为 0） | CHx_LLP[31:6] |
| 最大 outstanding 请求数 | 16 | 128 | 16 | 配置参数 DMAX_MSTIF_OSR_LMT | CHx_CFG_H.SRC_OSR_LMT, DST_OSR_LMT |

### Transfer width

**传输宽度编码表：**

| 编码值 | 传输宽度 | 说明 |
|--------|----------|------|
| 0 | 8 bit (1 byte) | 字节传输 |
| 1 | 16 bit (2 bytes) | 半字传输 |
| 2 | 32 bit (4 bytes) | 字传输 |
| 3 | 64 bit (8 bytes) | 双字传输 |
| 4 | 128 bit (16 bytes) | 128 位传输 |

**寄存器位置：**
- CHx_CTL.SRC_TR_WIDTH[14:12] - 源传输宽度
- CHx_CTL.DST_TR_WIDTH[10:8] - 目的传输宽度

### Burst size

**Burst 长度编码表：**

| 编码值 | Burst 长度 | 说明 |
|--------|------------|------|
| 0 | 1 beat | 单次传输 |
| 1 | 4 beats | 4 拍突发 |
| 2 | 8 beats | 8 拍突发 |
| 3 | 16 beats | 16 拍突发 |
| 4 | 32 beats | 32 拍突发 |
| 5-7 | 保留 | - |

**寄存器位置：**
- CHx_CTL.SRC_MSIZE[28:26] - 源突发长度
- CHx_CTL.DST_MSIZE[24:22] - 目的突发长度

### Block size

**Block Size 参数表：**

| 参数 | 值 |
|------|-----|
| 最大值 | 0x40000 (256K 传输单元) |
| 寄存器 | CHx_BLOCK_TS[17:0] |
| 实际字节数 | BLOCK_TS × TR_WIDTH/8 |

**计算公式：**
```
实际传输字节数 = (BLOCK_TS + 1) × (2^TR_WIDTH) / 8
```

**示例：**
- TR_WIDTH=2 (32bit), BLOCK_TS=0x40000 → 最大 1MB
- TR_WIDTH=3 (64bit), BLOCK_TS=0x40000 → 最大 2MB

### FIFO



**FIFO 参数表：**



| 参数 | 范围 | 默认值 | 配置参数 |
|------|------|--------|----------|
| FIFO 深度 | 8 ~ 可配置 | - | DMAX_CHx_FIFO_DEPTH |
| FIFO 宽度 | 32/64/128 bit | - | DMAX_CHx_FIFO_WIDTH |
| ECC 保护 | Yes/No | No | DMAX_ECC_MEMIF_EN |



### FIFO 与 ECC 开销



**FIFO 宽度与 ECC 开销表：**



| FIFO 宽度 | FIFO 深度 | ECC 前数据宽度 | ECC 校验位 | ECC 后总宽度 |
|-----------|-----------|----------------|------------|--------------|
| 32 bit | 8 | 35 | 7 | 42 |
| 32 bit | 32 | 37 | 7 | 44 |
| 64 bit | 8 | 67 | 8 | 75 |
| 64 bit | 32 | 69 | 8 | 77 |
| 128 bit | 32 | 133 | 9 | 142 |



### 中断输出配置



一般使用 comb 中断？



**中断输出模式表：**



| 模式 | 输出信号 |
|------|----------|
| COMBINED_ONLY (0) | intr（所有中断组合） |
| CHANNEL_AND_COMMONREG (1) | intr_ch, intr_cmreg |
| ALL_INTERRUPT_OUTPUTS (2) | intr, intr_ch, intr_cmreg |

## 握手信号

### 核心目的

是的，握手信号的设计就是用来做 Flow Control (流量控制) 的。

**握手信号在 DMA 传输中起到以下关键作用：**
1. 协调 DMA 与外设之间的数据传输节奏 - 确保外设准备好接收/发送数据时 DMA 才动作
2. 支持可变长度的块传输 - 外设可以动态控制何时结束一个 block
3. 区分单次传输和突发传输 - 外设可以请求不同类型的传输
4. 支持两种 Flow Controller 模式 - DMA 作为流控制器 或 外设作为流控制器

### 硬件握手信号

**握手信号定义表：**

| 信号名 | 方向 | 含义 |
|--------|------|------|
| dma_req | 外设→DMAC | 突发传输请求 |
| dma_single | 外设→DMAC | 单次传输请求/状态 |
| dma_last | 外设→DMAC | 最后一次传输指示 |
| dma_ack | DMAC→外设 | DMAC 确认 |
| dma_finish | DMAC→外设 | 块传输完成指示 |

### 两种流控模式

这是理解握手信号的关键。DMAC 支持两种流控模式：

**模式 1：DMAC 作为 Flow Controller（外设不是流控器）**

**适用场景：** 传输长度已知的固定长度数据块

| 信号 | 行为 |
|------|------|
| dma_req | 请求突发传输，必须保持到 dma_ack 到来 |
| dma_single | 仅在 Single Transaction Region 有效，表示外设能接收/发送至少 1 个数据 |
| dma_last | 被忽略，无意义 |
| dma_ack | 传输完成后 assertion，等待外设撤销请求后 de-assert |
| dma_finish | Block 传输完成时 assertion |

**时序规则：**
```
外设：dma_req 断言 ───────┐
DMAC:        dma_ack 断言 ─┼── dma_ack 撤销
外设:                      └── dma_req 撤销
```

**模式 2：外设作为 Flow Controller**

**适用场景：** 传输长度未知的可变长度数据块（如 FIFO 数据）

| 信号 | 行为 |
|------|------|
| dma_req | 必须与 dma_single 或 dma_last 同时 assertion 才有效 |
| dma_single | 与 dma_req 同时 assertion = 请求单次传输 |
| dma_last | 与 dma_req 同时 assertion = 这是最后一次传输 |
| dma_ack | 传输完成后 assertion，外设撤销请求后立即撤销 |
| dma_finish | 与 dma_ack 同时 assertion，表示 block 完成 |

**组合逻辑（外设是流控器时）：**

| dma_req | dma_single | dma_last | DMAC 动作 |
|---------|------------|----------|----------|
| 0 | X | X | 无效请求 |
| 1 | 0 | 0 | 突发传输（非最后） |
| 1 | 0 | 1 | 突发传输（最后一次） |
| 1 | 1 | 0 | 单次传输（非最后） |
| 1 | 1 | 1 | 单次传输（最后） |



### 握手接口映射的硬件设计和软件配置



**握手配置代码实现：**



```c

// 初始化 DMA 通道的握手配置

int dmac_channel_init(unsigned int ch, enum flow_controller fc,

                      enum handshaking_type hs_type, unsigned int hs_if)

{

    u32 val;



    // 1. 配置 Flow Controller

    val = axidma_reg_read(CHx_CTL_H);

    val &= ~(0x03 << 20);

    val |= (fc << 20);  // TT_FC

    axidma_reg_write(CHx_CTL_H, val);



    // 2. 配置握手类型（硬件/软件）

    val = axidma_reg_read(CHx_CFG_L);

    val &= ~(0x03 << 32);

    val |= (hs_type << 32);  // HS_SEL_SRC/DST

    axidma_reg_write(CHx_CFG_L, val);



    // 3. 配置握手接口映射

    val = axidma_reg_read(CHx_CFG_L);

    val &= ~(0x3F << 36);

    val |= (hs_if << 36);  // SRC_HS_IF / DST_HS_IF

    axidma_reg_write(CHx_CFG_L, val);



    return 0;

}

```



**硬件预设外设与 DMAC 的握手信号的 HS_IF 编号示例：**



| 外设 | 推荐 HS_IF 编号 | Flow Controller | SRC_HS_IF | DST_HS_IF | 说明 |
|------|----------------|-----------------|-----------|-----------|------|
| UART RX | 0 | DMAC | 0 | N/A | UART 作为源，FIFO 满阈值请求 |
| UART TX | 1 | DMAC | N/A | 1 | UART 作为目的，FIFO 空阈值请求 |
| SPI RX | 2 | DMAC | 2 | N/A | SPI RX FIFO 数据就绪请求 |
| SPI TX | 3 | DMAC | N/A | 3 | SPI TX FIFO 有空位请求 |
| I2C RX | 4 | DMAC | 4 | N/A | I2C 字节接收完成请求 |
| I2C TX | 5 | DMAC | N/A | 5 | I2C TX FIFO 空请求 |



**握手接口映射配置寄存器说明：**



**CHx_CFG_L 寄存器 - SRC_PER 字段：**



SRC_PER 字段 Assigns a hardware handshaking interface (0 - DMAX_NUM_HS_IF-1) to the source of Channelx if the CHx_CFG.HS_SEL_SRC field is 0; otherwise, this field is ignored.



**注意：** For correct DW_axi_dmac operation, only one peripheral (source or destination) should be assigned to the same handshaking interface.



**SRC_PER 就是 DMA 通道到源外设握手接口的 "接线开关"：**

你选一个接口号 → 这个通道就和对应外设的 req/ack 线连上了。

接口数量决定了这个字段的位宽，最多支持 DMAX_NUM_HS_IF 个外设同时握手。

注意，多个外设可以使用同一 HS_IF，但同一时间只能一个通道使用



**注意事项汇总表：**



| 注意事项 | 说明 |
|----------|------|
| 1. 握手信号时序 | dma_req/dma_single assertion 后必须保持到 dma_ack 到来 |
| 2. 跨时钟域处理 | 外设时钟与 DMAC 时钟不同步时，需使能 DMAX_ASYNC_HS_EN |
| 3. Burst 长度选择 | 根据外设 FIFO 深度选择，通常 FIFO 深度的 1/2 或 1/4 |
| 4. Flow Controller 选择 | 固定长度传输用 DMAC 流控，可变长度用外设流控 |
| 5. 握手接口复用 | 多个外设可共享同一 HS_IF，但同一时间只能一个通道使用 |
| 6. Single Transaction Region | 块传输的尾部区域，剩余数据不足一次 Burst 时使用单次传输 |



### 总结

**握手信号常见问题解答：**

| 问题 | 答案 |
|------|------|
| 握手信号是用来做 Flow Control 的吗？ | 是的，核心目的就是流量控制 |
| 软件需要做什么配置？ | 配置 Flow Controller 角色、握手类型、握手接口映射、Burst 长度 |
| 硬件设计要注意什么？ | dma_req/dma_single/dma_last assertion 后必须保持到 dma_ack 到来 |
| 两种 Flow Controller 模式的区别？ | DMAC 流控 = 固定长度传输；外设流控 = 可变长度传输 |
| Single Transaction Region 是什么？ | 块传输的尾部区域，剩余数据不足一次 Burst 时使用单次传输 |

## 原型验证CASE列表
