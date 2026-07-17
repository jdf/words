// e2e screenshot driver: one pinned Chrome for Testing instance serves
// every golden case. For each name|query pair, navigate, wait for the
// engine's idle flag (set by web/index.html when the worker reports the
// scene drawn), and capture a PNG. Driven by tools/e2e-golden.sh.
//
//   node tools/e2e-shot.mjs <chrome> <base-url> <out-dir> name|query...
import puppeteer from 'puppeteer-core';

const [chrome, baseUrl, outDir, ...cases] = process.argv.slice(2);

// No --disable-gpu: OffscreenCanvas frames from the worker reach the
// compositor through the GPU process (where SwiftShader runs); disabling
// it captures only blank pages. SwiftShader keeps rasterization on the
// CPU and bit-exact either way.
const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: ['--use-angle=swiftshader', '--hide-scrollbars'],
});
try {
  const page = await browser.newPage();
  // SHOT_VIEWPORT=WxH overrides for one-off renders (e.g. the 1200x630
  // social card); the golden harness never sets it.
  const [vw, vh] = (process.env.SHOT_VIEWPORT || '1200x750')
      .split('x').map(Number);
  await page.setViewport({ width: vw, height: vh, deviceScaleFactor: 1 });
  for (const c of cases) {
    const sep = c.indexOf('|');
    const name = c.slice(0, sep);
    const query = c.slice(sep + 1);
    await page.goto(baseUrl + query, { waitUntil: 'load' });
    // Poll from this side, not via waitForFunction: its rAF-based polling
    // starves in new headless once the page goes quiet (frames are only
    // produced on demand), deadlocking against an already-true flag.
    const deadline = Date.now() + 30000;
    while (!(await page.evaluate('window.__wordsIdle === true'))) {
      if (Date.now() > deadline) throw new Error(`timeout waiting for ${name}`);
      await new Promise((r) => setTimeout(r, 150));
    }
    // The flag is set when the engine reports drawn; give the worker's
    // canvas commit a beat to reach the compositor before capturing.
    await new Promise((r) => setTimeout(r, 200));
    // SHOT_HIDE=<selector> blanks page chrome for one-off renders (the
    // social card hides #status); the golden harness never sets it.
    if (process.env.SHOT_HIDE) {
      await page.evaluate((sel) => {
        document.querySelectorAll(sel).forEach((el) => {
          el.style.display = 'none';
        });
      }, process.env.SHOT_HIDE);
    }
    await page.screenshot({ path: `${outDir}/e2e-received-${name}.png` });
    console.log(`shot: ${name}`);
  }
} finally {
  await browser.close();
}
