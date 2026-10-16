const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const ctx = await chromium.launchPersistentContext('/tmp/t/prof-site', {
    executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', headless: true,
    args: ['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--enable-features=WebAssemblyExperimentalJSPI','--js-flags=--experimental-wasm-jspi'] });
  const page = await ctx.newPage();
  page.on('console', m => { const t = m.text(); console.log('[c]', t.slice(0,200)); });
  await page.goto('http://localhost:8096/oad/?debug');
  await page.waitForTimeout(240000);
  await page.screenshot({ path: '/tmp/t/site2.png' });
  await ctx.close();
})();
