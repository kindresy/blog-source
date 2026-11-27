---
title: "LTR 与 OBFF：设备和平台怎样协商省电时机"
date: 2026-11-27
updated: 2026-11-27
description: "解释可容忍延迟和集中访问提示，理解整条路径上的支持条件与策略边界。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "枚举与系统流程"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-ltr-obff/"
---

## 1. 两种方向相反的功耗提示

PCIe 平台想让内存控制器、互连和 CPU 多睡一会儿，却不能把对延迟敏感的网卡/存储饿到 timeout。LTR 与 OBFF 从两边交换信息：

| 机制 | 方向 | 说的是什么 |
|---|---|---|
| LTR | Endpoint → Root Complex | “我的 Memory traffic 最多容忍这段 service latency” |
| OBFF | Root Complex → Endpoint | “现在中央资源本来就醒着，集中做 DMA/IRQ 的增量功耗较低” |

它们都是 power/performance policy 输入，不是 traffic 许可协议。LTR 不是 Completion Timeout，OBFF 也不是 Bus Master Enable。

## 2. LTR 报的是容忍上限，不是测量结果

Latency Tolerance Reporting 把 snoop 和 no-snoop traffic 分开报告。每个 16-bit 字段包含：

- Requirement：这一类 traffic 是否有要求；
- 10-bit Value；
- 3-bit Scale：1 ns、32 ns、1024 ns……逐级放大。

Requirement=1 且 Value=0 表示“任何 delay 都会影响我，请给最佳 service”；Requirement=0 表示没有这一类要求，此时 Value/Scale 忽略。LTR 并不是设备用秒表测出 RC 延迟，而是设备根据 buffer 深度、实时 deadline 和功能需求声明可接受上限。

规范定义的 service latency 尤其值得驱动工程师记住：

- Non-Posted Request：从 request 最后部分发出，到第一个 Completion 开始返回；
- Posted traffic 被 credit 卡住：从耗尽 credit 的前一包发完，到能解锁下一包的 FC DLLP 开始返回；
- 若使用 Clock PM，还要把设备拉`CLKREQ#`到有效 clock 的时间算进平台总预算。

所以 LTR 会间接影响平台是否敢进入更深省电状态，却不直接命令 Link 进入 L1，也不修改 MRRS/MPS/Completion Timeout。

## 3. LTR 是整条 hierarchy 的承诺

软件只有在 Root Complex 与路径上所有 Switch 都支持 LTR 时才可给 Endpoint enable。推荐从靠近 Root Port 的组件向下启用；disabled/unsupported Downstream Port 收到 LTR Message 要报 UR。

Multi-Function Device 把各 Function 要求合并，取最严格（最小）的 snoop/no-snoop 值。Switch 还要扣除自身引入的 latency 后再向上报告。DL_Down 会使旧值失效；Function 转到 non-D0 或 LTR 被 disable 时，若此前报告过有效 requirement，还需按规则发一条 Requirement 全清的更新，以便平台及时撤销旧的延迟要求。

Root Complex 并非绝对必须满足请求值，但规范强烈建议最坏 service latency 不要超过它。LTR 是协商信息，不是硬实时合同。

## 4. 用接收队列估一个延迟上限

假设一个端点还有 32 KiB 空间可吸收新数据，入流是稳定的 64 MiB/s。若暂时得不到主机内存服务，这些空间约在 488 微秒后用完。端点还要从这个数里扣掉处理时间、突发流量余量等，才能决定这段时间向上报告多严格的要求。这里的数值只用于演算。

负载退去后，应更新较宽松的要求。否则平台持续按照高负载时的延迟预算运行，会错失更深的空闲状态。

## 5. OBFF：平台给 Endpoint 的批处理提示

Optimized Buffer Flush/Fill 告诉 Endpoint：CPU、memory 或 central interconnect 正因其他工作处于 active window，此时发 bus-master DMA 或 interrupt 的额外唤醒成本较低。

Message 模式有三种 code：

- `CPU Active`：所有 device action 窗口；
- `OBFF`：system memory path 适合 bus-master read/write；
- `Idle`：平台正处低功耗空闲。

**它只是 hint。** Function 在 BME/IRQ 已允许时仍可在窗口外发 traffic；只是最好 buffer/batch 到合适窗口。Endpoint 也不能假定窗口至少持续多久。否则一个晚到的 DMA burst 会让“省电优化”变成新的数据丢失条件。

OBFF 可经 WAKE#或 Message 传递。Message 只向 downstream、point-to-point routing；Variation A 在 Link 处于 Tx_L0s/L1 时丢弃通知，Variation B 会先唤到 L0 再发送。平台必须确认整条拓扑能把所选形式送到 Endpoint，才可 enable。

## 6. 一批 DMA 要不要等 OBFF 窗口

假设一批统计计数可以延后 2 毫秒上传。端点可以暂存这些数据，一旦收到表明中央资源活跃的提示就一起提交。若到期限仍没有提示，也应该照常发送。对即将溢出的接收队列，等待窗口反而会丢数据。

因此要分别观察两件事：平台发出的提示有没有抵达，业务队列收到提示后有没有真正合并提交。只读一个 OBFF Enable 位分不出两者。

## 7. Linux/驱动应该怎样使用这个认知

普通 Function driver 通常不应自行开启整条 hierarchy 的 LTR/OBFF；PCI core、firmware 和 platform PM policy 需要先确认路径能力。device-specific driver/firmware 更关心：

- 当前工作负载变化时，是否需要更新 LTR requirement；
- 更新为更严格值要提前于相关 traffic；放宽值应跟在旧需求最后一笔之后；
- non-D0、FLR、DL_Down/Up 后是否重建/清除报告；
- 收到 OBFF 窗口时只优化 batch，不改变正确性和 deadline；
- DMA completion/interrupt ordering 仍按普通 PCIe 与 DMA API 规则处理。

排查时把上游端口和 Function 的 Device Capabilities 2 / Device Control 2 放在一起看。单看设备支持哪些消息形式，还不知道平台实际选择了哪一种。

## 8. Debug

- LTR Message 收到 UR：检查路径每一跳 support/enable 以及启用顺序；
- L1.2 后 DMA 尾延迟异常：比较 LTR 值、CLKREQ#有效时间、L1 exit 与 RC service latency 总和；
- LTR 更新迟迟看不见：确认链路已经可用、路径允许发送，并对照负载变化时间；
- 多 Function 的报告值异常：检查各 Function 状态改变后，是否重新取了最严格的要求；
- OBFF 无功耗收益：先证明 EP client 真的消费了 hint 并 batch traffic；
- OBFF 报 UR：核对上下游支持并启用的传输形式；

## 9. 参考资料

- PCI Express Base Specification 5.0 r1.0：§§2.2.8.8–.9、6.18–6.19、相关 Device Capabilities/Control 2 与 LTR Extended Capability。
- [Linux v6.6 的 PCI 配置字段定义](https://github.com/torvalds/linux/blob/v6.6/include/uapi/linux/pci_regs.h)
