---
title: claude使用经验
date: 2026-03-16
toc: true
max_depth: 3 
mermaid: true
categories: [AI, Claude Code]
tags: [ai, claude-code, troubleshooting]
---

## webfetch失败

![](./claude_fix_exp/2026-03-20-15-28-19.png)

`.claude/settings.json`追加配置`"skipWebFetchPreflight": true`