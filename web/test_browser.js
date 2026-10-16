// Headless-Chromium smoke test. usage: node test_browser.js <url> [seconds] [screenshot.png]
const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const url = process.argv[2] || 'http://localhost:8080/index.html';
  const secs = parseInt(process.argv[3] || '60', 10);
  const shot = process.argv[4] || '/tmp/shot.png';
  const browser = await chromium.launch({
    executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader',
           '--ignore-gpu-blocklist', '--enable-webgl', '--enable-features=SharedArrayBuffer', ...(process.env.JSFLAGS ? ['--js-flags=' + process.env.JSFLAGS] : [])],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const T0 = Date.now(); const ts = () => ((Date.now() - T0) / 1000).toFixed(1).padStart(6) + 's';
  page.on('console', m => console.log(ts(), '[console]', m.text().slice(0, 4000)));
  page.on('pageerror', e => console.log('[pageerror]', (e.stack || String(e)).slice(0, 4000)));
  page.on('worker', w => console.log('[worker]', w.url().slice(-60)));
  await page.goto(url);
  await page.waitForTimeout(secs * 1000);
  console.log('crossOriginIsolated =', await page.evaluate(() => window.crossOriginIsolated));
  try { await page.screenshot({ path: shot, timeout: 240000 }); } catch (e) { console.log('screenshot failed:', String(e).slice(0, 200)); }
  console.log('screenshot ->', shot);
  await browser.close();
})();
