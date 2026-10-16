const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const ctx = await chromium.launchPersistentContext('/tmp/t/prof-site', {
    executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome', headless: true,
    args: ['--use-gl=angle','--use-angle=swiftshader','--enable-unsafe-swiftshader','--enable-features=WebAssemblyExperimentalJSPI','--js-flags=--experimental-wasm-jspi'] });
  const page = await ctx.newPage();
  page.on('console', m => { const t = m.text(); if (/ERROR|mainmenu|Error/.test(t)) console.log('[c]', t.slice(0,200)); });
  await page.goto('http://localhost:8096/');
  await page.waitForSelector('#go', { state: 'visible', timeout: 60000 });
  console.log('status:', await page.textContent('#status'));
  await page.click('#go');
  const t0 = Date.now();
  while (Date.now() - t0 < 900000) {
    await page.waitForTimeout(5000);
    const p = await page.textContent('#ptxt').catch(()=> '');
    console.log(p);
    if (/Error/.test(p)) break;
    if (await page.textContent('#status').then(s => /Starting the engine/.test(s)).catch(()=>false)) break;
  }
  await page.waitForTimeout(120000);
  await page.screenshot({ path: '/home/user/oad/web/screenshot-site.png' });
  console.log('shot');
  await ctx.close();
})();
