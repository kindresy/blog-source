'use strict';

const fs = require('node:fs');
const path = require('node:path');
const profile = require('../tools/site-profile');
const { articleTypes } = require('../tools/taxonomy');

const names = values => values?.toArray ? values.toArray().map(x => x.name) : [];
const slug = post => String(post.path || '').replace(/\/index\.html$/, '/').split('/').filter(Boolean).pop();
const topicFor = post => profile.topics.find(t => names(post?.categories).includes(t.name) || post?.category === t.name);
const reading = topic => {
  if (!topic) return [];
  const all = hexo.locals.get('posts').toArray().filter(p => names(p.categories).includes(topic.name));
  return all.sort((a, b) => {
    const ia = topic.order.indexOf(slug(a)), ib = topic.order.indexOf(slug(b));
    return (ia < 0 ? 1000 : ia) - (ib < 0 ? 1000 : ib) || +a.date - +b.date;
  });
};

hexo.extend.helper.register('engineering_profile', () => profile);
hexo.extend.helper.register('engineering_topic', topicFor);
hexo.extend.helper.register('engineering_slug', slug);
hexo.extend.helper.register('engineering_reading', reading);
hexo.extend.helper.register('engineering_meta', post => ({
  type: articleTypes[names(post.tags).find(t => articleTypes[t])] || '文章',
  evidence: profile.evidence[slug(post)],
  topic: topicFor(post)
}));

hexo.extend.filter.register('before_generate', () => {
  const views = {
    'index.ejs': 'index.ejs',
    'engineering.ejs': 'engineering.ejs',
    'engineering/category-card.ejs': 'category-card.ejs',
    'engineering/topic-intro.ejs': 'topic-intro.ejs',
    '_partial/main/navbar/nav_tabs_blog.ejs': 'nav.ejs',
    '_partial/main/post_list/post_card.ejs': 'post-card.ejs',
    '_partial/main/navbar/article_banner.ejs': 'article-banner.ejs',
    '_partial/main/navbar/dateinfo.ejs': 'dateinfo.ejs',
    '_partial/widgets/related.ejs': 'related.ejs',
    '_partial/widgets/recent.ejs': 'recent.ejs'
  };
  // Preserve the theme banner's existing image, breadcrumb and wiki behavior.
  hexo.theme.setView('_partial/main/navbar/original_article_banner.ejs', fs.readFileSync(path.join(hexo.theme_dir, 'layout/_partial/main/navbar/article_banner.ejs'), 'utf8'));
  for (const [view, file] of Object.entries(views)) {
    hexo.theme.setView(view, fs.readFileSync(path.join(hexo.base_dir, 'layout/engineering', file), 'utf8'));
  }
});

hexo.extend.injector.register('head_end', '<link rel="stylesheet" href="/css/engineering.css?v=20261127-1">');
hexo.extend.injector.register('body_end', '<script defer src="/js/engineering.js?v=20261127-1"></script>');
