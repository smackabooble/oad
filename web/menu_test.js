const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const url = process.argv[2] || 'http://localhost:8081/index.html';
  const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const T0 = Date.now(); const ts = () => ((Date.now() - T0) / 1000).toFixed(0).padStart(4) + 's';
  page.on('console', m => { const t = m.text(); if (/ERROR|ABORT|Assertion|JavaScript/.test(t) && !/get_average/.test(t)) console.log(ts(), t.slice(0, 220)); });
  page.on('pageerror', e => console.log(ts(), '[pageerror]', String(e).slice(0, 200)));
  const shot = async n => { try { await page.screenshot({ path: `/tmp/menu_${n}.png`, timeout: 120000 }); console.log(ts(), 'shot', n); } catch (e) { console.log('shot failed', n); } };
  await page.goto(url);
  await page.waitForTimeout(30000);
  await shot('0');
  await page.mouse.click(496, 553); await page.waitForTimeout(4000);          // welcome dialog: OK
  await page.mouse.click(180, 231); await page.waitForTimeout(6000);          // Single-player
  await shot('1_singleplayer');
  await page.mouse.click(180, 167); await page.waitForTimeout(1000);
  await browser.close();
})();
