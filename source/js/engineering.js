'use strict';

(() => {
  function init() {
    let wide = false;
    try { wide = localStorage.getItem('blog-reading-wide') === 'true'; } catch (_) {}
    document.documentElement.dataset.reading = wide ? 'wide' : 'normal';
    document.querySelectorAll('[data-reading-width]').forEach(button => {
      button.setAttribute('aria-pressed', String(wide));
      button.textContent = wide ? '恢复标准宽度' : '宽屏阅读';
    });
    document.querySelectorAll('article.content table').forEach((table, index) => {
      if (table.closest('.engineering-table-wrap')) return;
      const headers = [...table.querySelectorAll('thead th')].map(th => th.textContent.trim());
      const wrap = document.createElement('div');
      wrap.className = 'engineering-table-wrap' + (headers.length >= 5 ? ' wide-columns' : '');
      wrap.tabIndex = 0;
      wrap.setAttribute('role', 'region');
      wrap.setAttribute('aria-label', headers.length ? headers.join('、') + '表格' : '可滚动表格');
      table.before(wrap); wrap.append(table);
      const hint = document.createElement('p');
      hint.className = 'table-scroll-hint'; hint.textContent = '左右滑动查看完整表格';
      wrap.before(hint);
      const updateHint = () => { hint.hidden = table.scrollWidth <= wrap.clientWidth + 1; };
      if (typeof ResizeObserver !== 'undefined') new ResizeObserver(updateHint).observe(wrap);
      updateHint();

      const rows = [...table.querySelectorAll('tbody tr')];
      if (headers[0] !== '用例' || !rows.some(row => /^TC-\d/.test(row.cells[0]?.textContent || ''))) return;
      const group = document.createElement('section');
      group.className = 'testcase-switch'; group.dataset.view = 'cards';
      wrap.before(group);
      const controls = document.createElement('div'); controls.className = 'testcase-view-tools';
      const cards = document.createElement('div'); cards.className = 'testcase-cards'; cards.id = `testcase-cards-${index}`;
      wrap.id = `testcase-table-${index}`;
      for (const [view, label, id] of [['cards', '逐项阅读', cards.id], ['table', '完整表格', wrap.id]]) {
        const button = document.createElement('button'); button.type = 'button'; button.textContent = label;
        button.setAttribute('aria-controls', id); button.setAttribute('aria-pressed', String(view === 'cards'));
        button.addEventListener('click', () => {
          group.dataset.view = view;
          [...controls.children].forEach(b => b.setAttribute('aria-pressed', String(b === button)));
          updateHint();
        });
        controls.append(button);
      }
      for (const row of rows) {
        const cells = [...row.cells]; const detail = document.createElement('details');
        const summary = document.createElement('summary');
        summary.textContent = cells.slice(0, 2).map(cell => cell.textContent.trim()).join(' · ');
        detail.append(summary);
        cells.slice(2).forEach((cell, i) => {
          const label = document.createElement('h4'); label.textContent = headers[i + 2];
          const text = document.createElement('p'); text.textContent = cell.textContent.trim();
          detail.append(label, text);
        });
        cards.append(detail);
      }
      group.append(controls, hint, wrap, cards);
    });
  }

  document.addEventListener('click', event => {
    const widthButton = event.target.closest('[data-reading-width]');
    if (widthButton) {
      const wide = document.documentElement.dataset.reading !== 'wide';
      try { localStorage.setItem('blog-reading-wide', String(wide)); } catch (_) {}
      document.documentElement.dataset.reading = wide ? 'wide' : 'normal';
      widthButton.setAttribute('aria-pressed', String(wide));
      widthButton.textContent = wide ? '恢复标准宽度' : '宽屏阅读';
    }
    if (event.target.closest('[data-site-search]')) {
      if (innerWidth <= 667 && !document.querySelector('.l_body')?.hasAttribute('leftbar')) {
        if (typeof sidebar !== 'undefined') sidebar.leftbar();
      }
      const input = document.getElementById('search-input');
      // Initialize directly: a mobile sidebar can focus the input before the
      // theme's deferred focus handler has attached.
      if (input && typeof searchFunc === 'function') {
        searchFunc('/search.json', input.dataset.filter || '', 'search-wrapper', 'search-input', 'search-result');
      }
      input?.focus();
    }
  });
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', init);
  else init();
  document.addEventListener('pjax:complete', init);
})();
