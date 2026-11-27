---
title: "PCIe 错误与 AER：从事务失败到系统恢复"
date: 2026-11-27
updated: 2026-11-27
description: "区分链路、事务和设备错误，沿 Completion Status、Header Log 与恢复入口查找原因。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-error-model-aer/"
---

## 1. “PCIe error”不是一种错误

PCIe 把多个可靠性层叠起来，因此错误也按发生位置和影响范围分层：

```text
Physical:    receiver/framing/block alignment/equalization
Data Link:   bad LCRC/DLLP、replay timeout/rollover、flow-control protocol
Transaction: UR、CA、Unexpected Completion、Malformed、Poison、Completion Timeout
Port/Link:   Surprise Down、DPC
Controller:  FIFO overflow、ECC、internal parity、AXI response
```

先辨认是谁检测到了什么错误、哪笔事务受影响，再看 AER 有没有把这件事记录和上报。

## 2. Correctable、Non-Fatal、Fatal

| 分类 | 协议含义 | 典型例子 | 常见恢复粒度 |
|---|---|---|---|
| Correctable | 硬件恢复，无信息丢失；也含部分 advisory 情形 | Receiver Error、Bad TLP 后 replay | 记录/计数，通常无需 driver recovery |
| Uncorrectable Non-Fatal | 某笔 transaction 不可靠，但 Link 可继续工作 | UR、CA、Completion Timeout（默认常为 non-fatal） | 终止/重试业务，可能 Function recovery |
| Uncorrectable Fatal | Link/相关硬件已不可信 | Receiver Overflow、严重 protocol/internal error（依 severity） | containment、reset、重建 hierarchy |

Fatal/Non-Fatal 是 PCIe severity，最终平台动作仍可由 firmware/OS policy 决定。AER 的 Uncorrectable Error Severity register 允许逐 error 选择 fatal/non-fatal；baseline error reporting 没有这个细粒度。

偶尔出现 Bad TLP 并成功重传，与同一链路持续累积几千次 Correctable Error，值得追查的程度不同。保留时间区间和计数变化比单截一行日志更有价值。

来源：Base Spec 5.0 §§6.2.1–6.2.2，printed/PDF pp.505–507。

## 3. 三种互补的错误传播方式

### 3.1 Completion Status：告诉原 Requester 这笔请求失败

对 Non-Posted Request，Completer 可返回：

- **UR**（Unsupported Request）：请求类型/地址/属性不受支持，或没有合适 Completer；
- **CA**（Completer Abort）：Completer 接受到了请求，但内部原因无法完成；
- **CRS**：Configuration Request Retry Status，Function 暂未准备好；
- **SC**：Successful Completion，不是错误。

Requester 用 Requester ID + Tag 关联原请求。Completion Status 是 transaction-local 的结果，不等同于已经向 Root Complex 上报 error event。

### 3.2 ERR_COR / ERR_NONFATAL / ERR_FATAL：向 Root Complex 报事件

这是路由到 Root Complex 的 Message TLP。Root Port 根据 Requester ID 记录 source，把它转成平台事件/中断。AER mask、Device Control reporting enable、Root Error Command 等共同决定是否发送/是否中断。

一次配置读得到 UR，有时只是软件扫到了一个空 Function。Role-Based Error Reporting 对部分此类错误规定了 Advisory Non-Fatal 的处理方式。因此，给请求者的失败 Completion 与向 Root Port 报告的严重级别，要分开看。

### 3.3 Poison：数据继续走，但标记“不可信”

TLP header 的 `EP` bit 是 Error Poisoned。它允许系统把带错误的数据沿 transaction 路径转发，由最终消费者处理/报告。Poison 不是 Endpoint 的缩写，也不意味着 packet 的 LCRC 一定错；LCRC 保护“传输是否改变”，Poison 表示“payload 在更早处就已知不可靠”。

来源：Base Spec §§6.2.3–6.2.3.3，printed/PDF pp.507–513。

## 4. 几个驱动最常遇到的错误

### 4.1 Unsupported Request（UR）

UR 表示没有设备能按所请求的方式完成访问。配置软件扫描空的 BDF、驱动读到未实现的窗口，都可能走到这条路径。

常见原因：

- 访问未实现 BAR/offset 或桥 window/routing 配错；
- Function 不支持某 TLP/type/AtomicOp；
- 配置访问不存在的 BDF；
- Memory Space Enable/目标 decode 条件不满足；
- Link down/DPC 边界上的 request 被 Port 以 UR 终止。

对需 Completion 的 request 返回 Cpl(UR)；对 Posted request 无 Completion，只能记录/上报 UR。

### 4.2 Completer Abort（CA）

Completer 已经接收这笔请求，但自身遇到异常，无法完成。对一笔 Memory Read，读到 Cpl(CA) 至少说明收到了失败响应；要追具体原因，需回到完成方的状态和日志。

### 4.3 Unexpected Completion

收到的 Completion 找不到匹配的 outstanding Requester ID + Tag，或上下文不匹配。接收端丢弃并记录；真正等待它的 requester 可能随后 Completion Timeout。常查 tag reuse、reset/FLR/link flap、错误 routing、重复/迟到 completion。

### 4.4 Completion Timeout（CTO）

Requester 发了需 Completion 的 request，直到配置的 timeout interval 仍没收齐。大 MRd 只有部分 CplD 返回也未完成。CTO 是 **Requester Function** 的错误，而不是天然属于 Completer。

可能根因从“request 根本没出去”、远端 BAR/AXI hang、completion 返回丢失，到 async removal；timeout 值不是典型 latency 指标，不能拿它做性能规格。

来源：Base Spec §2.8，printed/PDF p.204。

### 4.5 Malformed TLP

TLP 格式或字段组合违反了接收端必须检查的协议规则。例如 Length 与实际携带的数据不符。Malformed 和链路传输中的 LCRC 错误应分别判断。

### 4.6 Surprise Down / Link Down

DL_Active 在没有被 Secondary Bus Reset、Link Disable、正常 power-off/hotplug policy 等预期条件解释时突然进入 DL_Inactive，可报告 Surprise Down。此时在途 Non-Posted request 可能 CTO，Posted request 可能静默丢失或伴随 Port-side UR reporting。

## 5. AER Extended Capability 是什么

AER（Advanced Error Reporting）位于 Extended Configuration Space。关键 register：

| Offset | Register | 核心作用 |
|---:|---|---|
| `+0x04` | Uncorrectable Error Status | 哪类 uncorrectable 已发生，RW1C |
| `+0x08` | Uncorrectable Error Mask | 屏蔽该类向 RC 报告/log，不等于阻止检测 |
| `+0x0C` | Uncorrectable Error Severity | 0=Non-Fatal，1=Fatal |
| `+0x10` | Correctable Error Status | Receiver/Bad TLP/Bad DLLP/replay/advisory 等，RW1C |
| `+0x14` | Correctable Error Mask | 逐项 reporting mask |
| `+0x18` | Capabilities and Control | First Error Pointer、ECRC、multiple-header 等 |
| `+0x1C..+0x28` | Header Log | 与 first uncorrectable error 相关的最多 16 B TLP header |
| Root only `+0x2C` | Root Error Command | correctable/nonfatal/fatal interrupt enable |
| Root only `+0x30` | Root Error Status | 收到的 ERR_* 与 multiple error 状态 |
| Root only `+0x34` | Error Source ID | 首个 correctable/uncorrectable Requester ID |

Uncorrectable bits 包括 Data Link Protocol、Surprise Down、Poison、Flow Control、CTO、CA、Unexpected Completion、Receiver Overflow、Malformed、ECRC、UR、ACS Violation、Internal Error 等。Correctable bits 包括 Receiver Error、Bad TLP、Bad DLLP、Replay Number Rollover、Replay Timer Timeout、Advisory Non-Fatal、Corrected Internal Error。

### Status、Mask、Severity 别互换

- **Status**：发生过；多数是 sticky RW1C/W1C。软件写 1 清对应 bit，写 0 不动。
- **Mask**：是否进一步 log/report；被 mask 的错误通常仍影响 status。
- **Severity**：若是 uncorrectable，按 fatal 还是 non-fatal 报。
- **Enable**：Device/Root 层还各有 class-level enable；单项没 mask 不代表一定会产生平台 IRQ。

清 AER 前先采集 Status、First Error Pointer、Header Log 和 Source ID。有些字段采用写 1 清除，先清再读就找不回第一笔错误的上下文了。

来源：Base Spec §§7.8.4.1–7.8.4.11，printed/PDF pp.842–856。

## 6. Header Log 怎样帮你还原 TLP

Header Log 保存 4 DW。按 spec 的 DWORD 读法，字段布局与 TLP header 图一致，但每个 DW 内的 byte 展示要留意 endian。解码顺序：

1. 由 Fmt/Type 判断 TLP 类型和 3DW/4DW；
2. 看 Length、TC、Attr、EP；
3. request 看 Requester ID、Tag、BE、address；completion 看 Completer/Requester ID、Tag、status；
4. 用 First Error Pointer 确认 header 对应哪个 uncorrectable status；
5. 再联系 Port 方向判断是 inbound 还是 outbound。

并非每个 error 都必有可用 header；masked error 不进入 Header Log，多个错误还受 first-error/multiple-header 规则影响。CTO 记录的是原 Request header 也需要 capability 宣告支持。

## 7. 把现场记录按观察位置排在一起

假设驱动报告一次读寄存器超时。先保留 Root Port 的 AER Status 和 Header Log，再把设备日志、链路状态与超时时间对齐。下面这些观察能帮助缩小范围：

| 观察到的现象 | 下一步核对 |
|---|---|
| 请求端先看到 Cpl(UR) | 检查所访问的地址、桥窗口、目标 Function 的地址解码 |
| 收到 Cpl(CA) | 查完成方是否已接受请求，以及处理该请求时发生了什么 |
| 有 MRd，始终没收到应得的 CplD | 核对 Requester ID、Tag、链路是否消失以及上游是否有 Completion Timeout |
| Bad TLP 计数增加，重传后业务继续 | 看哪一跳报告错误，并核对同一时段的链路速率、重传和训练记录 |
| 先触发 DPC，随后下游整片消失 | 保存 DPC 触发原因，按隔离域检查受影响的设备 |

这些观察并非一一对应。主机看到的第一条错误日志与最先发生的硬件事件，有时隔着一次重传、一次超时，甚至一次链路隔离。

## 8. Linux AER 的位置

概念路径：

```text
device/Port detects error
 → local AER status/header log
 → optional ERR_* Message upstream
 → Root Port Root Error Status/source + platform IRQ/MSI
 → Linux PCIe port service / AER service
 → print severity/source/TLP header
 → decide recovery scope
 → pci_driver.err_handler callbacks
```

支持 PCI error recovery 的 driver 通常提供 `struct pci_error_handlers`，概念阶段包括 `error_detected()`、可选 `mmio_enabled()`、`slot_reset()`、`resume()`；channel state 和各 driver vote 决定是否 reset/recover/disconnect。

Linux 要接管原生 AER，需要相应的内核配置和端口服务，ACPI 平台还涉及固件授予控制权。设备业务引擎独有的错误通常仍由功能驱动解释。

## 9. Error pollution：别让一个根因生出一串假线索

同一次掉链可能先留下端口事件，随后出现未完成读请求的超时，最后才到驱动的恢复回调。对照 BDF、错误状态和时间先后，才能把属于同一次故障的记录串在一起。

## 10. 参考资料

- PCI Express Base Specification 5.0 r1.0, §2.8–2.9, printed/PDF pp.204–207。
- PCI Express Base Specification 5.0 r1.0, §6.2, printed/PDF pp.505–533。
- PCI Express Base Specification 5.0 r1.0, §7.8.4, printed/PDF pp.841–858。
- [Linux PCIe AER 驱动指南](https://docs.kernel.org/PCI/pcieaer-howto.html)
