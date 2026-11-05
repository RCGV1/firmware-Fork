/* Optional Umami integration. Configuration is supplied by the Pages workflow. */
(() => {
  'use strict';

  let send;
  const pending = [];
  window.fullPositionAnalytics = {
    download(event, { track, release, board }) {
      if (!event.isTrusted || ![track, release, board].every(value =>
        typeof value === 'string' && /^[A-Za-z0-9._-]+$/.test(value))) return;
      const hit = { track, release, board };
      if (send) send(hit);
      else if (pending.length < 16) pending.push(hit);
    }
  };

  function disabled() {
    const preference = new URLSearchParams(location.search).get('analytics');
    let optedOut = preference === 'off';
    try {
      if (preference === 'off') localStorage.setItem('full-position.analytics.disabled', '1');
      if (preference === 'on') localStorage.removeItem('full-position.analytics.disabled');
      optedOut ||= localStorage.getItem('full-position.analytics.disabled') === '1';
    } catch { /* Storage may be unavailable in private browsing. */ }
    return optedOut || location.origin !== 'https://rcgv1.github.io' ||
      !['/firmware-Fork/', '/firmware-Fork/index.html'].includes(location.pathname) ||
      navigator.webdriver || navigator.doNotTrack === '1' || window.doNotTrack === '1' ||
      navigator.globalPrivacyControl === true;
  }

  async function start() {
    if (disabled()) return;
    try {
      const response = await fetch('analytics.json');
      if (!response.ok) return;
      const { websiteId } = await response.json();
      if (typeof websiteId !== 'string' || !/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/i.test(websiteId)) return;
      let referrer = '';
      try { if (document.referrer) referrer = new URL(document.referrer).origin; } catch { /* Ignore invalid referrers. */ }
      window.fullPositionAnalyticsBeforeSend = (type, payload) => ({
        ...payload, url: location.pathname, referrer
      });
      const script = document.createElement('script');
      script.src = 'https://cloud.umami.is/script.js';
      script.async = true;
      script.dataset.websiteId = websiteId;
      script.dataset.autoTrack = 'false';
      script.dataset.domains = 'rcgv1.github.io';
      script.dataset.excludeSearch = 'true';
      script.dataset.excludeHash = 'true';
      script.dataset.beforeSend = 'fullPositionAnalyticsBeforeSend';
      script.addEventListener('load', () => {
        const track = (...args) => {
          try { Promise.resolve(window.umami.track(...args)).catch(() => {}); }
          catch { /* Downloads must work without analytics. */ }
        };
        send = hit => {
          track('download_click', hit);
        };
        track();
        pending.splice(0).forEach(send);
      });
      document.head.append(script);
    } catch { /* A blocked analytics service must not affect board selection or downloads. */ }
  }
  start();
})();
