# PCIe 专题归档记录

2026-11-27 从本地 36 份讲解型笔记中整理出 31 篇公开正文，其中 30 篇新建，1 篇合入原有入门文章。原有的两篇软件流程笔记合并为一篇。专题共 34 篇。

| 分类 | 篇数 |
|---|---:|
| 基础架构 | 11 |
| 枚举与系统流程 | 15 |
| 驱动实例 | 5 |
| Endpoint 固件 | 3 |

## 读者会遇到的几处变化

- 入门文章沿用原网址和 PCI/PCIe 对照图，补上根端口与交换机拓扑，并澄清图中 Gen5 x16 的带宽是双向合计。
- 两篇软件流程笔记保留较完整的一篇。旧网址自动跳转，原流程图仍可从原地址打开。
- 同题的总线与 LTSSM 概览未再另开文章。常见问题收入入门文；专题首页给出 12 篇阅读顺序。
- 控制器部分沿 Linux 上游代码分别讲 Host 配置访问和 Endpoint BAR、映射与中断。按非公开手册编写的章节索引、实例内部分块和寄存器细节保存在原始资料中。
- 8 张独立 SVG 分别说明拓扑、BAR 读与返回、链路训练、地址翻译上下文、Type 0 配置头，以及 Capability 发现、遍历和启用。保留旧文已有的对照图和软件流程图。
- 端到端算例给出低于 4 GiB 的读请求和 MSI-X 写请求。两者使用 3 DW 地址格式。驱动支持 64 位 DMA 与这一次 TLP 选用的格式是两个问题。

## 本轮整理的文章

- [ARI 与 SR-IOV：从多 Function 到 PF、VF](../source/_posts/embedded/pcie/architecture/pcie-ari-sriov.md)
- [读 Linux Cadence PCIe Host 驱动：一次配置读怎样找到 Endpoint](../source/_posts/embedded/pcie/architecture/pcie-cadence-core-architecture.md)
- [Data Link：ACK、重放与逐跳流控](../source/_posts/embedded/pcie/architecture/pcie-data-link-replay-flow-control.md)
- [IOMMU、ATS、PASID 与 PRI：设备怎样访问进程地址空间](../source/_posts/embedded/pcie/architecture/pcie-iommu-ats-pasid-pri.md)
- [Link、Lane 与 LTSSM：PCIe 链路怎样建立](../source/_posts/embedded/pcie/architecture/pcie-link-lane-ltssm.md)
- [Memory TLP 与 Completion：PCIe 怎样完成一次读写](../source/_posts/embedded/pcie/architecture/pcie-memory-tlp-completion.md)
- [Message、AtomicOp 与 TLP Prefix：数据读写之外的 PCIe](../source/_posts/embedded/pcie/architecture/pcie-message-atomic-prefix.md)
- [MPS、MRRS 与 Ordering：包长和顺序怎样影响性能](../source/_posts/embedded/pcie/architecture/pcie-mps-mrrs-ordering.md)
- [NTB 与多 Host：两棵 PCIe 树怎样通信](../source/_posts/embedded/pcie/architecture/pcie-ntb-multi-host.md)
- [TC、VC、PTM 与 ACS：分类、时钟和隔离各管什么](../source/_posts/embedded/pcie/architecture/pcie-tc-vc-qos-ptm-acs.md)
- [PCIe 入门：拓扑、角色与三层协议](../source/_posts/embedded/pcie/architecture/pcie_introduction.md)
- [PCIe DMA 与 Linux DMA API：引擎、地址和缓存](../source/_posts/embedded/pcie/driver-examples/pcie-dma-linux-api.md)
- [INTx、MSI 与 MSI-X：中断怎样沿 PCIe 到达 CPU](../source/_posts/embedded/pcie/driver-examples/pcie-intx-msi-msix.md)
- [Linux PCI 功能驱动：probe、MMIO 与生命周期](../source/_posts/embedded/pcie/driver-examples/pcie-linux-function-driver.md)
- [Linux Host、EPC 与 EPF：控制器驱动的职责边界](../source/_posts/embedded/pcie/driver-examples/pcie-linux-host-controller.md)
- [读 Linux Cadence Endpoint 驱动：BAR、出站窗口和一笔 MSI](../source/_posts/embedded/pcie/endpoint-firmware/pcie-cadence-register-programming.md)
- [Link Down、Hot Reset 与 FLR：恢复时该重建什么](../source/_posts/embedded/pcie/endpoint-firmware/pcie-reset-recovery.md)
- [BAR 与地址翻译：一笔访问怎样到达设备内部](../source/_posts/embedded/pcie/enumeration-system/pcie-bar-address-translation.md)
- [BDF、Requester ID 与 PCIe 层次路由](../source/_posts/embedded/pcie/enumeration-system/pcie-bdf-requester-id.md)
- [PCIe Capability：发现能力与安全遍历](../source/_posts/embedded/pcie/enumeration-system/pcie-capability-safe-walking.md)
- [配置空间与 ECAM：主机怎样发现 PCIe 设备](../source/_posts/embedded/pcie/enumeration-system/pcie-configuration-ecam-routing.md)
- [DPC：先隔离下游故障，再恢复设备](../source/_posts/embedded/pcie/enumeration-system/pcie-dpc-recovery.md)
- [DPA：设备保持 D0 时怎样调整功耗预算](../source/_posts/embedded/pcie/enumeration-system/pcie-dynamic-power-allocation.md)
- [一块 Endpoint 的完整旅程：从上电到 DMA 与 MSI-X](../source/_posts/embedded/pcie/enumeration-system/pcie-endpoint-transaction-walkthrough.md)
- [PCIe 错误与 AER：从事务失败到系统恢复](../source/_posts/embedded/pcie/enumeration-system/pcie-error-model-aer.md)
- [Expansion ROM：驱动加载前，设备如何参与启动](../source/_posts/embedded/pcie/enumeration-system/pcie-expansion-option-rom.md)
- [PCIe 热插拔：有序移除与意外掉卡](../source/_posts/embedded/pcie/enumeration-system/pcie-hotplug-removal.md)
- [Linux PCI Core：从枚举到驱动绑定](../source/_posts/embedded/pcie/enumeration-system/pcie-linux-pci-core.md)
- [LTR 与 OBFF：设备和平台怎样协商省电时机](../source/_posts/embedded/pcie/enumeration-system/pcie-ltr-obff.md)
- [PCIe 电源管理：D-state、ASPM、L1SS 与 PME](../source/_posts/embedded/pcie/enumeration-system/pcie-power-aspm-pme.md)
- [Slot Power Limit 与 Power Budgeting：功耗需求和额度怎样对接](../source/_posts/embedded/pcie/enumeration-system/pcie-slot-power-budget.md)
