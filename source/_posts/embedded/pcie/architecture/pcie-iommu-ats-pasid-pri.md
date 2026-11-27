---
title: "IOMMU、ATS、PASID 与 PRI：设备怎样访问进程地址空间"
date: 2026-11-27
updated: 2026-11-27
description: "从普通 DMA 的 IOVA 开始，解释翻译缓存、进程身份、缺页请求及失效通知。"
toc: true
max_depth: 3
categories: ["嵌入式开发", "PCIe", "基础架构"]
tags: ["pcie", "architecture", "tutorial"]
permalink: "2026/11/27/pcie-iommu-ats-pasid-pri/"
---

## 1. 从普通 DMA 地址开始

没有地址翻译时，EP 发出的 PCIe Memory Request address 可直接对应平台 memory address。加入 IOMMU 后：

```text
EP Requester ID + PCIe address(IOVA)
 → Root Complex / IOMMU Translation Agent
 → translation + permission table
 → Host physical address
 → memory/cache-coherent interconnect
```

IOMMU 不在 EP BAR 后面；它通常位于 **EP 主动访问 Host/peer memory 的 upstream 路径**。CPU 访问 EP BAR 则走 host outbound MMIO window，是另一方向。

Linux DMA API 给 driver 的 `dma_addr_t` 是 device 应放进 descriptor 的地址。IOMMU 开启时它常是 IOVA；即便暂时等于 physical address，driver 也不能依赖这个偶然。

## 2. Requester ID 是最基本的地址空间身份

IOMMU 用 Requester ID（传统 BDF，或 ARI Function ID）区分设备/Function。两个 Function 即使发同一 IOVA，也可映射到不同 physical page 或 permission。

这解释了为什么 SR-IOV 的每个 VF 需要唯一 Routing/Requester ID：它不仅用于 Completion routing，也是一道 DMA isolation identity。

## 3. ATS：让设备预先询问并缓存翻译

ATS（Address Translation Services）解决高频 DMA 每次都在 IOMMU 查表的 latency/pressure。启用后：

```text
Function 发 ATS Translation Request(untranslated address)
 → RC 的 Translation Agent 查 translation/protection table
 → ATS Translation Completion(translated address + permissions/range)
 → Function 缓存在自己的 ATC

之后 DMA MRd/MWr
 → address 来自 ATC，TLP AT field 标记 Translated
 → RC 识别已翻译请求，按平台规则快速处理
```

- **TA** 是 RC/platform 的 Translation Agent；
- **ATPT** 是其地址翻译和保护表抽象；
- **ATC** 是 Function 内的 Address Translation Cache。

ATS 不是“设备自己绕过 IOMMU”。translation 仍由可信 TA 授予，设备只能缓存通过 ATS protocol 得到的结果。

ATS 本身不能防止恶意设备向其他 EP 的 BAR 地址发起 DMA。Untranslated Request 本来就应接受 IOMMU/TA permission；P2P Request 是否会先到 translation agent，则取决于 hierarchy routing、ACS redirect/control 和平台 topology。ATS 允许设备携带 TA 授予的 Translated address，反而要求平台更信任 Requester 正确遵守 protocol。隔离边界是 IOMMU context、Requester identity、ACS/路由与 OS ownership 的组合，不是简单地把 AT 从 `00b` 改成 `10b`。

王齐教材 §13.2 用“ATS 防止恶意 VM 把地址写成别的 EP BAR”解释安全性，适合作为风险场景的引子，但因果归属不够准确；现代实现必须按上面的 path 逐段验证。

### Invalidation 为什么复杂

OS 修改 page table 后，旧 ATC translation 不能继续用：

```text
TA → ATS Invalidation Request(range + ITAG)
Function → invalidate matching ATC entries
         → drain/nullify references and order old Posted Writes
Function → Invalidate Completion(ITAG)
TA → 收齐后才认为旧 translation 不再使用
```

这不是普通 cache flush 小事；少一次 invalidation completion 就可能让设备 DMA 到已经给别人的 page。

来源：Base Spec 5.0 §§10.1.1、10.2–10.3，printed/PDF pp.1170–1203。

## 4. PASID：在一个 Function 内再区分进程地址空间

Requester ID 只能区分 Function。若 GPU/accelerator 的同一个 Function 同时代表多个用户进程，就需要 **PASID**（Process Address Space ID）：

```text
translation context key = Requester ID + PASID
```

PASID 通过 TLP Prefix 携带，还可带 Execute Requested、Privileged Mode Requested 语义。这样 device work queue 可以提交用户 virtual address，由平台按对应进程页表/权限翻译。

PASID 并不自动保证安全：OS/IOMMU driver 必须建立 context binding，设备必须可靠地给每笔 request 加正确 PASID，reset/进程退出时还要停止并清理在途访问。

## 5. PRI：地址还没驻留时，让设备请求“把页准备好”

ATS Translation Request 若页面不存在，只能得到不可访问结果。PRI（Page Request Interface）允许 Function 发 Page Request，请平台 page-in/建立 mapping，然后收到 Page Response 再重试，适用于 Shared Virtual Memory/on-demand paging。

```text
device work uses VA + PASID
 → ATC miss / translation unavailable
 → PRI Page Request(PASID, page, permissions)
 → OS handles fault / maps page
 → Page Response
 → ATS translation / DMA resumes
```

PRI 有 outstanding request allocation/credits、Stop/Reset protocol。设备必须遵守平台分给它的在途请求上限。

来源：Base Spec §§10.1.2–10.1.3、10.4–10.5，printed/PDF pp.1176–1213。

## 6. 四者关系

| 能力 | 解决什么 | 最小身份 | 缓存在哪里 |
|---|---|---|---|
| IOMMU/TA | DMA translation + isolation | Requester ID | RC/IOMMU TLB |
| ATS | Function 预取/缓存 translation | Requester ID（可加 PASID） | device ATC |
| PASID | 一个 Function 下多个 process context | Requester ID + PASID | context/table key |
| PRI | page 尚不可用时请求 OS 处理 | Requester ID + optional PASID | 不是 translation cache，而是 fault protocol |

可以有 IOMMU 而无 ATS；可以 ATS 而不使用 PASID；PASID/PRI 常与 ATS/SVM 组合，但不是四个按钮必须一起全开。

![同一 Function 的两笔 DMA 使用不同的进程地址空间](pcie-iommu-ats-pasid-pri/translation-context.svg)

图中两个队列提交相同的虚拟地址。平台用 Requester ID 和 PASID 选择翻译上下文，各自查到不同的页；只有启用了 ATS 的设备才会在设备侧缓存翻译。

## 7. Linux 视角

普通 function driver 大多只使用 DMA API，IOMMU core 自动按 `struct device`/Requester ID 建 domain。ATS/PASID/PRI/SVA 需要 PCI core、IOMMU driver、设备 driver 共同协商，不能由 driver 只写一个 capability bit 就宣布成功。

例如 Linux 6.6 的 `drivers/pci/ats.c` 分别维护 ATS、PRI 和 PASID 的能力与使能状态。读到一个 Capability 只能回答“硬件声明支持什么”；要判断某个队列实际上使用哪套地址空间，还要看设备驱动和 IOMMU 建立的绑定。

## 8. Debug

- IOMMU fault 先记 BDF/RID、PASID、IOVA、access type；
- descriptor 用的是 DMA address，还是错塞 CPU virtual/physical；
- Function/ATS/PASID/PRI capability 是否真的 enable；
- AT field 是否与地址性质一致，translated address 必须来自 ATS；
- page-table change 后 invalidation request/completion 是否收齐；
- reset/FLR 后 ATC/context 是否失效；
- PRI allocation 是否耗尽、Response Failure/Unexpected Response 是否置位。

## 9. 参考资料

- PCI Express Base Specification 5.0 r1.0, Chapter 10, printed/PDF pp.1169–1213。
- PCI Express Base Specification 5.0 r1.0, §7.8.8 PASID, printed/PDF pp.871–874。
- 王齐《PCI Express体系结构导读》，§13.2，printed pp.352–358 / PDF pp.364–370（作为早期ATS解释及安全边界校正样本）。
- [Linux v6.6 PCI ATS、PRI 与 PASID 的实现](https://github.com/torvalds/linux/blob/v6.6/drivers/pci/ats.c)
