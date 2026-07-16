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
  await page.setViewport({ width: 1200, height: 750, deviceScaleFactor: 1 });
  for (const c of cases) {
    const sep = c.indexOf('|');
    const name = c.slice(0, sep);
    const query = c.slice(sep + 1);
    await page.goto(baseUrl + query, { waitUntil: 'load' });
    await page.waitForFunction('window.__wordsIdle === true', { timeout: 30000 });
    await page.screenshot({ path: `${outDir}/e2e-received-${name}.png` });
    console.log(`shot: ${name}`);
  }
} finally {
  await browser.close();
}
