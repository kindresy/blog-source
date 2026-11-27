'use strict';

// Run after generating: checks the published routes and editorial UI contract.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const { parse } = require('hexo-front-matter');
const { categories } = require('./taxonomy');
const root = path.resolve(__dirname, '..');
const pub = path.join(root, 'public');
const read = route => fs.readFileSync(path.join(pub, route, 'index.html'), 'utf8');
const home = read('');
assert(home.includes('home-intro') && home.includes('current-project'), '首页缺少个人介绍或工程状态');
assert.equal((home.match(/aria-label="主导航"/g) || []).length, 1, '首页导航重复');
const about = read('about');
assert(about.includes('我是 Luyuan') && about.includes('Linux 驱动'), '关于页缺少本人工作方向');
assert(!/mailto:|tel:|联系方式|联系我/.test(about), '关于页不应包含联系方式');
const branches = Object.values(categories).reduce((count, branch) => count + Object.keys(branch).length, 0);
assert.equal((read('categories').match(/class="topic-card"/g) || []).length, branches, '分类卡片须对应现有分支');
assert(read('engineering').includes('验收计划 · 待执行'), '工程记录丢失验收状态');
for (const topic of ['i3c', 'mctp-pldm', 'pcie', 'dma']) {
  const html = read('categories/embedded-tech/' + topic);
  assert(html.includes('建议从这里开始'), `${topic} 缺少阅读顺序`);
  for (const match of html.matchAll(/href="(\/[^"#?]*)(?:[?#][^"]*)?"/g)) {
    const route = decodeURIComponent(match[1]);
    if (route.startsWith('//')) continue;
    const target = path.join(pub, route);
    assert(fs.existsSync(target), `无效站内链接：${route}`);
  }
}
let posts = 0;
function checkPosts(directory) {
  for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
    const file = path.join(directory, entry.name);
    if (entry.isDirectory()) checkPosts(file);
    else if (entry.name.endsWith('.md')) {
      const post = parse(fs.readFileSync(file, 'utf8'));
      if (!post.permalink) continue;
      assert(fs.existsSync(path.join(pub, post.permalink, 'index.html')), `文章旧地址失效：${post.permalink}`);
      posts++;
    }
  }
}
checkPosts(path.join(root, 'source/_posts'));
for (const asset of ['css/engineering.css', 'js/engineering.js', 'images/evening-bell.svg']) {
  assert(fs.statSync(path.join(pub, asset)).size > 0, `缺少资源：${asset}`);
}
const testcases = read('2026/11/27/mctp-pldm-porting-acceptance-testcases');
assert(testcases.includes('data-reading-width') && testcases.includes('engineering-related'), '文章缺少阅读工具或专题导航');
assert(testcases.includes('证据截至 2026-11-27'), '工程文章缺少明确证据日期');
console.log(`UI 检查通过：关于页、${branches} 个分类入口、4 条阅读路线、${posts} 个文章旧地址与阅读资源。`);
