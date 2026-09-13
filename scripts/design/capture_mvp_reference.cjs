#!/usr/bin/env node
// Reproducible browser evidence only: never modifies the prototype or native preferences.
const assert = require('node:assert/strict');
const fs = require('node:fs/promises');
const path = require('node:path');
const { pathToFileURL } = require('node:url');
const { createHash } = require('node:crypto');
const { execFileSync } = require('node:child_process');
const { chromium } = require(process.env.PLAYWRIGHT_MODULE || 'playwright');
const root = path.resolve(__dirname, '../..');
const source = path.join(root, 'docs/mvp-design');
const output = path.resolve(process.argv[2] || path.join(root, 'docs/design/mvp-reference'));
const hash = bytes => createHash('sha256').update(bytes).digest('hex');
const cropSelectors = {
  'workspace-results': { 'button-default': '.editor-bottom [data-action="save-sql"]', 'button-primary': '.editor-bottom [data-action="run"]', 'document-tabs': '.work-tabs' },
  'object-columns': { 'pane-tabs': '.pane-tabs', 'table-header': '#schema-detail thead', 'table-row': '#schema-detail tbody tr:first-child' },
  'preferences-editor': { 'field': '#editor-size', 'selector': '#editor-font', 'toggle': '#line-numbers', 'modal': '.preferences-dialog', 'modal-default-button': '.preferences-dialog > .actions [data-action="close-modal"]', 'modal-primary-button': '.preferences-dialog > .actions .primary' },
  'menu-keyboard': { 'menu': '.app-menu-popup' },
  'button-keyboard-focus': { 'button-focus': '.page-actions .primary' },
  'export-ready': { 'export-modal': '.export-dialog' },
};
const selectors = ['body', '.shell', '.sidebar', '.main', '.statusbar', '.work-tabs', '.document-tab.active > button', '.document-tab:not(.active) > button', '.editor', '#sql-editor', '.editor-bottom', '.editor-bottom [data-action="run"]', '.editor-bottom [data-action="cancel"]', '.result-bottom', '.result-bottom .primary', '.page-actionbar', '.page-actionbar .primary', '.pane-tabs', '.pane-tabs .active', '.connection-option.selected', '.connection-option:not(.selected)', '.tree summary.selected', '.tree summary', '.search', '.search input', 'th', 'td', '.badge.green', '.badge.red', '.modal-backdrop', '.modal', '.modal > .row:first-child', '.modal > .actions', '.modal .primary', '.modal .field input', '.modal .field select', '.modal .toggle', '.modal .settings-row', '.callout', '.callout.warn', '.app-menu-popup', '.app-menu-popup button', '.findbar', '.progress', '.icon'];

(async () => {
  await fs.mkdir(output, { recursive: true });
  await fs.mkdir(path.join(output, 'captures'), { recursive: true });
  const sources = [];
  for (const name of (await fs.readdir(source)).sort()) {
    const bytes = await fs.readFile(path.join(source, name));
    sources.push({ path: `docs/mvp-design/${name}`, bytes: bytes.length, sha256: hash(bytes) });
  }
  const browser = await chromium.launch({ executablePath: process.env.CHROMIUM_EXECUTABLE || undefined });
  const manifest = {
    schema: 1, capturedAt: new Date().toISOString(), sourceGitRef: execFileSync('git', ['rev-parse', 'HEAD'], { cwd: root, encoding: 'utf8' }).trim(),
    sources, platform: { os: execFileSync('sw_vers', [], { encoding: 'utf8' }).trim(), arch: process.arch, node: process.version, browser: await browser.version(), playwright: require(path.join(path.dirname(require.resolve(process.env.PLAYWRIGHT_MODULE || 'playwright')), 'package.json')).version, headless: true },
    fixture: 'Fresh nonpersistent browser context for each theme/viewport. Only choscor-prototype theme is seeded; all data and rendered controls are the untouched prototype defaults. Query completion uses actual Run input and its simulated timer. No browser or native user profiles are read.',
    settings: { deviceScaleFactor: 1, locale: 'en-US', timezoneId: 'Asia/Ho_Chi_Minh', reducedMotion: 'reduce', osColorSchemeMatchesTheme: true, fixedDate: '2026-09-13T02:42:18.000Z (timers run normally)' },
    comparison: { cssPixelsPerQtLogicalUnit: 1, appContentOrigin: { x: 0, y: 57 }, nativeExceptions: ['Browser prototype menu row y=0..27 corresponds to the native OS menu, excluded.', 'Browser prototype titlebar y=27..57 corresponds to native titlebar, excluded.'], contentRectangle: 'x=0,y=57,width=viewport.width,height=viewport.height-57; includes app-owned statusbar. Native capture must identify the equivalent client rectangle explicitly.' },
    captures: [], crops: [], interactions: [], errors: []
  };
  try {
    for (const viewport of [{ width: 1280, height: 900 }, { width: 960, height: 640 }]) {
      for (const theme of ['light', 'dark']) {
        const context = await browser.newContext({ viewport, deviceScaleFactor: 1, locale: 'en-US', timezoneId: 'Asia/Ho_Chi_Minh', reducedMotion: 'reduce', colorScheme: theme });
        await context.addInitScript(t => { if (!localStorage.getItem('choscor-prototype')) localStorage.setItem('choscor-prototype', JSON.stringify({ theme: t === 'dark' ? 'Dark' : 'Light' })); }, theme);
        const page = await context.newPage();
        await page.clock.setFixedTime('2026-09-13T02:42:18.000Z');
        page.on('pageerror', error => manifest.errors.push(error.message));
        const cdp = await context.newCDPSession(page);
        await cdp.send('DOM.enable'); await cdp.send('CSS.enable');
        const act = name => page.locator(`[data-action="${name}"]`).filter({ visible: true }).first().click();
        const go = async name => {
          await page.goto(pathToFileURL(path.join(source, `${name}.html`)).href);
          await page.evaluate(() => document.fonts.ready);
          await page.mouse.move(0, 0);
          assert.equal(await page.locator('.shell').count(), 1);
        };
        const capture = async (state, procedure) => {
          await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
          const measured = await page.evaluate(sels => {
            const rootStyle = getComputedStyle(document.body), tokens = {};
            for (const key of ['bg', 'panel', 'text', 'muted', 'line', 'soft', 'green', 'green-bg', 'red', 'shadow', 'mono']) tokens[key] = rootStyle.getPropertyValue(`--${key}`).trim();
            const elements = {};
            for (const selector of sels) {
              const el = [...document.querySelectorAll(selector)].find(e => e.checkVisibility());
              if (!el) continue;
              const c = getComputedStyle(el), r = el.getBoundingClientRect();
              elements[selector] = { rect: { x: r.x, y: r.y, width: r.width, height: r.height }, font: c.font, letterSpacing: c.letterSpacing, color: c.color, background: c.backgroundColor, padding: c.padding, gap: c.gap, border: { width: c.borderWidth, color: c.borderColor, style: c.borderStyle }, radius: c.borderRadius, shadow: c.boxShadow, outline: c.outline, outlineOffset: c.outlineOffset, opacity: c.opacity, minHeight: c.minHeight, overflow: c.overflow, transition: c.transition, strokeWidth: c.strokeWidth, strokeLinecap: c.strokeLinecap, strokeLinejoin: c.strokeLinejoin, focusVisible: el.matches(':focus-visible'), disabled: !!el.disabled, checked: 'checked' in el ? el.checked : undefined };
            }
            const focused = document.activeElement;
            return { tokens, elements, focus: { tag: focused.tagName, id: focused.id, action: focused.dataset.action || null, text: focused.textContent.slice(0, 100) }, shellInert: document.querySelector('.shell').inert, menuInert: document.querySelector('.mac-menubar').inert };
          }, selectors);
          const documentNode = await cdp.send('DOM.getDocument');
          const resolvedFonts = {};
          for (const selector of ['.connection-label strong', '#sql-editor', '.modal input', '.modal h2', '.history-item .mono']) {
            const { nodeId } = await cdp.send('DOM.querySelector', { nodeId: documentNode.root.nodeId, selector });
            if (nodeId) resolvedFonts[selector] = (await cdp.send('CSS.getPlatformFontsForNode', { nodeId })).fonts;
          }
          const filename = `captures/${theme}-${viewport.width}x${viewport.height}-${state}.png`;
          const bytes = await page.screenshot({ path: path.join(output, filename), animations: 'disabled' });
          const measurementFile = `captures/${theme}-${viewport.width}x${viewport.height}-${state}.json`;
          await fs.writeFile(path.join(output, measurementFile), JSON.stringify({ ...measured, resolvedFonts }) + '\n');
          if (state === 'workspace-results') {
            manifest.tokenSummary ||= { source: 'manifest.json and captures/*-workspace-results.json', cssPixelsPerQtLogicalUnit: 1, palettes: {}, baselineGeometry: {} };
            manifest.tokenSummary.palettes[theme] = measured.tokens;
            manifest.tokenSummary.baselineGeometry[`${theme}-${viewport.width}x${viewport.height}`] = Object.fromEntries(Object.entries(measured.elements).map(([key, value]) => [key, { rect: value.rect, font: value.font, padding: value.padding, radius: value.radius }]));
          }
          manifest.captures.push({ state, theme, viewport, file: filename, sha256: hash(bytes), bytes: bytes.length, measurements: measurementFile, procedure });
          for (const [name, selector] of Object.entries(cropSelectors[state] || {})) {
            const bounds = await page.locator(selector).boundingBox();
            assert.ok(bounds, `Visible crop target ${selector}`);
            // Retain focus outline and a six-pixel surrounding context; never rescale.
            const x = Math.max(0, Math.floor(bounds.x) - 6), y = Math.max(0, Math.floor(bounds.y) - 6);
            const clip = { x, y, width: Math.min(viewport.width, Math.ceil(bounds.x + bounds.width) + 6) - x, height: Math.min(viewport.height, Math.ceil(bounds.y + bounds.height) + 6) - y };
            const file = `captures/${theme}-${viewport.width}x${viewport.height}-component-${name}.png`;
            const png = await page.screenshot({ path: path.join(output, file), clip, animations: 'disabled' });
            manifest.crops.push({ name, theme, viewport, sourceState: state, selector, bounds, clip, file, sha256: hash(png) });
          }
          console.log(filename);
        };
        await go('index'); await capture('start', 'Navigate to index.html with fresh isolated context.');
        await page.locator('.page-actions .primary').hover(); await capture('button-hover', 'Hover Start New connection button.');
        await page.mouse.down(); await capture('button-pressed', 'Pointer held down on Start New connection; reference defines no separate active CSS.'); await page.mouse.up();
        assert.equal(await page.locator('.shell').evaluate(e => e.inert), true);
        await capture('connection-postgresql', 'Release Start New connection button; PostgreSQL form opens and name receives focus.');
        await page.keyboard.press('Escape');
        assert.equal(await page.locator('.modal').count(), 0);
        assert.equal(await page.locator('.page-actions .primary').evaluate(e => e === document.activeElement), true);
        await page.keyboard.press('Tab'); await page.keyboard.press('Shift+Tab');
        await capture('button-keyboard-focus', 'Close dialog by Escape, Tab then Shift+Tab to show keyboard focus on restored New connection.');
        manifest.interactions.push({ theme, viewport, name: 'connection-close-restores-invoker', passed: true });
        await act('new-connection');
        await act('test-connection'); await capture('connection-testing', 'Click Test connection; actual simulated asynchronous test, button disabled.');
        await page.locator('#connection-feedback').getByText('Connection successful', { exact: false }).waitFor();
        await capture('connection-success', 'Wait for actual prototype Test completion.');
        await page.locator('.prototype-states summary').click(); await act('connection-error');
        await capture('connection-error', 'Open Sample error state and activate Simulate authentication error.');
        await page.locator('[data-action="connection-driver"][data-driver="sqlite"]').click();
        await capture('connection-sqlite', 'Switch actual driver picker to SQLite.');
        await page.keyboard.press('Escape');
        await go('workspace'); await capture('workspace-ready', 'Navigate to workspace.html; untouched default editor document has no result.');
        await act('run'); await capture('workspace-running', 'Click Run; actual simulated run before terminal timer, Cancel visible and Run disabled.');
        await act('cancel'); await capture('workspace-cancelling', 'Click Cancel; actual 500ms pending cancellation.');
        await page.locator('#query-state').getByText('Cancelled', { exact: false }).waitFor();
        await capture('workspace-cancelled', 'Wait for actual terminal cancellation.');
        await act('run'); await page.locator('#query-state').getByText('Completed', { exact: false }).waitFor();
        await page.locator('#toast').waitFor({ state: 'hidden' });
        await page.mouse.move(0, 0); await capture('workspace-results', 'Run again and wait for Completed and toast dismissal; fixed sample rows rendered.');
        await page.keyboard.press('Meta+f'); await capture('editor-find', 'Invoke actual Find keyboard shortcut.'); await act('close-find');
        await act('open-export'); await capture('export-ready', 'Click workspace Export; actual modal defaults, initial selector focused.');
        await page.locator('.export-dialog .primary').focus(); await page.keyboard.press('Tab');
        assert.equal(await page.locator('.modal [data-action="close-modal"]').first().evaluate(e => e === document.activeElement), true);
        manifest.interactions.push({ theme, viewport, name: 'export-tab-wrap', passed: true });
        await act('export-error'); await capture('export-error', 'Activate prototype Preview export failure; terminal failure and enabled retry.');
        await page.keyboard.press('Escape');
        assert.equal(await page.locator('[data-action="open-export"]').evaluate(e => e === document.activeElement), true);
        manifest.interactions.push({ theme, viewport, name: 'export-close-restores-invoker', passed: true });
        await page.keyboard.press('Meta+,'); await capture('preferences-appearance', 'Invoke Preferences via actual macOS keyboard shortcut.');
        for (const section of ['editor', 'results', 'history', 'shortcuts']) {
          await page.locator(`.preference-tabs a[href="#${section}"]`).click();
          await capture(`preferences-${section}`, `Click Preferences ${section} tab.`);
        }
        await page.keyboard.press('Escape');
        await page.keyboard.press('Meta+k'); await capture('quick-switch', 'Invoke actual Quick switch keyboard shortcut.'); await page.keyboard.press('Escape');
        await page.locator('[data-app-menu="File"]').click(); await page.keyboard.press('ArrowDown');
        await capture('menu-keyboard', 'Open prototype File menu and select next entry with ArrowDown.'); await page.keyboard.press('Escape');
        await go('schema'); await capture('object-columns', 'Navigate to schema.html with default public.customers object.');
        for (const tab of ['Indexes', 'Keys', 'DDL', 'Data']) {
          await page.locator(`[data-action="schema-tab"][data-tab="${tab}"]`).click();
          await capture(`object-${tab.toLowerCase()}`, `Click actual ${tab} object tab.`);
        }
        await go('history'); await capture('history', 'Navigate to history.html after one actual simulated execution; prototype retains its query entry.');
        await act('clear-history'); await capture('confirmation', 'Click Clear history; capture actual confirmation modal.');
        await context.close();
      }
    }
    assert.deepEqual(manifest.errors, []);
    for (const item of sources) assert.equal(hash(await fs.readFile(path.join(root, item.path))), item.sha256, 'Capture must not modify source reference');
  } finally {
    if (manifest.tokenSummary) {
      await fs.writeFile(path.join(output, 'tokens.json'), JSON.stringify(manifest.tokenSummary, null, 2) + '\n');
      delete manifest.tokenSummary;
    }
    await fs.writeFile(path.join(output, 'manifest.json'), JSON.stringify(manifest, null, 2) + '\n');
    await browser.close();
  }
})();
