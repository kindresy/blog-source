'use strict';

// null marks a category that can hold articles directly.
const categories = {
  '嵌入式开发': {
    I3C: { '协议基础': null, '控制器与裸机开发': null, 'Linux 驱动': null },
    'MCTP 与 PLDM': { '原理与网络': null, '移植与回归': null },
    PCIe: { '基础架构': null, '枚举与系统流程': null, '驱动实例': null, 'Endpoint 固件': null },
    I2C: { '协议与时序': null, '控制器与驱动': null },
    DMA: { 'AXIDMA 硬件与裸机': null, 'Linux DMAEngine': null },
    SD: null
  },
  AI: {
    'Claude Code': { '核心概念': null, '技能与插件': null, '使用排障': null },
    OpenClaw: null
  },
  '生活记录': { '身心与状态': null, '旅行与见闻': null }
};

const articleTypes = {
  tutorial: '教程',
  'code-analysis': '源码分析',
  milestone: '工程里程碑',
  'reading-notes': '阅读笔记'
};

function validCategoryPath(names) {
  if (!Array.isArray(names) || !names.length) return false;
  let branch = categories;
  for (const name of names) {
    if (branch === null || !Object.hasOwn(branch, name)) return false;
    branch = branch[name];
  }
  return branch === null;
}

module.exports = { categories, articleTypes, validCategoryPath };
