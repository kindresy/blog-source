# 裸机芯片 MCTP / PLDM 独立移植包

这是独立源码交付件，没有修改或加入现有 baremetal 工程。用于芯片作为 I3C Target、Linux HOST 作为 Controller 的最小协议联通。当前完成的是可编译、可离线验证的移植起点；没有连接芯片 HAL，也没有实板运行。

## 内容与固定版本

- `vendor/libmctp`：上游原始源码，commit `ab58506f08dbda562fccaa66340faad9c2e96fa4`。
- `vendor/libpldm`：上游原始源码，tag `v0.13.0`，commit `af271b8247d22935c062cacd578d74cc35b6ec45`。
- `src/chip_endpoint.c`：新增的单实例裸机端点、I3C 包队列和 PLDM Base 示例。
- `include/chip_endpoint.h`：平台对接接口、调用时机和缓冲区所有权约定。
- `compat/`：本包专用的字节序、字符类型、ABI 宏及 libc 兼容层。不要全局加入已有工程公共 include 路径，建议只对本包源文件生效。
- `tests/test_endpoint.c`：内存中的协议测试，不会访问板卡或产生总线事务。
- `ARCHITECTURE.md`：未来集成到 baremetal 的分层和执行模型。

上游：[libmctp](https://github.com/openbmc/libmctp)、[libpldm](https://github.com/openbmc/libpldm)。保留各目录 LICENSE 和原文件版权声明。新增代码按 Apache-2.0 提供。

## 当前功能边界

- MCTP 核心分包重组；固定 BTU=64，每笔 I3C 数据最多 69 字节，支持重组到 512 字节。
- 上游 MCTP Control：Set/Get Endpoint ID、Get Version Support、Get Message Type Support。其他 Control 命令由该上游版本决定，不承诺完整 I3C 端点发现流程。
- PLDM Base：SetTID、GetTID、GetPLDMVersion、GetPLDMTypes、GetPLDMCommands；只报告 Base 类型及上述命令，版本 1.1.0。
- PEC 软件生成和校验、IBI 所需待发队列、短读保留队头从头重发。
- 单主循环执行、一次处理一条管理请求。响应未发送完成时，RX 返回 `-EBUSY`，平台须保留收到的包后续重试；这不等于在总线上自动 NACK。

首版没有实现：传感器/PDR、FRU、固件更新、SPDM、主动事件、Discovery Notify 周期流程、通用高并发事务管理。上游完整库已带入，但默认只构建所需模块。后续添加功能必须同时更新真实处理器和能力位图，不能只宣称支持。

初始 EID 可以为 0，由 HOST 主动 SetEID，或用固定测试 EID 9。TID 示例为 1，不等于 I3C 动态地址。该上游 Control 的 GetEID 属性等行为尚需针对最终 I3C 产品逐项审查；本包不是协议符合性认证实现。

## 平台接入顺序

1. 先单独构建本包，保留原有 `i3c_test` 回归路径。
2. 建立 MCTP 运行模式：配置 DCR=0xCC、正确 BCR/MDB 能力；保证 MRL/MWL 至少 69 字节，并使 HOST 接受 IBI。固定只接收最多 69 字节时必须正确报告/协商长度，不能沿用其他测试模式的较大上限却只分配 69 字节。
3. 调用 `chip_endpoint_init(9, 1, platform_monotonic_ms, context)`。默认依赖 malloc/free；可在 init 前用 `mctp_set_alloc_ops` 替换，且平台必须保证分配器可用及内存用量受控。
4. ISR/DMA 完成后只保存事件与数据。主循环将一次 Private Write 的实际数据（含 PEC、不含地址头）交给 `chip_endpoint_rx`。硬件自动添加/剥离 PEC 必须关闭或明确适配，避免重复校验/重复追加。
5. 主循环调用 `chip_endpoint_tx_peek`，把返回数据完整准备到硬件 TX buffer，然后才发 IBI，MDB=0xAE。不要在 IBI ACK 时就释放响应。
6. Controller Private Read 完成后，用实际长度和硬件结束结果调用 `chip_endpoint_tx_complete`。完整正常读才出队；失败/短读保留原包，重新 peek、重装硬件 FIFO，再按 binding 时序重发 IBI。
7. 若 IBI 被拒绝，不能当作 Private Read 完成。平台管理 IBI 重试定时、ENEC/DISEC 状态和 pending 数据，不能 busy-loop 无限触发。
8. 总线/外设复位或动态地址变化时，先停止 IRQ/DMA、撤销旧硬件缓冲区，再清理端点并重新初始化。重新建立 EID 配置。PEC 总是按本次事务的实际动态地址计算。

主循环伪代码（平台事件名不是真实 HAL API）：

```c
/* ISR -> event queue -> 以下主循环；不要在 ISR 执行整个协议栈。 */
if (rx_event_ready) {
    rc = chip_endpoint_rx(current_da, rx_data, rx_actual_length);
    /* -EBUSY: 保留此包重试；PEC/格式错误：丢弃并记录；其他错误：记录。 */
}
if (!hardware_tx_active && chip_endpoint_tx_pending()) {
    rc = chip_endpoint_tx_peek(current_da, stable_tx_buffer,
                               sizeof(stable_tx_buffer), &tx_length);
    if (!rc) {
        /* platform: arm hardware with tx_length, then request IBI MDB=0xAE */
        /* arm 失败需 tx_complete(0,false) 解除 prepared，保留原包。 */
    }
}
if (private_read_complete_event) {
    rc = chip_endpoint_tx_complete(actual_read_length, normal_end);
    /* 异常时须先清理硬件残留，再从包头重新准备队头。 */
}
```

`stable_tx_buffer` 不能是 ISR 返回后失效的栈缓冲区；DMA 需要平台自行处理地址映射和 cache 同步。库不配置 IRQ、时钟、复位、MMIO 或 DMA。

## 构建

在源码包根目录、Linux 环境执行：

```sh
make test
make all CC=riscv64-unknown-elf-gcc AR=riscv64-unknown-elf-ar
```

默认得到 `build/libchip_endpoint.a`。这是协议组件静态库，不是可以直接烧写的 ELF/BIN。集成时沿用 Makefile 中的源文件、局部 include 路径与宏，并在最终链接中保留 `-Wl,--gc-sections`。改变编译器或 CFLAGS 后用独立输出目录或重建，避免混用宿主和 RISC-V 对象。

系统依赖：GNU C11、标准整数/字符串/errno/内存分配、单调毫秒计时。PLDM 部分使用编译器属性和 bitfield；移植其他编译器需重新验证字节序与布局。

## 验证范围

本包的离线测试覆盖 PEC 已知向量、MCTP Control、PLDM Base、版本 CRC32、错误类型/版本应答、短读重发、69 字节首包重组和 EID 修改。ASan/UBSan 检查及 RISC-V 静态库编译结果见 `VALIDATION.md`。未进行硬件、Linux AF_MCTP、时序或协议符合性验证。

固定 libmctp 版本的 `mctp_bus_rx` 实现借用 RX packet、不释放，而头文件注释声称接管；本适配在调用返回后释放 packet，已用内存检查验证。升级上游时必须重新核实该所有权约定，不能直接换库。
