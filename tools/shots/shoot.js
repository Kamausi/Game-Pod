// Free-camera asset shots: node shoot.js name:px,py,pz,tx,ty,tz[,fov] ...   (y = metres above ground)
const { chromium } = require('playwright-core');
(async () => {
  const browser = await chromium.launch({ executablePath: process.env.CHROME || '/opt/pw-browsers/chromium-1194/chrome-linux/chrome',
    args: ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader', '--ignore-gpu-blocklist'] });
  const page = await browser.newPage({ viewport: { width: 960, height: 540 } });
  const errs = []; page.on('pageerror', (e) => errs.push(e.message));
  page.on('console', (m) => { if (/error|gl_|\[cad\]/i.test(m.text())) errs.push(m.text().slice(0, 200)); });
  await page.goto(process.env.GAME_URL || 'http://localhost:8787/', { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => window.Module && Module._mb_hud && document.getElementById('loading').classList.contains('hide'), null, { timeout: 300000 });
  const act = (a) => page.evaluate((a) => Module.ccall('mb_action', null, ['string'], [a]), a);
  await page.evaluate(() => { api.resize(960, 540); startSolo(0); });
  await page.waitForTimeout(2500);
  await page.evaluate(() => { for (const id of ['hud', 'announce', 'coach']) { const e = document.getElementById(id); if (e) e.style.display = 'none'; } });
  await act('warp:-170,-170,0');   // park the truck far off-camera
  for (const spec of process.argv.slice(2)) {
    const [name, cam] = spec.split(':');
    await act('freecam:' + cam);
    await page.waitForTimeout(5000);
    await page.screenshot({ path: `${process.env.SHOTS_DIR || 'shots'}/fc_${name}.png`, timeout: 180000 });
  }
  console.log('errors', JSON.stringify([...new Set(errs)]));
  await browser.close();
})();
