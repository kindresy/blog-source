文章内容一定要有人味

部署之前，检查一下新文章是否有AI痕迹。

上传前，将“NGU800P”或“EDGE10”相关字眼全部替换为“Current Proj”（不区分大小写）。

## 文章分类

新增或整理文章时，按下列体系归类。`tools/taxonomy.js` 是分类校验和分类导航的统一配置；本文件与其保持一致。

```text
嵌入式开发
├── I3C
│   ├── 协议基础
│   ├── 控制器与裸机开发
│   └── Linux 驱动
├── MCTP 与 PLDM
│   ├── 原理与网络
│   └── 移植与回归
├── PCIe
│   ├── 基础架构
│   ├── 枚举与系统流程
│   ├── 驱动实例
│   └── Endpoint 固件
├── I2C
│   ├── 协议与时序
│   └── 控制器与驱动
├── DMA
│   ├── AXIDMA 硬件与裸机
│   └── Linux DMAEngine
└── SD
AI
├── Claude Code
│   ├── 核心概念
│   ├── 技能与插件
│   └── 使用排障
└── OpenClaw
生活记录
├── 身心与状态
└── 旅行与见闻
```

- 按文章主要主题选择一个完整的分类路径，`categories` 从一级分类写到末级方向。例如 `[嵌入式开发, MCTP 与 PLDM, 移植与回归]`。
- MCTP/PLDM 的网络、协议、移植和回归文章归入独立的“MCTP 与 PLDM”专题；使用 I3C 承载时，可增加 `i3c` 标签。
- 跨主题内容用标签补充，不通过重复归类打乱分类树。
- 确需扩展分类时，同步更新 `tools/taxonomy.js`、本文件、`README.md` 和 `_config.yml` 的 `category_map`，为新增分类配置稳定的英文路径。

## 文章类型与元数据

每篇文章必须填写 `title`、`date`、完整的 `categories`、`tags` 和固定 `permalink`。

`tags` 中必须且只能选择一个文章类型标签，再补充协议、平台和机制标签：

| 类型标签 | 适用内容 |
|---|---|
| `tutorial` | 原理讲解、学习教程与使用指南 |
| `code-analysis` | 源码、驱动和固件实现分析 |
| `milestone` | 移植、联调、回归及阶段性工程记录 |
| `reading-notes` | 阅读摘录、笔记与整理；不代表一定为转载 |

标签使用小写英文或数字，多词用连字符连接。转载内容保留原作者和来源，不因调整分类而删除出处。

## 源码目录与链接

- 文章源码分别放入 `source/_posts/embedded/`、`source/_posts/ai/`、`source/_posts/life-record/`，继续按专题和内容方向的英文目录细分，参照现有文章的存放位置。
- 配图、交互图和证据附件放在与文章同名的资源目录，移动文章时一起移动，并更新相关生成脚本的路径。
- 调整分类或源码位置时，保留原有 `permalink`，不要让文章网址随目录变化。检查旧文章链接、图片和附件仍能访问。
- 分类首页由 `layout/categories.ejs`、`scripts/categories.js` 和统一分类配置生成，不直接修改 `public/` 或已安装主题文件作为持久修复。

## 双仓库同步与发布

用户要求发布或推送博客时，必须同时完成文章源码和生成网页的同步，不能只完成其中一端。

| 仓库 | 分支 | 用途 |
|---|---|---|
| `https://github.com/kindresy/blog-source` | `main` | Markdown 原文、配图、配置、规则和工具脚本；当前工作区的 `origin` |
| `https://github.com/kindresy/kindresy.github.io` | `master` | Hexo 生成的静态网页；`npm run deploy` 的发布目标 |

按以下顺序完成发布：

1. 人工检查新写或改写内容的表达、事实依据和脱敏。检查范围包括标题、正文、示例、附件和配图文字，不能用自动扫描代替通读。
2. 运行 `npm run check:content` 和 `npm run build`，检查分类层级、文章类型、链接、资源与图表。大规模迁移导致缓存冲突时，先 `npm run clean` 再构建。
3. 将本次修改提交并推送到 `blog-source` 的 `main`，确认源码已到远端。
4. 核对部署仓库远端状态，避免覆盖其他更新；运行 `npm run deploy`，同步网页到 `kindresy.github.io` 的 `master`。
5. 等待 GitHub Pages 构建成功，核验受影响的公网文章、分类导航、图片、交互图和附件；仅推送成功不能代替公网核验。
6. 向用户提供可访问的结果链接。若任一步失败，明确说明源码推送、网页部署和公网验证分别完成到哪一步，不把部分成功描述为全部完成。

不要将 Markdown 源码当作网页产物推到部署仓库，也不要把 `public/`、`.deploy_git/` 或 `node_modules/` 提交到源码仓库。
