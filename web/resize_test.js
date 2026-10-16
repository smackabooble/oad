const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const browser = await chromium.launch({ executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
  page.on('console', m => { const t = m.text(); if (/ERROR|ABORT|Assertion/.test(t) && !/get_average/.test(t)) console.log(t.slice(0, 200)); });
  await page.goto('http://localhost:8081/index.html');
  await page.waitForTimeout(30000);
  await page.setViewportSize({ width: 900, height: 560 });
  await page.waitForTimeout(6000);
  await page.screenshot({ path: '/tmp/resize_small.png', timeout: 120000 });
  await page.setViewportSize({ width: 1500, height: 840 });
  await page.waitForTimeout(6000);
  await page.screenshot({ path: '/tmp/resize_big.png', timeout: 120000 });
  console.log('done');
  await browser.close();
})();
