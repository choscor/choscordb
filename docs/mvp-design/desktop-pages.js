/* Shared pane treatment for every non-editor screen. Keep actual controls so
   the prototype's existing handlers and modal flows continue to work. */
function simplifyDesktopPage() {
  if (page === 'workspace') return;
  const main = document.querySelector('.main');
  const content = main.querySelector('.content');
  main.classList.add('document-pane');
  const footer = document.createElement('footer');
  footer.className = 'page-actionbar';
  footer.setAttribute('aria-label', 'Page actions');
  const hints = {
    index: 'PostgreSQL · SQLite', connections: 'Saved on this device',
    connection: 'Connection settings open in a dialog', schema: 'Sample metadata · read-only',
    history: 'Queries open without execution', exports: '8 sample rows · browser download',
    preferences: 'Preferences are saved on this device',
    changes: '2 pending changes · post-MVP concept', workflows: '11 linked screens · sample data'
  };
  footer.innerHTML = `<span class="page-status">${hints[page]}</span><div class="page-actions"></div>`;
  const actions = footer.querySelector('.page-actions');
  if (page === 'index') actions.innerHTML = btn(icon('plus')+' New connection','new-connection','primary');
  const move = el => { if (el) actions.append(el); };
  const moveAction = name => move(content.querySelector(`[data-action="${name}"]`));
  const heading = content.querySelector('.page-heading');
  if (heading) [...heading.children].slice(1).forEach(el => {
    if (el.matches('button,a')) move(el);
    else if (el.classList.contains('actions')) [...el.children].forEach(move);
  });

  if (page === 'connection') {
    content.querySelectorAll('.empty .actions [data-action="new-connection"]').forEach(el => el.remove());
    content.querySelectorAll('.empty .actions a,.empty .actions [data-action="focus-connections"]').forEach(move);
    content.querySelector('.empty .actions')?.remove();
  }

  if (page === 'schema') {
    content.querySelector('.grid')?.classList.add('object-summary');
    content.querySelector('.callout')?.remove();
  }
  if (page === 'history') {
    const items = [...content.querySelectorAll('.history-item')];
    const open = items[0]?.querySelector('[data-action="history-open"]');
    move(open);
    open?.classList.add('primary');
    items.forEach((item, i) => {
      item.querySelector('[data-action="history-open"]')?.remove();
      item.tabIndex = 0;
      item.setAttribute('role', 'button');
      item.setAttribute('aria-pressed', i === 0);
      item.classList.toggle('selected-history', i === 0);
      item.addEventListener('click', () => selectHistoryItem(item));
      item.addEventListener('keydown', e => {
        if (e.key === 'Enter' || e.key === ' ') { e.preventDefault(); selectHistoryItem(item); }
      });
    });
  }


  if (page === 'changes') {
    content.querySelector('.grid')?.classList.add('object-summary');
    const row = content.querySelector('[data-action="apply-changes"]').parentElement;
    row.querySelectorAll('a,button').forEach(move);
    row.remove();
  }

  // Primary actions consistently finish the bottom bar.
  [...actions.querySelectorAll(':scope > .primary')].forEach(move);
  compactDesktopContent(main, content, footer);
  main.append(footer);
}
function selectHistoryItem(item) {
  document.querySelectorAll('.history-item').forEach(row => {
    row.classList.toggle('selected-history', row === item);
    row.setAttribute('aria-pressed', row === item);
  });
}

// Utility-window layout: tabs and filters first, then the actual working surface.
function compactDesktopContent(main, content, footer) {
  main.classList.add('compact-pane');
  const heading = content.querySelector('.page-heading');
  if (heading) {
    const title = heading.querySelector('h1')?.textContent;
    const context = heading.querySelector('p')?.textContent;
    if (page === 'schema') {
      const caption = document.querySelector('.window-caption');
      caption.textContent = `${title} — ${selectedConnection.name} / ${selectedConnection.schema} — ChoscorDB`;
      caption.title = context;
      footer.querySelector('.page-status').textContent = `${selectedConnection.schema}.${title} · Sample metadata`;
    }
    heading.remove();
  }
  content.querySelectorAll('.object-summary').forEach(el => el.remove());
  if (page === 'schema') {
    const tabs = content.querySelector('.tabs');
    tabs.classList.add('pane-tabs');
    tabs.removeAttribute('style');
    main.insertBefore(tabs, content);
    content.classList.add('object-content');
  }


  if (page === 'history') {
    const filters = content.querySelector(':scope > .row');
    filters.classList.add('pane-filters'); filters.removeAttribute('style');
    main.insertBefore(filters, content);
    content.querySelector(':scope > .eyebrow')?.remove();
    const note = content.querySelector(':scope > p');
    const preferences = note?.querySelector('[data-action="open-preferences"]');
    if (preferences) footer.querySelector('.page-actions').prepend(preferences);
    note?.remove();
  }

  if (page === 'connection') {
    content.querySelector('.db-icon')?.remove();
    const empty = content.querySelector('.empty');
    empty.classList.add('connection-empty');
    empty.querySelector('h2').textContent = 'No connection open';
    empty.querySelector('p').textContent = 'Choose New connection below or select a saved connection in the sidebar.';
  }


  if (page === 'changes') {
    content.querySelector('.callout').textContent = 'Post-MVP preview · Changes require a primary key and explicit confirmation.';
    content.querySelector('.sql')?.parentElement.classList.add('change-sql');
  }

}
