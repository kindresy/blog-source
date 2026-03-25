# blog-source

This repository is the Hexo source repository for the blog. It is used for writing posts, updating configuration, and maintaining the theme and dependencies.

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
