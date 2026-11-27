'use strict';

const fs = require('node:fs');
const path = require('node:path');
const { categories, articleTypes } = require('../tools/taxonomy');

// Override the view through Hexo, without changing the installed theme.
hexo.extend.filter.register('before_generate', function () {
  hexo.theme.setView('categories.ejs', fs.readFileSync(path.join(hexo.base_dir, 'layout/categories.ejs'), 'utf8'));
});

hexo.extend.helper.register('article_taxonomy', () => ({ categories, articleTypes }));
