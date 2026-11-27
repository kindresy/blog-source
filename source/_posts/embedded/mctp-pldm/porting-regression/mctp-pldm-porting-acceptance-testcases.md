---
title: MCTP/PLDM 移植验收：Host 与 Target 的七项功能测试
date: 2026-11-27 15:45:00
permalink: 2026/11/27/mctp-pldm-porting-acceptance-testcases/
categories: ["嵌入式开发", "MCTP 与 PLDM", "移植与回归"]
tags: ["mctp", "pldm", "i3c", "linux", "baremetal", "testing", "tutorial"]
toc: true
max_depth: 3
description: 按 Host 和 Target 两侧整理七项功能验收用例，并列出当前对 MIPI、DMTF 协议规范及常见管理工具的实现范围、验证证据和支持边界。
---

前面的[架构文档](/2026/11/27/mctp-pldm-porting-software-architecture/)说明了两端移植了什么。接下来需要回答一个更实际的问题：拿着现在这套软件，怎样判断主要功能已经可以用了？

这一轮先收住范围，检查七件事：设备能接入，管理能力能查询，短消息能往返，69B 边界能收发，长消息能重组，错误能正确处理，异常之后还能继续通信。先把这些做扎实，再考虑传感器、固件升级和更复杂的拓扑。

**下面是待执行的验收设计，所有用例的本轮结果均为“未执行”。** 历史上通过的查询、新工具的离线检查和板端安装，分别保留各自的结论，不提前填进这张表。

<!-- more -->

需要在回归时填写结果，可以下载 [TESTCASE 表格 CSV](/2026/11/27/mctp-pldm-porting-acceptance-testcases/testcases.csv)。CSV 使用 UTF-8 BOM，便于直接用表格软件打开；字段与下方验收表对应。

## 1. 先确定本轮测什么

当前对象是单 Host、单 Target，使用 I3C 承载 MCTP，管理业务为 PLDM Base。Host 运行 Linux MCTP 核心和 I3C binding；Target 运行裸机端点组件，由平台代码衔接 I3C 驱动。

| 项目 | 当前约定 | 验收前需要确认 |
|---|---|---|
| Host 内核 | `6.6.36-slim-mctp` | 运行版本与已部署模块匹配 |
| MCTP 地址 | network 1、Host EID 8、Target EID 9 | 两端实际配置一致，路由和 PID 邻居有效 |
| PLDM 标识与版本 | Target TID 1、Base 1.1.0 | 按本轮 Target 固件确认；配置变更时更新预期值 |
| 单包边界 | MCTP 包 68B；I3C 数据含 PEC 为 69B | 短包按实际长度传输，不补齐到 69B |
| 消息范围 | Target 交付组件配置最大消息 512B | 配置上限不等于已经完成对应长度的实板验收 |
| Host 工具 | `mctp`、原 `mctp-pldm-request`、新独立版 `pldmtool` | 保存实际二进制 SHA256，区分工具版本 |
| Target 操作 | 由 Target 操作者执行准备、状态查看和恢复 | 保存本轮固件版本与日志，不假设存在尚未实现的命令 |

这份验收表不包含 PLDM Type 2 传感器、Type 5 固件更新、SPDM、桥接和多端点管理。Host 工具有某个查询入口，不代表 Target 已经支持对应业务。

11 月 26 日已有七项只读查询通过记录，但连续两包的第二包地址 NACK 尚未在那轮完成解决，详见[当日回归记录](/2026/11/26/i3c-mctp-regression-milestone-20261126/)。本轮应按实际源码、固件和工具重新记录结果。

## 2. 七项主要功能的 TESTCASE 表格

表中的“可安排”只说明有现成入口，不表示现在就可以直接发包。每次仍需先检查环境，再等待 Target 操作者确认准备完成。

| 用例 | 主要功能 | Host 准备与操作 | Target 准备与操作 | 预期结果与通过判据 | 执行入口与本轮结果 |
|---|---|---|---|---|---|
| TC-01 | 启动、枚举与绑定 | 使用冻结版本的内核和模块，按已确认流程完成启动与枚举；检查 MCTP 接口、network、本地 EID、route 和 PID 邻居；记录真实枚举日志 | 进入 MCTP 管理模式，准备身份、接收和 IBI；确认 DCR、IBI/MDB、MRL/MWL 等能力符合 binding 要求，EID 与 Host 一致 | 枚举成功，Target 绑定到 MCTP 驱动；接口、身份与配置吻合，没有绑定或能力检查错误。只有本地表项、没有枚举证据时，不能填写“枚举通过” | 沿用既有启动流程与本地检查入口；未执行 |
| TC-02 | 管理能力查询 | 串行执行 3 项 MCTP Control 查询和 4 项 PLDM Base 查询，保存请求、响应和退出码；Base 可用两个 requester 对照 | 保持管理主循环工作，接收请求、处理对应 handler，准备响应并发送 IBI；记录实际请求与返回内容 | Control 的 EID、版本、消息类型正确；PLDM 的 TID、Type、版本与命令位图符合当前固件，版本 CRC 正确；各条正常查询成功完成 | 两类 requester 已有对应入口，可安排；未执行 |
| TC-03 | 短消息双向往返 | 选择 GetTID 等短查询，核对真实发送和接收长度，比较解码结果；请求完成后再发一笔正常查询 | 按响应实际长度准备 TX，完成一次 IBI 通知及 Host 取回，释放本次响应，再接收下一请求 | 短包没有被当作满包读取，没有错误补齐、旧数据或 PEC 错误；第一笔响应内容正确，下一笔仍能完成 | 用已有短查询入口；长度判断结合双端日志，必要时抓波；未执行 |
| TC-04 | 69B 单包边界收发 | 分别安排 Host→Target 满包与 Target→Host 满包，使用可逐字节比较的测试内容；记录实际长度、数据和 PEC 处理结果 | 准备可观测的满包接收与满包响应，接收、取走及清理均按真实长度处理；操作员记录数据或等价校验结果 | 两个方向均能完整传递 69B I3C 数据区，长度和内容一致，PEC 正确，无截断或缓冲区越界；只看到 Host 发送成功不足以通过 | 需要专用测试消息/响应入口，普通 Base 查询不能代替；未执行 |
| TC-05 | MCTP 分包与重组 | 至少安排一条需要两包的上层消息，并反向安排一条分包响应；核对 SOM/EOM、序号及最终重组内容，完成后再发 GetTID | 为两包连续接收和完整消息处理做好准备，确认只在重组完整后交给上层；反向按序排队响应片段并完成清理 | 两方向都能按约定时序完成分包、重组，最终数据一致，无重复交付、残留片段或卡死；之后正常查询仍可完成 | 需要配套长消息测试入口；历史第二包 NACK 需在本轮核对；未执行 |
| TC-06 | 异常处理 | 发送一条格式有效但未支持的 PLDM Base 命令，核对错误响应；随后立即执行正常 GetTID。超时分支在具备受控暂停响应入口后另记一轮 | 对不支持的命令返回对应错误，不误改状态；继续处理下一请求。超时分支由操作者控制暂停与恢复响应 | 不支持命令得到预期 completion code；工具报告远端失败，后续正常查询成功。超时时能在期限内退出，不无限等待、不自动重发；恢复响应后通信正常 | 不支持命令可用 raw 入口；受控超时分支需 Target 配合，分别记录；未执行 |
| TC-07 | 连续运行与异常恢复 | 先串行完成 10 轮基础查询，不并发堆请求；再分别安排 Target 重启恢复、Host 重启恢复，按既有流程检查/重建枚举与本地配置，重跑基础查询 | 连续处理请求并清理 RX/TX/IBI 状态；按操作者确认的步骤重启、重新进入管理模式，准备接收并恢复地址配置 | 10 轮结果稳定，没有遗留响应或资源持续占用；每个重启场景重新准备后都能完成基础查询；恢复过程可重复，不靠随机重试才能碰巧成功 | 连续查询入口可安排；重启场景按独立轮次操作和归档；未执行 |

TC-06 中的错误请求，**工具退出非零反而可能是正确结果**。例如设备明确返回“不支持命令”，我们要检查的是它是否按协议拒绝，以及下一笔正常请求是否还能完成。

TC-04、TC-05 的入口还需要配套准备。没有能产生满包响应或长消息的 Target 测试 handler，就先标为“待入口”，不要拿短查询成功顶替这两项。

## 3. 两侧怎样准备，在哪里停下来等待

| 阶段 | Host 侧 | Target 侧 | 继续条件 |
|---|---|---|---|
| 固定版本 | 记录仓库 commit、未提交差异、内核、模块与工具指纹 | 记录实际固件版本、构建配置和消息容量 | 两端版本能对应到本轮产物 |
| 本地检查 | 查看接口、地址、路由、邻居和当前计数；只执行 show、help 不发管理请求 | 查看准备状态，避免状态查询误消费接收数据 | 现有配置没有明显冲突 |
| 进入管理模式 | 停止可能争用同一 Target 的旧测试或轮询程序 | 进入 MCTP 模式，准备 RX、主循环、响应和 IBI；确认实际 EID | **Target 操作者确认准备完成后，Host 才发请求** |
| 执行一项用例 | 发出该项请求，收集结果；失败就停止本项，不自行复位或反复重发 | 配合记录接收、响应、IBI 和清理结果 | 响应和双端状态符合该项判据 |
| 归档 | 保存命令、退出码、响应字段、驱动日志和工具 SHA256 | 保存同轮固件身份、状态日志或截图 | 两端证据齐全后再填写结果 |

没有现成计数器时，可以使用等价日志或操作员截图。不要因为架构图里画了 RX/TX 生命周期，就假设固件已经暴露全部观测字段。

## 4. 基础查询的实际命令

### 4.1 原 requester 执行 MCTP Control

在原工具目录执行。这里使用当前实验的 network 1、Target EID 9；不是通用固定地址。

```bash
./mctp-pldm-request control 1 9 get-eid
./mctp-pldm-request control 1 9 get-version
./mctp-pldm-request control 1 9 get-types
```

它们分别查询 MCTP EID、版本支持和消息类型。Control 的 `get-types` 与下面 PLDM 的 `GetPLDMTypes` 属于不同协议层。

### 4.2 新 pldmtool 执行 PLDM Base

工具已经安装在 Host 的独立目录。先进入对应 `bundle`：

```bash
cd /opt/frdm/tools/pldmtool-20261127/bundle

./pldmtool base GetTID -N 1 -m 9 --timeout-ms 30000 -v
./pldmtool base GetPLDMTypes -N 1 -m 9 --timeout-ms 30000 -v
./pldmtool base GetPLDMVersion -N 1 -m 9 -t base --timeout-ms 30000 -v

# 仅在上一步确认 Base 1.1.0 后，使用这个版本字段
./pldmtool base GetPLDMCommands -N 1 -m 9 -t base \
    -d 0x00 0xf0 0xf1 0xf1 --timeout-ms 30000 -v
```

| 参数或结果 | 解释 |
|---|---|
| `-N 1` | 本独立适配版增加的 MCTP network 参数 |
| `-m 9` | 目的 EID，不是 I3C 动态地址或 PLDM TID |
| `-t base` | 查询 PLDM Base Type |
| `-d 00 f0 f1 f1` | Base 1.1.0 的版本字段，顺序为 alpha、update、minor、major |
| `--timeout-ms 30000` | 等待响应的期限为 30 秒，不自动重发 |
| `-v` | 输出 PLDM TX/RX；日志没有额外拼接 MCTP 类型字节 |
| 退出 `0` | 所选命令收到成功响应且解析通过，仍需比较字段 |
| 退出 `1` / `2` / `3` | 分别为本地或解析错误、远端 completion code 非零、等待超时；参数错误也可能有其他非零码 |

当前 Target 预期 TID 为 1、Base 版本为 1.1.0、Base 命令位图包含 `0x01`～`0x05`。若固件变更，以本轮确认的能力为准，不能把历史预期当作新固件的实际返回值。

这套 `pldmtool` 是固定 OpenBMC 版本的独立 Host 适配，直接连接 AF_MCTP，不依赖板上运行 `pldmd` 或 D-Bus。它已经完成原生与 AArch64 仿真各 36 项离线检查，以及板端校验和 help 启动检查；这些结果没有替代本文的硬件用例。

### 4.3 一次保存四项 Base 查询

在同一 `bundle` 内，Target 准备完成后，可用随包脚本代替逐条输入：

```bash
bash run-base-regression.sh 1 9 regression-run01
```

脚本拒绝覆盖旧日志目录，保存环境、工具指纹、TX/RX 与退出码。版本不是 1.1.0 时，它不会继续发送固定版本的 GetPLDMCommands；任何查询失败也会停止。

也可以下载[本次回归脚本](/2026/11/27/mctp-pldm-porting-acceptance-testcases/run-base-regression.sh)。它只覆盖 TC-02 的 PLDM Base 部分，不能替代整张验收表。输出 `QUERIES_COMPLETED` 之后，还要核对字段并保存 Target 同轮日志。

### 4.4 一条可明确判断的异常请求

当前 Target 未支持的 Base 命令 `0x7f` 可用于拒绝处理检查。该命令会实际发包：

```bash
./pldmtool raw -N 1 -m 9 -d 0x80 0x00 0x7f -v
echo $?
```

`80 00 7f` 是 PLDM 请求头，分别对应请求标志及实例字段、Type 0、命令 `0x7f`；工具会设置实际 Instance ID。预期设备返回“不支持命令”完成码 `0x05`，该适配工具应退出 `2`。随后执行一次正常 GetTID，确认错误没有影响后续请求。

raw 的具体作用由输入内容决定，不能把所有 raw 请求都当成只读。这里仅使用这一条未知命令，不在默认脚本里发送设置或更新请求。

## 5. 69B、分包和短消息要分别记录

本轮约定的 69B 是 **I3C 数据区：68B MCTP 包，加 1B PEC**，不包含地址/RW 阶段。

```text
单个满包：4B MCTP 头 + 64B MCTP payload + 1B PEC = 69B
首包 payload：1B Message Type + 最多 63B 上层消息
```

因此，普通 GetTID 这样的短查询不会自然形成满包。它能验证短消息往返，但不能证明满包 TX/RX、容量边界或长消息重组已经通过。

一个可以用于 TC-05 的最小两包例子是 **64B 上层消息**：Message Type 只在首包出现，第一包承载前 63B，第二包承载剩余 1B。按当前 binding，两个 I3C 数据区分别是 69B 和 6B。最终应检查一条完整的 64B 上层消息，而不是把两次 I3C 收包各当作一条业务消息。

上层消息长度包含该层自己的头；如果测试内容采用 PLDM，不能把 PLDM 头再算到这 64B 之外。具体字节模式、handler 行为和反向长响应，应在执行前固定。

历史上加约 1 秒间隔后，Host 两包发送成功，只是定位线索。它没有同时证明 Target 重组内容正确，也没有证明反向分包响应完成。TC-05 要按双方约定的正常时序、两端内容和下一笔请求是否仍可完成来判定。

Host 的 `sendto()` 返回值也不是全部证据：实际 I3C 发送可能在后续线程执行，地址 NACK 可能稍后才出现在驱动日志或计数中。

## 6. 命令、内部动作与总线行为

| 操作 | 软件内部发生什么 | 是否产生总线事务 |
|---|---|---|
| `mctp … show` | 通过 rtnetlink 查看 Host 本地表项 | 不向 Target 发管理请求 |
| `pldmtool … --help` | 解析并显示工具参数 | 不创建管理请求 |
| 一笔 Base 查询 | 上游前端经 libpldm 编码，AF_MCTP 发送；内核路由和必要分包；binding 添加 PEC | Host Private Write → Target 处理 → IBI `0xAE` → Host Private Read |
| 接收响应 | binding 按实际长度检查 PEC，内核重组，工具检查来源 network/EID、类型、方向、Instance ID 和命令，最后解码 | 读取本身是实际事务；用户态打印 JSON 不再发请求 |
| 受控重启恢复 | 操作者触发重启，重新准备管理模式、枚举和配置 | 会改变硬件状态；应按独立轮次安排，不能在失败后悄悄自动执行 |

线上具体 START、Repeated START、STOP、地址/RW、ACK/NACK 和读数据 T-bit，应结合本轮驱动日志及必要波形确认。CLI 响应正确可以作为功能证据，不能据此补写尚未取得的时序测量结论。

## 7. 回归结束时怎样填写结果

结果使用下面四种状态，避免把“还没做”和“确实失败”混在一起：

| 状态 | 填写条件 |
|---|---|
| PASS | 本用例的双端准备、操作和判据都完成，证据对应同一轮版本 |
| FAIL | 已执行，出现与预期不符的结果；记录停在哪一步 |
| PENDING | 尚未执行，或其中一个方向、一个恢复场景还没做完 |
| 待入口 | 所需测试 handler、注入或观测入口尚未准备好 |

每轮至少留下用例编号、双端版本、实际配置、Host 命令与退出码、响应字段，以及对应的 Target 日志或截图。出现超时、PEC 错误、地址 NACK 或异常状态时，先停下来保存现场，再决定下一步。

源码合入 master、重新构建、部署到板上和实际回归是四件事。即使工具已经安装、查询曾经通过，也要把这轮 testcase 的结果单独填清楚。

## 8. 当前能力与标准支持清单

如果现在有人问“你们已经支持哪些业界协议，可以接什么应用”，我会先回答：**我们已经用标准协议完成了一组基础管理查询，正在补齐通信边界和恢复验收；目前能交付的业务范围仍是设备识别与能力发现。**

下表按 2026 年 11 月 27 日的源码、产物和已有日志整理。“标准”指 MIPI、DMTF 发布的规范；Linux API、OpenBMC 工具和 NVIDIA 工具分别注明来源，不把社区软件或厂商协议统称为国际标准。表中的规范用于说明功能归属，除明确写出的实现版本外，不代表工程已符合链接版本的全部条款。

### 8.1 先看支持程度，再看协议名称

| 支持程度 | 在本文中代表什么 | 当前例子 |
|---|---|---|
| 局部功能已有实板证据 | 指定版本和配置下，真实 Host 与 Target 完成过对应交互 | 原 requester 的 3 项 Control、4 项 PLDM Base 查询 |
| 已实现，尚缺实板验收 | 源码或离线测试中有该能力，不能沿用其他用例的 PASS | Target SetTID；MCTP 分包重组的组件实现 |
| 工具已部署，实链路待回归 | 程序能在板端启动，相关编码解析有离线检查；尚未用它对当前 Target 发请求 | 新独立版 `pldmtool` |
| 本链路未集成 | 尚无对应业务 handler、服务或端到端流程 | 传感器管理、固件更新、Redfish、SPDM、NSM |

这里不填写“标准支持百分比”。没有先固定规范版本、角色、必选条款和测试集合，百分比没有可比较的含义；实现几个命令也不等于完成一致性认证。

### 8.2 协议、规范及我们实际做到的位置

| 协议或功能 | 标准组织与规范 | 当前实现范围 | 能验证到什么程度 | 还不能据此宣称什么 |
|---|---|---|---|---|
| I3C SDR 基础通信 | MIPI：[I3C / I3C Basic](https://www.mipi.org/specifications/i3c-sensor-specification) | 当前管理链路使用枚举、Private Transfer、IBI 和相关 CCC；Host 为 Controller，Target 为目标设备 | 历史查询已经实际经过这条 I3C 链路；TC-01、TC-03 用于本轮复核 | 没有据此完成整个 I3C 规范、电气时序、HDR 或多 Controller 的一致性验证 |
| MCTP 基础传输与 Control | DMTF：[DSP0236](https://www.dmtf.org/sites/default/files/standards/documents/DSP0236_1.3.3.pdf) | Host 内核 MCTP、Target libmctp；已有 EID、消息类型、Tag、包头与分包重组实现，当前按固定 EID 手工配置 | GetEID、GetVersion、GetMessageTypeSupport 有实板结果；连续分包、恢复等仍按各用例验收 | 不能写成完整 MCTP 功能通过；没有部署自动 bus-owner 管理，也没有完成桥接、多端点或跨网络互通 |
| MCTP over I3C | DMTF：[DSP0233 1.0.0](https://www.dmtf.org/sites/default/files/standards/documents/DSP0233_1.0.0.pdf)；这是当前回移驱动注释引用的版本 | Linux v6.8 binding 回移到当前 6.6.36 工程；使用软件 PEC、实际长度和 MDB `0xAE` 通知；本轮固定 68B MTU / 69B 含 PEC 数据区 | 单包查询链路已有证据；TC-04、TC-05 专门检查满包及分包 | 69B 是当前 binding 的容量边界，不是所有业务消息的固定长度；不能宣称已完成该规范的全部发现、协商与异常条款验证 |
| PLDM over MCTP | DMTF：[DSP0241 1.0.0](https://www.dmtf.org/sites/default/files/standards/documents/DSP0241_1.0.0.pdf) | 使用 MCTP Message Type `0x01` 传递 PLDM 请求和响应，当前端点路径使用 IC=0 | 四项 Base 查询已有实板结果；新工具的 AF_MCTP 封装与匹配经过离线检查 | 不代表所有 PLDM Type、所有消息长度和所有端点均已互通 |
| PLDM Base / Type 0 | DMTF：[DSP0240 1.1.0](https://www.dmtf.org/sites/default/files/standards/documents/DSP0240_1.1.0.pdf) | 当前 Target handler 实现 SetTID、GetTID、GetPLDMTypes、GetPLDMVersion、GetPLDMCommands；返回 Base 1.1.0 | 四项只读查询有历史实板结果；SetTID 有组件实现和离线测试，尚不记为本轮实板通过 | 不能用命令位图代替逐项验证；没有据此实现新版本的全部协商和 PLDM 跨命令 multipart 功能 |
| PLDM 平台监控 / Type 2 | DMTF：[DSP0248](https://www.dmtf.org/sites/default/files/standards/documents/DSP0248_1.3.0.pdf) | 当前链路未集成 PDR 仓库、真实 sensor / effecter provider 和事件业务 | 可以据 Target 的类型声明判断该业务不在当前范围；还不能读取真实温度、功耗或控制执行器 | 不能说已实现服务器级传感器监控、功耗控制或告警 |
| PLDM FRU / Type 4 | DMTF：[DSP0257，见规范目录](https://www.dmtf.org/standards/published_documents) | 当前链路没有 FRU 记录表服务 | 尚无板卡序列号、部件号等 FRU 业务查询结果 | 不能把 EID、TID 或芯片 PID 当成已经实现 FRU 资产管理 |
| PLDM Firmware Update / Type 5 | DMTF：[DSP0267](https://www.dmtf.org/sites/default/files/standards/documents/DSP0267_1.2.0.pdf) | Host `pldmtool` 有 QueryDeviceIdentifiers、GetFirmwareParameters、GetStatus 查询前端；当前 Target 没有 Type 5 handler，双方没有完整更新流程 | Host 的查询编码和模拟响应解析可离线验证；没有对当前 Target 的 Type 5 成功互通记录 | 不能说已经支持刷固件；镜像匹配、拉取、校验、激活、取消和失败恢复均未交付 |
| PLDM RDE / Type 6 | DMTF：[DSP0218](https://www.dmtf.org/sites/default/files/standards/documents/DSP0218_1.2.0.pdf) | 当前链路未集成设备资源模型、字典与 RDE 操作 | 没有设备通过 RDE 向 BMC 提供 Redfish 资源的结果 | 不能将 PLDM Base 查询成功写成 Redfish Device Enablement 已完成 |
| SPDM 认证与度量 | DMTF：[DSP0274 / SPDM](https://www.dmtf.org/standards/spdm) | 当前链路没有 requester / responder、证书或度量流程 | 尚无本设备的身份认证、度量或会话验证结果 | PEC、版本 CRC32 都不是设备身份认证，也不能据此宣称安全会话已建立 |
| Redfish 管理 API | DMTF：[DSP0266 / Redfish](https://www.dmtf.org/standards/redfish) | 当前工程没有把这张 Target 的资产、传感器或更新能力接入 Redfish 服务 | 没有本设备对应的 Redfish 管理资源可用于验收 | 不能因为 Host 能运行 Linux 工具，就把它视为已经具有完整 BMC 北向管理接口 |
| 其他 MCTP binding | DMTF：SMBus/I²C DSP0237、PCIe VDM DSP0238、USB DSP0283；见[规范目录](https://www.dmtf.org/standards/published_documents) | 本轮实际集成与验证对象只有 I3C | 这些通道需各自的硬件连接、驱动和配置，再单独验证 | 不能把 I3C 查询结果外推为 SMBus、PCIe VDM 或 USB 兼容 |
| NVIDIA NSM 遥测 | NVIDIA：[NSM 开源实现](https://github.com/NVIDIA/nsmd)，属于厂商协议，不是 PLDM Type 2 | 当前 Host/Target 链路未集成 NSM handler 或 `nsmd` | 尚无本设备的 NSM 发现和遥测结果 | MCTP 能承载厂商消息，不代表当前 Target 已能响应 NVIDIA GPU 管理命令 |

上表中最容易混淆的是 Type 5：**我们能准备标准查询请求，但当前设备没有提供相应业务。** 这与“已经能给当前设备升级固件”之间，还隔着完整的双向更新状态机和实际固件存储、激活、恢复实现。

### 8.3 常见 App、服务和库的支持程度

下面把可运行的工具、常驻服务和底层库分开标明。工具的参数与源码来自开源项目，不属于 DMTF 强制规定的统一 CLI；没有安装某个工具，也不直接等于协议不符合标准。

| App / 组件 | 来源与类别 | 当前工程的支持程度 | 现在可以怎样验证 | 需要注意的范围 |
|---|---|---|---|---|
| `mctp` | [CodeConstruct/mctp](https://github.com/CodeConstruct/mctp)，社区命令行工具 | 已集成，当前使用固定版本 `34900256b183811b25ff4046aa9702cef54befc9` | 查看 link、address、route、neigh；已有本地配置和实际查询链路记录 | show 查看的是本地状态，不替 Target 设置 EID，不是完整端点管理服务 |
| `pldmtool` | [OpenBMC pldm](https://github.com/openbmc/pldm)，社区 PLDM requester | OpenBMC 2.14 固定基线的独立 Host 适配已安装；包含 Base 四项查询、Type 5 三项信息查询与 raw | 原生及 AArch64 仿真各 36 项离线检查通过；板端文件校验和 help 通过；真实查询回归待执行 | 这是明确裁剪的适配版，不是最新版完整前端；不包含 Type 2 前端、常驻管理或升级流程 |
| `mctp-pldm-request` | 本项目自研测试工具，不是行业通用 CLI | 已集成并用于历史联调，支持所列 Control 和 Base 请求 | 11 月 26 日七项只读查询有实板日志，可与新工具串行对照 | 当前 Base GetCommands 固定查询 1.1.0；不是通用的全部 PLDM 业务客户端 |
| Linux `AF_MCTP` | [Linux 内核接口](https://www.kernel.org/doc/html/latest/networking/mctp.html)，不是一个 App | 当前 Host 内核已恢复该接口，工具直接使用它收发 | 已有 socket、路由和真实查询记录；新工具的 socket 契约有离线检查 | 需要对应 binding、路由、邻居和 Target 准备；socket 创建成功不能证明对端可达 |
| `libmctp` / `libpldm` | [OpenBMC libmctp](https://github.com/openbmc/libmctp)、[libpldm](https://github.com/openbmc/libpldm)，协议实现库 | Target 使用 libmctp 与 Base codec；Host requester 使用 libpldm，Host 的 MCTP 核心在内核中 | 可核对固定源码、组件测试以及已走通的 Base 数据 | Host 没有因此部署用户态 libmctp；库中存在其他类型的编解码，不等于平台已有对应 handler 或业务 provider |
| `mctpd` | [CodeConstruct/mctp](https://github.com/CodeConstruct/mctp)，端点管理服务 | 当前 Host 未部署 | 现阶段使用固定 EID 和手工拓扑配置 | 尚无自动 bus-owner 发现、地址管理与服务级恢复结果 |
| `pldmd` | [OpenBMC pldm](https://github.com/openbmc/pldm)，常驻 PLDM 服务 | 当前 Host 未部署；独立 `pldmtool` 不依赖它 | 目前使用一次请求、一次等待的手动工具 | 没有常驻设备发现、传感器轮询、D-Bus 资产发布或更新代理服务 |
| `bmcweb` / Redfish 客户端 | [OpenBMC bmcweb](https://github.com/openbmc/bmcweb) 提供服务；HTTP 客户端用于访问服务 | 当前链路没有集成本设备的 Redfish 服务端 | 后续需先有设备资源和实际 API，再用客户端验收 | 客户端能发 HTTP 请求，与设备是否提供 Redfish 是两件事 |
| `ipmitool` | [ipmitool 项目](https://github.com/ipmitool/ipmitool)，IPMI 客户端 | 当前 Target 链路没有 IPMI 管理服务或适配 | 不能直接用它替代当前 PLDM Base 验收 | IPMI 是另一套管理接口，安装客户端不会让 MCTP/PLDM 设备自动成为 IPMI 设备 |
| `nsmd` / `nsmtool` | [NVIDIA/nsmd](https://github.com/NVIDIA/nsmd)，厂商管理服务与客户端 | 当前工程未集成 | 等 Target 真正实现 NSM 并接入服务后，再安排互通 | 不能把 NVIDIA 工具的可用性当作通用 PLDM 兼容判据 |

### 8.4 这些结论可以回查什么

“标准定义了什么”和“我们实际做到了什么”使用不同证据：前者看上表的官方规范，后者看对应源码、产物和运行记录。

| 当前结论 | 可回查的依据 | 它证明到哪里 |
|---|---|---|
| I3C binding、地址和路由已建立过 | [绑定与本地配置日志](/2026/11/27/mctp-pldm-porting-software-architecture/evidence/mctp-binding-and-route.log) | 当时配置下的绑定与本地状态，不是完整标准认证 |
| 原 requester 的 Control / Base 查询能往返 | [GetEID](/2026/11/27/mctp-pldm-porting-software-architecture/evidence/get-eid.log)、[GetTID](/2026/11/27/mctp-pldm-porting-software-architecture/evidence/03-base-get-tid.log)、[版本与 CRC](/2026/11/27/mctp-pldm-porting-software-architecture/evidence/05-base-get-version.log)、[Base 命令位图](/2026/11/27/mctp-pldm-porting-software-architecture/evidence/06-base-get-commands.log) | 已记录的具体查询；位图声明包含 SetTID，不等于 SetTID 已完成实板测试 |
| 分包还需单独验收 | [连续分包原始记录](/2026/11/27/mctp-pldm-porting-software-architecture/evidence/fragment-no-delay.log) | 历史第二包 NACK 的实际停点，不能沿用查询 PASS 覆盖它 |
| 新 pldmtool 已完成集成准备 | [集成验证摘要](/2026/11/27/mctp-pldm-porting-acceptance-testcases/pldmtool-validation-summary.txt) | 固定版本、离线检查、板端安装与 help；摘要明确未做真实 Target 查询 |

对外介绍时，我会使用这样的表述：**当前已打通基于 Linux AF_MCTP 的 MCTP over I3C 基础管理链路，完成过三项 MCTP Control 和四项 PLDM Base 查询；新工具已部署，完整通信验收仍按本文用例推进。**

等后续补上真实传感器、PDR、更新状态机或 Redfish 服务，再逐项更新表格。没有新的实现和同轮证据，就不把支持范围往上加。
