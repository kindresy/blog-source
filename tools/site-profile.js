'use strict';

// Editorial choices only. Category names and paths stay in the existing taxonomy.
module.exports = {
  topics: [
    { name: 'I3C', mark: 'I3C', description: '从两根线上的事务，走到控制器、裸机与 Linux 驱动。', order: ['i3c-basic-spec', 'i3c-controller-guide', 'i3c-target-study', 'i3c-linux-driver'] },
    { name: 'MCTP 与 PLDM', mark: 'M/P', description: '读懂网络与 ID，沿双端架构跟进移植、调试和验收。', order: ['mctp-pldm-network-ids-packet-path', 'mctp-pldm-topologies-from-simple-to-complex', 'mctp-pldm-porting-software-architecture', 'mctp-pldm-i3c-milestone-20261125', 'i3c-mctp-regression-milestone-20261126', 'mctp-pldm-porting-acceptance-testcases'] },
    { name: 'PCIe', mark: 'PCIe', description: '从拓扑、枚举与地址开始，沿事务、DMA 和中断读到 Linux 驱动与控制器。入门先读下面的主线，进阶内容按四个分类继续查阅。', readingLimit: 12, order: ['pcie_introduction', 'pcie-configuration-ecam-routing', 'pcie-bdf-requester-id', 'pcie-bar-address-translation', 'pcie-endpoint-transaction-walkthrough', 'pcie-memory-tlp-completion', 'pcie-data-link-replay-flow-control', 'pcie-link-lane-ltssm', 'pcie-dma-linux-api', 'pcie-intx-msi-msix', 'pcie-linux-pci-core', 'pcie-linux-function-driver', 'pcie_software1', 'pcie-linux-host-controller', 'pcie-cadence-core-architecture', 'pcie-cadence-register-programming', 'pcie_fw'] },
    { name: 'DMA', mark: 'DMA', description: '从一笔内存搬运出发，理解硬件配置与 DMAEngine。', order: ['axidma-basics', 'axidma', 'linux_dma'] },
    { name: 'I2C', mark: 'I²C', description: '协议时序、控制器实现，以及驱动里容易踩到的边界。', order: ['i2c', 'i2c_controller', 'i2c_controller_1'] },
    { name: 'SD', mark: 'SD', description: '沿卡、总线与 Linux MMC 子系统梳理存储访问。', order: ['sd_introduction'] },
    { name: 'AI', mark: 'AI', description: '记录 AI 工具的概念、技能配置与使用中的问题。', order: [] },
    { name: '生活记录', mark: '日常', description: '给阅读、思考和工作之外的日常留一点地方。', order: [] }
  ],
  featured: ['mctp-pldm-network-ids-packet-path', 'axidma-basics', 'pcie_software1'],
  current: {
    date: '2026-11-27',
    title: 'I3C → MCTP → PLDM',
    status: '基础查询已有实板记录，分包与恢复待验收。',
    architecture: '/2026/11/27/mctp-pldm-porting-software-architecture/',
    testcases: '/2026/11/27/mctp-pldm-porting-acceptance-testcases/'
  },
  evidence: {
    'mctp-pldm-i3c-milestone-20261125': { label: '阶段记录 · 首包地址 NACK', date: '2026-11-25' },
    'i3c-mctp-regression-milestone-20261126': { label: '基础查询通过 · 分包待回归', date: '2026-11-26' },
    'mctp-pldm-porting-software-architecture': { label: '架构基线 · 范围已说明', date: '2026-11-27' },
    'mctp-pldm-porting-acceptance-testcases': { label: '验收计划 · 待执行', date: '2026-11-27' }
  }
};
