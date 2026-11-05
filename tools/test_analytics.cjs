const { test } = require('node:test');
const assert = require('node:assert/strict');
const { readFileSync } = require('node:fs');
const { resolve } = require('node:path');
const vm = require('node:vm');

const code = readFileSync(resolve(__dirname, '../analytics.js'), 'utf8');
const selection = { track: 'release', release: 'release-v2.8.2.abc-p123', board: 'heltec-v3' };
const websiteId = '94db1cb1-74f4-4a40-ad6c-962362670409';

async function browser({ websiteId: id = websiteId, origin = 'https://rcgv1.github.io',
  pathname = '/firmware-Fork/', search = '', navigator = {}, storage = new Map(),
  fetchError = false, storageError = false } = {}) {
  const scripts = [], hits = [];
  let requests = 0;
  const window = {};
  const context = {
    window, URL, URLSearchParams, location: { origin, pathname, search }, navigator,
    localStorage: {
      getItem: key => { if (storageError) throw Error('blocked'); return storage.get(key); },
      setItem: (key, value) => storage.set(key, value),
      removeItem: key => storage.delete(key)
    },
    fetch: async () => {
      requests++;
      if (fetchError) throw Error('offline');
      return { ok: true, json: async () => ({ websiteId: id }) };
    },
    document: {
      referrer: 'https://example.com/private/path?secret=value',
      createElement: () => ({ dataset: {}, addEventListener(name, callback) { this[name] = callback; } }),
      head: { append: script => scripts.push(script) }
    }
  };
  vm.runInNewContext(code, context);
  await new Promise(setImmediate);
  return { window, scripts, hits, requests, storage,
    load() {
      window.umami = { track: (name, data) => {
        if (name) hits.push({ name, data: JSON.parse(JSON.stringify(data)) });
        return Promise.resolve();
      } };
      scripts[0].load();
    }
  };
}

test('production loads the configured counter without query strings or referring paths', async () => {
  const b = await browser({ search: '?private=value' });
  assert.equal(b.scripts.length, 1);
  assert.equal(b.scripts[0].src, 'https://cloud.umami.is/script.js');
  assert.equal(b.scripts[0].dataset.websiteId, websiteId);
  assert.equal(b.scripts[0].dataset.autoTrack, 'false');
  const payload = b.window.fullPositionAnalyticsBeforeSend('event', {
    url: '/firmware-Fork/?secret=value', referrer: 'https://example.com/private', name: 'download_click'
  });
  assert.equal(payload.url, '/firmware-Fork/');
  assert.equal(payload.referrer, 'https://example.com');
  assert.equal(payload.name, 'download_click');
});

test('no account or an invalid account cannot load a collector', async () => {
  for (const websiteId of [null, '', 'https://other.example/count', 'not-a-uuid']) {
    const b = await browser({ websiteId });
    assert.equal(b.scripts.length, 0);
  }
});

test('local previews, bots and browser privacy preferences send nothing', async () => {
  for (const settings of [
    { origin: 'http://localhost:8000' }, { pathname: '/another-project/' },
    { navigator: { webdriver: true } }, { navigator: { doNotTrack: '1' } },
    { navigator: { globalPrivacyControl: true } }
  ]) {
    const b = await browser(settings);
    assert.equal(b.requests, 0);
  }
});

test('owner exclusion persists across visits and can be cleared', async () => {
  const storage = new Map();
  assert.equal((await browser({ search: '?analytics=off', storage })).requests, 0);
  assert.equal((await browser({ storage })).requests, 0);
  assert.equal((await browser({ search: '?analytics=on', storage })).scripts.length, 1);
});

test('one trusted click records the exact board, release and track', async () => {
  const b = await browser();
  b.load();
  b.window.fullPositionAnalytics.download({ isTrusted: true }, selection);
  assert.deepEqual(b.hits, [{ name: 'download_click', data: selection }]);
});

test('a click before the counter loads is delivered once after loading', async () => {
  const b = await browser();
  b.window.fullPositionAnalytics.download({ isTrusted: true }, selection);
  assert.equal(b.hits.length, 0);
  b.load();
  assert.equal(b.hits.length, 1);
});

test('synthetic clicks and invalid dimensions cannot inflate events', async () => {
  const b = await browser();
  b.load();
  b.window.fullPositionAnalytics.download({ isTrusted: false }, selection);
  b.window.fullPositionAnalytics.download({ isTrusted: true }, { ...selection, board: 'invalid/path' });
  b.window.fullPositionAnalytics.download({ isTrusted: true }, { ...selection, board: undefined });
  assert.equal(b.hits.length, 0);
});

test('blocked storage or network and a failed counter cannot interrupt a click', async () => {
  const offline = await browser({ fetchError: true });
  assert.equal(offline.scripts.length, 0);
  const b = await browser({ storageError: true });
  b.load();
  b.window.umami.track = () => { throw Error('blocked'); };
  assert.doesNotThrow(() => b.window.fullPositionAnalytics.download({ isTrusted: true }, selection));
});

function downloadRow(analytics) {
  const nodes = [], clicks = [];
  if (analytics === undefined) analytics = { download: (...args) => clicks.push(args) };
  function element(tag) {
    const node = { tag, children: [], listeners: {}, value: '',
      append(...children) { this.children.push(...children); },
      replaceChildren(...children) { this.children = children; },
      setAttribute(key, value) { this[key] = value; },
      removeAttribute(key) { delete this[key]; },
      addEventListener(name, callback) { this.listeners[name] = callback; }
    };
    nodes.push(node);
    return node;
  }
  const container = element('div');
  const context = vm.createContext({
    window: { fullPositionAnalytics: analytics },
    document: { createElement: element, getElementById: () => container },
    fetch: () => new Promise(() => {})
  });
  const html = readFileSync(resolve(__dirname, '../index.html'), 'utf8');
  const script = html.match(/<script>([\s\S]*?)<\/script>/)[1];
  vm.runInContext(script + '\nglobalThis.renderForTest = renderTrack;', context);
  const release = { tag_name: selection.release, name: 'Test release', published_at: '2026-11-01',
    html_url: 'https://github.com/RCGV1/firmware-Fork/releases',
    assets: ['heltec-v3', 'rak4631'].map(board => ({
      name: `firmware-${board}-${selection.release}.zip`, browser_download_url: `https://example.com/${board}.zip`
    }))
  };
  context.renderForTest({ id: 'release', title: 'Latest release', description: '' }, release);
  return { clicks, select: nodes.find(n => n.tag === 'select'),
    input: nodes.find(n => n.tag === 'input'), download: nodes.find(n => n.className === 'download') };
}

test('changing boards and filtering cannot attribute a click to a stale selection', () => {
  const row = downloadRow();
  row.select.value = 'https://example.com/heltec-v3.zip';
  row.select.listeners.change();
  row.download.listeners.click({ type: 'click', isTrusted: true });
  assert.equal(row.clicks[0][1].board, 'heltec-v3');
  assert.equal(row.clicks[0][1].release, selection.release);
  row.select.value = 'https://example.com/rak4631.zip';
  row.select.listeners.change();
  row.download.listeners.auxclick({ type: 'auxclick', button: 1, isTrusted: true });
  assert.equal(row.clicks[1][1].board, 'rak4631');
  row.download.listeners.auxclick({ type: 'auxclick', button: 2, isTrusted: true });
  assert.equal(row.clicks.length, 2);
  row.input.value = 'no-matching-board';
  row.input.listeners.input();
  row.download.listeners.click({ type: 'click', isTrusted: true });
  assert.equal(row.clicks.length, 2);
  assert.equal(row.download.hidden, true);
  assert.equal(row.download.href, undefined);
});

test('downloads still have their correct URL when analytics is unavailable', () => {
  const row = downloadRow(null);
  row.select.value = 'https://example.com/heltec-v3.zip';
  row.select.listeners.change();
  assert.doesNotThrow(() => row.download.listeners.click({ type: 'click', isTrusted: true }));
  assert.equal(row.download.href, 'https://example.com/heltec-v3.zip');
});
