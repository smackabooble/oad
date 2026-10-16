// Interaction smoke test: start a match, click a unit, toggle the FPS counter, scroll the camera.
const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const url = process.argv[2] || 'http://localhost:8080/index.html?args=-autostart=scenarios/arcadia%20-autostart-players=2';
  const browser = await chromium.launch({
    executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'],
  });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  const T0 = Date.now(); const ts = () => ((Date.now() - T0) / 1000).toFixed(0).padStart(4) + 's';
  page.on('console', m => { const t = m.text(); if (/ERROR|ABORT|Assertion/.test(t)) console.log(ts(), t.slice(0, 200)); });
  page.on('pageerror', e => console.log(ts(), '[pageerror]', String(e).slice(0, 200)));
  await page.goto(url);
  await page.waitForTimeout(35000);
  const shot = async n => { try { await page.screenshot({ path: `/tmp/int_${n}.png`, timeout: 120000 }); console.log(ts(), 'shot', n); } catch (e) { console.log('shot failed', n); } };
  await shot('0_start');
  // click on the cavalry/infantry group near the temple
  await page.mouse.move(780, 440); await page.waitForTimeout(1500);
  await page.mouse.click(780, 440); await page.waitForTimeout(3000);
  await shot('1_clicked');
  // toggle FPS overlay (Alt+F in default keys is not guaranteed); press a few keys
  await page.keyboard.press('Alt+f'); await page.waitForTimeout(3000);
  // scroll camera with arrow keys
  await page.keyboard.down('ArrowRight'); await page.waitForTimeout(2500); await page.keyboard.up('ArrowRight');
  await page.mouse.wheel(0, -400); await page.waitForTimeout(2500);
  await shot('2_moved');
  await browser.close();
})();
