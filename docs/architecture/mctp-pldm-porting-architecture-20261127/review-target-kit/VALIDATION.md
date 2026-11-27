# 验证记录

本包完成以下检查；未运行芯片硬件或 i.MX93 MCTP 联调。

- Linux x86_64 GCC 原生编译与内存协议测试：PASS，启用 `-Wall -Wextra -Werror`。记录：`validation/native-test.log`。
- AddressSanitizer + UndefinedBehaviorSanitizer：PASS，含默认内存泄漏检查。记录：`validation/asan-ubsan.log`。
- Xuantie RISC-V GCC 14.3.0 / newlib 静态库编译：PASS。记录：`validation/riscv-build.log` 和 `validation/riscv-toolchain.txt`。
- RISC-V 产物：`artifacts/riscv64/libchip_endpoint.a`。使用工具链默认架构/ABI生成，仅作编译验证；实际集成须按芯片固件原有 `-march/-mabi` 重编译。

测试内容：PEC 已知向量、MCTP Get/SetEID、PLDM Base 五条命令、GetVersion CRC32、错误版本与类型响应、短读重试、69B MCTP 首包重组、EID 修改和端点重建。

测试不是完整规范符合性测试。没有验证 IRQ/DMA、IBI 实际时序、动态地址分配、I3C 硬件 PEC 设置、Linux 驱动绑定、Discovery Notify 或最终固件链接/装载。
