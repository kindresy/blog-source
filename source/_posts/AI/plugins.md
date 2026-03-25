---
title: plugins
date: 2026-03-16
toc: true
max_depth: 3 
mermaid: true
categories: AI
tags: [AI]
---


除了Claude官网插件市场（https://claude.com/plugins#plugins），**Claude Code（核心使用插件的场景）** 还有**4种官方/社区主流的插件安装路径**，覆盖**在线插件市场添加、自然语言指令安装、本地离线安装、第三方工具安装**，同时支持官方/社区开源插件，以下是分场景的完整方法，可直接落地使用：

# 一、在线添加「官方/第三方插件市场」（推荐，可安装更多开源插件）
Claude Code支持通过命令添加**官方/社区维护的插件市场源**，除了官网默认市场，还能安装GitHub上的开源插件市场，步骤如下：
### 1. 核心命令（通用）
```bash
/plugin marketplace add 插件市场仓库地址/GitHub简写
```
### 2. 常用示例
- **添加Anthropic官方技能市场**（补充更多官方插件）：
  ```bash
  /plugin marketplace add anthropics/skills
  ```
- **添加社区开源插件市场**（如本地LLM集成、工程化插件）：
  ```bash
  # 本地LLM代理插件市场
  /plugin marketplace add fcakyon/claude-codex-settings
  # 复合工程化插件市场
  /plugin marketplace add https://github.com/EveryInc/compound-engineering-plugin
  # 记忆增强插件市场
  /plugin marketplace add thedotmack/claude-mem
  ```
### 3. 安装市场内插件
添加后直接通过命令安装，格式：`/plugin install 插件名@市场名`
```bash
# 从官方技能市场安装文档处理插件
/plugin install document-skills@anthropic-agent-skills
# 从社区市场安装本地LLM代理插件
/plugin install ccproxy-tools@claude-settings
```

# 二、自然语言直接指令安装（最便捷，无需记命令）
在Claude Code对话中，**直接用自然语言描述安装需求+插件地址**，Claude会自动识别并完成安装，适合新手/快速安装单插件：
### 示例指令
```
帮我安装GitHub上的pdf-split插件，项目地址是：https://skillsmp.com/skills/jongwony-cc-plugin-pdf-split-skills-pdf-split-skill-md
```
```
帮我安装anthropics/skills仓库里的pptx技能插件
```
Claude会自动解析地址、下载插件并配置到对应目录，无需手动操作。

# 三、本地离线安装（无网络/自定义插件，100%生效）
这是**最灵活的方式**，支持将**官网下载的插件包、GitHub克隆的开源插件、自己开发的自定义插件**，通过「解压到指定目录」完成安装，无网络也能使用，也是你之前安装PDF-Split插件的核心方法：
### 1. 核心安装目录
- **全局生效**（所有Claude Code项目可用）：`~/.claude/skills/`（插件/技能通用）
- **项目级生效**（仅当前项目可用）：`当前项目根目录/.claude/skills/`
### 2. 步骤
1. 从GitHub/社区下载插件包（zip/文件夹格式，核心含`SKILL.md`）；
2. 解压后，将**插件文件夹**直接复制到上述目录（不要嵌套外层文件夹）；
3. 执行`/plugin reload`或重启Claude Code，插件自动加载生效。
### 示例
安装PDF-Split插件：
```bash
# 创建全局目录
mkdir -p ~/.claude/skills
# 将解压后的pdf-split插件文件夹复制进去
cp -r 你的pdf-split插件文件夹/ ~/.claude/skills/
```

# 四、第三方工具安装（适用于Skills类插件，批量管理）
通过**OpenSkills**（Claude Skills专属管理工具），可批量安装官方/社区Skills插件，支持「全局/项目级」安装，适合需要批量管理插件的场景：
### 1. 先安装OpenSkills工具
```bash
npm i -g openskills
```
### 2. 安装插件
- **项目级安装**（仅当前项目）：
  ```bash
  openskills install anthropics/skills
  ```
- **全局安装**（所有项目共享）：
  ```bash
  openskills install anthropics/skills --global
  ```
### 3. 特点
安装时可**自定义勾选插件**，无需全部安装，单个插件大小从几十B到几MB不等，轻量高效。

# 五、补充：IDE集成安装（Claude Code插件）
如果使用**IntelliJ IDEA/CLion**等JetBrains系列IDE，可直接从**IDE自带的Plugin Market**安装「Claude Code (Beta)」插件，实现IDE内直接调用Claude插件功能，步骤：
1. 打开IDE → Settings → Plugins → Marketplace；
2. 搜索「Claude Code (Beta)」，点击安装并重启IDE；
3. 打开Claude Code面板，即可直接使用已安装的插件。

# 六、关键注意事项
1. **插件与Skills的目录通用**：Claude的Plugin和Skill最终都会加载`~/.claude/skills/`目录下的文件，无需区分存放，放入即生效；
2. **离线插件不显示在/plugin discover**：本地放入的插件不会出现在官网插件市场的「Discover」列表，但可通过`/skills list`查看，直接在对话中调用即可；
3. **企业级私有插件市场**：Claude Cowork支持企业管理员创建**私有插件市场**，在组织内部统一分发、管理自定义插件，适合企业团队使用。

# 七、快速验证插件是否安装成功
1. 查看已加载插件/技能：`/skills list`（核心命令，覆盖所有安装方式）；
2. 查看插件市场源：`/plugin marketplace list`；
3. 直接调用：在对话中输入`使用[插件名]完成[任务]`，如`使用pdf-split插件分割这个PDF`，Claude自动调用即表示安装成功。