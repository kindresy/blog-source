# 移植后 baremetal 分层架构

目标是复用现有 I3C 驱动，在其上建立管理协议服务。原 `i3c_test` 仍是独立验证入口，不承担生产管理服务。

```text
HOST i.MX93 Linux
  PLDM 请求 → MCTP → I3C Controller
                         │ SCL/SDA
=========================│==========================
芯片 baremetal           ▼
  ┌───────────────────────────────────────────────┐
  │ 应用入口：management service / CLI i3c_test    │
  │ 两个入口按运行模式选择，不争用同一设备会话    │
  └───────────────────────────────────────────────┘

  管理协议路径（自上而下）
  ┌───────────────────────────────────────────────┐
  │ PLDM 业务/provider                            │
  │ 传感器、PDR、FRU、控制、固件更新（逐项实现）   │
  ├───────────────────────────────────────────────┤
  │ PLDM 请求分发 + 编解码（libpldm）              │
  │ Type/Command/Instance ID、完成码、能力查询     │
  ├───────────────────────────────────────────────┤
  │ MCTP endpoint 核心（libmctp）                  │
  │ EID、Message Type、Tag、分包重组、Control      │
  ├───────────────────────────────────────────────┤
  │ MCTP over I3C binding（平台适配）              │
  │ PEC、69B 基线、TX 队列、IBI 0xAE、短读重发   │
  ├───────────────────────────────────────────────┤
  │ 现有 I3C Target 驱动                          │
  │ Private RX/TX、FIFO、CCC、IBI、IRQ、DMA        │
  ├───────────────────────────────────────────────┤
  │ BSP：时钟/复位、MMIO、IRQ 映射、DMA/cache      │
  ├───────────────────────────────────────────────┤
  │ SoC 硬件：I3C IP、DMA、APLIC/IMSIC、内存      │
  └───────────────────────────────────────────────┘

  横向运行支撑：裸机主循环、事件队列、timer、allocator、log
  业务数据来源：PLDM provider → 传感器/寄存器/PMBus/AVSBus 驱动
```

图中层级是职责关系，不表示每次操作都必须串行经过所有层。MCTP Control 在 MCTP 层处理；CCC 在 I3C 层处理。PLDM provider 读取本地传感器时，调用对应本地驱动，不把访问重新绕回 MCTP。

## 关键边界

| 层 | 知道什么 | 不应承担什么 |
|---|---|---|
| PLDM provider | Sensor ID、单位、状态、更新策略 | I3C FIFO 和寄存器位 |
| PLDM codec/dispatch | 命令字段、长度、完成码 | 动态地址、IBI |
| MCTP core | EID、消息、分包与 Tag | 传感器单位、I3C MMIO |
| I3C binding | 动态地址、PEC、IBI、包生命周期 | PLDM 管理语义 |
| I3C driver/BSP | FIFO、中断、DMA、时钟复位 | MCTP Control 与 PLDM 命令 |

## 一次读取传感器的完整路径

HOST 发 PLDM 请求 → I3C RX 完成中断 → ISR 保存数据/事件 → 主循环校验 PEC → MCTP 重组 → PLDM 解析 → provider 读本地硬件 → 编码响应 → MCTP 分包 → binding 排队并计算 PEC → 准备硬件 TX → 发 IBI → HOST Private Read → 完整传输后释放包。

传感器 provider 尚未包含在本包首版实现中；当前实际可运行的命令处理是 PLDM Base。

## 裸机执行模型

- ISR：清必要中断、记录实际长度和结果、维护 DMA/FIFO 必要状态，提交事件。
- 主循环：处理 RX 事件、执行协议与业务、准备 TX、启动 IBI，并处理完成/失败事件。
- 所有协议入口串行执行；共享事件/缓冲区交接使用平台的临界区或原子机制。
- FIFO/DMA 完成、IBI ACK、Private Read 完成、PLDM 命令成功是不同事件，不混用。
- `i3c_test` 与管理服务通过模式和设备所有权切换；切换时先停止事务、清理会话和缓冲区。

## 未来目录建议（尚未写入 baremetal）

```text
third_party/libmctp/
third_party/libpldm/
management/mctp/          endpoint 和 Control 扩展
management/pldm/          分发与管理业务 provider
ports/i3c/                MCTP I3C binding
ports/baremetal/           timer、allocator、事件与日志适配
```

这只是移植时的职责布局建议，最终目录服从既有工程约定。当前 `src/chip_endpoint.c` 为便于首轮联通将端点和 Base handler 放在同一文件，业务扩展时再按上述边界拆分。
