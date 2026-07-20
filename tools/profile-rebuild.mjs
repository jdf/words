// In-browser rebuild profiling: cold boots (empty shape cache, fresh
// wasm instance) and warm rebuilds per font, against a running dev
// server, in the pinned headless Chrome (foreground priority — an
// interactive tab driven by automation gets background-throttled and
// its numbers are garbage). Reads the window.__wordsTiming line the
// engine posts after every build (see logStageTimings in src/main.cc).
//
//   ./dev release
//   node tools/profile-rebuild.mjs "$(tools/get-chrome.sh)" http://localhost:8787/
import puppeteer from 'puppeteer-core';

const [chrome, baseUrl] = process.argv.slice(2);
const browser = await puppeteer.launch({
  executablePath: chrome,
  headless: true,
  args: ['--hide-scrollbars'],  // real ANGLE GPU path, unlike the goldens
});

const waitIdle = async (page) => {
  const deadline = Date.now() + 120000;
  while (!(await page.evaluate('window.__wordsIdle === true'))) {
    if (Date.now() > deadline) throw new Error('idle timeout');
    await new Promise((r) => setTimeout(r, 150));
  }
};

try {
  const page = await browser.newPage();
  await page.setViewport({ width: 1200, height: 750, deviceScaleFactor: 1 });

  for (const font of ['sexsmith', 'fridge']) {
    // Cold boots, 3x (fresh page load each: shape cache empty, wasm
    // freshly instantiated — tiering warmup visible run to run).
    for (let i = 0; i < 3; i++) {
      await page.goto(
          `${baseUrl}?font=${font}&max=2000&placement=center-line` +
              `&orientation=mostly-horizontal&palette=bw&no-ui`,
          { waitUntil: 'load' });
      await waitIdle(page);
      console.log(`${font} cold-boot #${i + 1}: ` +
                  await page.evaluate('window.__wordsTiming'));
    }
    // Warm rebuilds: UI mode, everything locked, Generate = new seed.
    await page.goto(
        `${baseUrl}?font=${font}&max=2000&placement=center-line` +
            `&orientation=mostly-horizontal&palette=bw&seed=1447`,
        { waitUntil: 'load' });
    await waitIdle(page);
    for (let i = 0; i < 4; i++) {
      await page.evaluate(
          'document.getElementById("generate").click(); undefined');
      await new Promise((r) => setTimeout(r, 300));
      await waitIdle(page);
      console.log(`${font} warm #${i + 1}: ` +
                  await page.evaluate('window.__wordsTiming'));
    }
  }
} finally {
  await browser.close();
}
