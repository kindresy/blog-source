'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const frontMatter = require('hexo-front-matter');
const { articleTypes, validCategoryPath } = require('./taxonomy');

const root = path.resolve(__dirname, '..', 'source', '_posts');
const permalinks = new Set();

function check(directory) {
  let count = 0;
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) {
      count += check(file);
      continue;
    }
    if (!/\.(md|html|svg|txt)$/i.test(entry.name)) continue;
    const raw = fs.readFileSync(file, 'utf8');
    assert(!/NGU800P|EDGE10/i.test(raw), `${file}: 存在未脱敏的项目标识`);
    if (!entry.name.endsWith('.md')) continue;
    assert(raw.startsWith('---\n'), `${file}: 缺少 Front Matter`);
    const post = frontMatter.parse(raw);
    assert(typeof post.title === 'string' && post.title.trim(), `${file}: 缺少标题`);
    assert(post.date instanceof Date && !Number.isNaN(+post.date), `${file}: 日期无效`);
    assert(validCategoryPath(post.categories), `${file}: 分类必须对应 tools/taxonomy.js 中的完整路径`);
    assert(typeof post.permalink === 'string' && post.permalink.trim(), `${file}: 缺少固定 permalink`);
    const permalink = post.permalink.replace(/^\/+|\/+$/g, '');
    assert(!permalinks.has(permalink), `${file}: permalink 与其他文章重复`);
    permalinks.add(permalink);
    assert(Array.isArray(post.tags) && post.tags.length, `${file}: 缺少标签`);
    assert(post.tags.every(tag => typeof tag === 'string' && /^[a-z0-9]+(?:-[a-z0-9]+)*$/.test(tag)), `${file}: 标签须使用小写英文或数字，以连字符分词`);
    assert(post.tags.filter(tag => Object.hasOwn(articleTypes, tag)).length === 1, `${file}: 须选择一种文章类型标签`);
    // Catch pasted assistant transcripts; editorial review is still needed before deployment.
    assert(!/<\/?think>|^(?:Grep "|Read \S+ \(lines|现在让我|让我继续|我已经收集)/m.test(post._content), `${file}: 残留对话或工具日志`);
    count++;
  }
  return count;
}

console.log(`内容检查通过：${check(root)} 篇文章。部署前仍需人工检查表达是否自然。`);
