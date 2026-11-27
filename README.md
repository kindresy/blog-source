# blog-source

This repository is the Hexo source repository for the blog. It is used for writing posts, updating configuration, and maintaining the theme and dependencies.

## 文章分类与标签

- 嵌入式开发：I3C、MCTP 与 PLDM、PCIe、I2C、DMA、SD。
- AI：Claude Code、OpenClaw。
- 生活记录：身心与状态、旅行与见闻。

完整分类树在 `tools/taxonomy.js` 中维护，博客分类首页自动按该树展示层级和文章数量。Front Matter 的 `categories` 数组按从一级分类到末级方向填写，例如 `[嵌入式开发, MCTP 与 PLDM, 移植与回归]` 或 `[生活记录, 身心与状态]`。新增分类时，同时为 `_config.yml` 的 `category_map` 配置稳定的英文路径。

源码目录与分类对应，分别位于 `source/_posts/embedded/`、`source/_posts/ai/`、`source/_posts/life-record/`，再按专题和内容方向细分。图片、交互图和证据附件跟随同名文章目录保存。

标签使用小写英文，多个单词用连字符连接，例如 `linux`、`dmaengine`、`claude-code`。每篇文章还需选择一种类型标签：`tutorial`（教程）、`code-analysis`（源码分析）、`milestone`（工程里程碑）、`reading-notes`（阅读笔记）。阅读笔记不表示文章一定为转载；转载来源继续在正文中保留。

每篇文章都要填写标题、日期、完整分类、标签和固定 `permalink`。移动旧文章时保留原有网址，并一起移动配图目录。

部署前运行 `npm run check:content`，再检查正文是否自然，有没有残留的对话过程或工具日志。自动检查只用于查漏，不能代替阅读。

## Repository Roles

There are two different GitHub repositories in this blog setup, and they serve different purposes.

### 1. Source Repository

Repository:
`https://github.com/kindresy/blog-source`

Purpose:
- Store Hexo source files
- Track article history
- Maintain theme, config, and dependencies

Typical contents:
- `source/`
- `themes/`
- `scaffolds/`
- `package.json`
- `package-lock.json`
- `_config.yml`

Daily work such as writing posts, editing config, and committing changes should happen in this repository.

### 2. Deployment Repository

Repository:
`https://github.com/kindresy/kindresy.github.io`

Purpose:
- Store generated static site files
- Serve the site through GitHub Pages

This repository is the publish target of Hexo deploy. It is not the place to maintain Hexo source code.

## Current Deployment Config

The deploy target is defined in [`_config.yml`](/home/luyuan/SambaShare/bokes/_config.yml):

```yml
deploy:
  type: git
  repo: https://github.com/kindresy/kindresy.github.io.git
  branch: master
```

That means:
- `git push` pushes Hexo source code to `blog-source`
- `hexo deploy` pushes generated static files to `kindresy.github.io`

## Daily Workflow

### Update source code

Use these commands when you write posts or modify the project:

```bash
git add .
git commit -m "update blog content"
git push
```

This pushes your source changes to:
`https://github.com/kindresy/blog-source`

### Preview locally

```bash
npm install
npm run server
```

Or:

```bash
npx hexo server
```

### Build locally

```bash
npm run build
```

This generates the static files into `public/`.

### Deploy the site

```bash
npx hexo deploy
```

Or a more complete flow:

```bash
npx hexo clean
npx hexo generate
npx hexo deploy
```

This publishes the generated site to:
`https://github.com/kindresy/kindresy.github.io`

## Important Notes

- Do not push Hexo source files to `kindresy.github.io`
- Do not use `blog-source` as the Hexo `deploy.repo`
- Do not commit `public/`, `node_modules/`, or deployment output into the source repository
- Keep `blog-source` for source code, and keep `kindresy.github.io` for generated static files only

## Quick Reminder

- `blog-source`: source repository
- `kindresy.github.io`: deployment repository
- `git push`: push source code
- `hexo deploy`: publish static site
