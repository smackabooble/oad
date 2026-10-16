const { chromium } = require('/opt/node-tools/node_modules/playwright');
(async () => {
  const w = parseInt(process.argv[2] || '480'), h = parseInt(process.argv[3] || '270');
  const ctx = await chromium.launchPersistentContext('/tmp/pwprofile', { viewport: { width: w, height: h }, executablePath: '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--no-sandbox', '--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = ctx.pages()[0] || await ctx.newPage();
  await page.goto('http://localhost:' + (process.env.PORT || 8080) + '/index.html?args=-autostart=scenarios/arcadia%20-autostart-players=2');
  await page.waitForTimeout(parseInt(process.env.WAIT || '110000'));
  await page.keyboard.press('Alt+f'); await page.waitForTimeout(6000);
  await page.screenshot({ path: `/tmp/fps_${w}.png`, clip: { x: Math.max(0, w - 200), y: 20, width: 200, height: 60 }, timeout: 120000 });
  console.log('done', w, h);
  await ctx.close();
})();
