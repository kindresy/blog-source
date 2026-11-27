---
title: claude使用经验
date: 2026-03-16
toc: true
max_depth: 3 
mermaid: true
categories: ["AI","Claude Code","使用排障"]
tags: ["ai","claude-code","troubleshooting","tutorial"]
permalink: 2026/03/16/AI/claude_fix_exp/
---

## webfetch失败

![](./claude_fix_exp/2026-03-20-15-28-19.png)

`.claude/settings.json`追加配置`"skipWebFetchPreflight": true`