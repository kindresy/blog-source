'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const frontMatter = require('hexo-front-matter');

const root = path.resolve(__dirname, '..', 'source', '_posts');
const categories = {
  '嵌入式开发': ['PCIe', 'I2C', 'DMA', 'SD'],
  AI: ['Claude Code', 'OpenClaw'],
  '生活记录': []
};

function check(directory) {
  let count = 0;
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      count += check(file);
      continue;
    }
    if (!entry.name.endsWith('.md')) continue;
    const raw = fs.readFileSync(file, 'utf8');
    assert(raw.startsWith('---\n'), `${file}: 缺少 Front Matter`);
    const post = frontMatter.parse(raw);
    assert(typeof post.title === 'string' && post.title.trim(), `${file}: 缺少标题`);
    assert(post.date instanceof Date && !Number.isNaN(+post.date), `${file}: 日期无效`);
    const [parent, child, ...extra] = post.categories || [];
    assert(Object.hasOwn(categories, parent), `${file}: 一级分类无效`);
    const children = categories[parent];
    assert(extra.length === 0 && (children.length ? children.includes(child) : !child), `${file}: 分类层级无效`);
    assert(Array.isArray(post.tags) && post.tags.length, `${file}: 缺少标签`);
    assert(post.tags.every(tag => typeof tag === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(tag)), `${file}: 标签须使用小写英文或数字，以连字符分词`);
    // Catch pasted assistant transcripts; editorial review is still needed before deployment.
    assert(!/<\/?think>|^(?:Grep "|Read \S+ \(lines|现在让我|让我继续|我已经收集)/m.test(post._content), `${file}: 残留对话或工具日志`);
    count++;
  }
  return count;
}

console.log(`内容检查通过：${check(root)} 篇文章。部署前仍需人工检查表达是否自然。`);
