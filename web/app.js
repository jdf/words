// The page is pure UI: the engine (wasm + WebGL) lives in a worker with an
// OffscreenCanvas, so layout never blocks this thread. The engine posts
// progress/idle; we relay prints and gate inputs.
//
// Interactivity model: the cloud spec has four style dimensions (font,
// layout, orientation, palette), each either *fixed* (locked — Generate
// keeps it) or *random* (Generate rolls it). The dropdown menus are the
// locks: choosing a specific value locks the dimension, choosing 🎲
// Random unlocks it. URL parameters are read at boot — `font=kenyan` is
// locked, `font=~kenyan` is random-mode currently showing kenyan — so a
// deep link reproduces a cloud exactly, but the address bar is never
// rewritten: a bare visit stays bare and the site rolls its own choices.
// (Feedback reports carry a specUrl() repro link instead.)
'use strict';

const status = document.getElementById('status');
const progress = document.getElementById('progress');
const canvas = document.getElementById('canvas');
const params = new URLSearchParams(location.search);

// The toolbar is on by default; tests (and anyone wanting a bare canvas)
// pass ?no-ui. ?ui is still accepted as a no-op.
const showUi = !params.has('no-ui');

// Show the sidebar before measuring the canvas: it takes layout space,
// and the OffscreenCanvas is sized from the measurement below. The
// with-ui class shifts the fixed status line clear of the sidebar.
if (showUi) {
  document.getElementById('panel').style.display = 'flex';
  document.body.classList.add('with-ui');
}

// ---------------------------------------------------------------------------
// The style catalog: display data mirroring the engine's tables
// (src/orientation.cc, src/layout.cc, src/palette.cc, the Basic Latin
// list in assets/fonts/capabilities.txt; family names from the fonts'
// own name tables).

const FONTS = [
  ['sexsmith', 'Sexsmith'],
  ['berylium', 'Berylium'],
  ['chunkfive', 'ChunkFive'],
  ['coolvetica', 'Coolvetica'],
  ['duality', 'Duality'],
  ['enamel', 'Enamel Brush'],
  ['exprswy_free', 'Expressway Free'],
  ['fridge', 'AlphaFridgeMagnetsAllCap'],
  ['gnuolane', 'Gnuolane'],
  ['goudy', 'Goudy Bookletter 1911'],
  ['jblack', 'JSL Blackletter'],
  ['kenyan', 'Kenyan Coffee'],
  ['king', 'Loved by the King'],
  ['leaguegothic', 'League Gothic'],
  ['mailrays', 'Mail Ray Stuff'],
  ['melochergbold', 'Meloche'],
  ['opensansbold', 'Open Sans'],
  ['owned', 'Owned'],
  ['powell', 'Powell Antique'],
  ['primerprintmedium', 'Primer Print'],
  ['steelfish', 'Steelfish'],
  ['tanklite', 'Tank'],
  ['teen', 'Teen'],
  ['telephoto', 'Telephoto'],
  ['vigo', 'Vigo'],
];

const ORIENTATIONS = [
  ['mostly-horizontal', 'Mostly Horizontal'],
  ['horizontal', 'Horizontal'],
  ['long-horizontal-likely', 'Long Horizontal Likely'],
  ['half-and-half', 'Half And Half'],
  ['mostly-vertical', 'Mostly Vertical'],
  ['vertical', 'Vertical'],
  ['any-which-way', 'Any Which Way'],
];

const PLACEMENTS = [
  ['center-line', 'Center Line'],
  ['vertical-center-line', 'Vertical Center Line'],
  ['center', 'Center'],
  ['square', 'Square'],
  ['alphabetical', 'Alphabetical'],
];

// [slug, label, background, word colors]. '' = the built-in dark scheme.
const PALETTES = [
  ['', 'App Colors', '#17171c',
   ['#EDEDE6', '#5CB8DB', '#F5A624', '#8CD44D', '#CC73C7', '#D9544F']],
  ['bw', 'BW', '#ffffff', ['#000000']],
  ['wb', 'WB', '#000000', ['#ffffff']],
  ['wordly', 'Wordly', '#ffffff',
   ['#880099', '#339922', '#993333', '#2266CC']],
  ['asparagus', 'Asparagus', '#ffffff',
   ['#CCFFCC', '#CCCC7E', '#727E4C', '#B1BD35']],
  ['bluesugar', 'BlueSugar', '#ffffff',
   ['#FFCC66', '#999999', '#9999CC', '#CCCCFF']],
  ['heat', 'Heat', '#ffffff',
   ['#CCCCCC', '#996666', '#660000', '#330000', '#666666']],
  ['ghostly', 'Ghostly', '#ffffff',
   ['#000000', '#333333', '#666666', '#888888']],
  ['chilled-summer', 'Chilled Summer', '#000000',
   ['#8DC3F2', '#CBE4F8', '#F2F2F2', '#8CBF1F', '#7AA61B']],
  ['blue-meets-orange', 'Blue Meets Orange', '#000000',
   ['#023059', '#3F7EA6', '#F2F2F2', '#D99E32', '#BF5E0A']],
  ['yramirp', 'yramirP', '#000000', ['#ff0000', '#00ff00', '#0000ff']],
];

// Custom palettes ride in spec.palette (and ?palette=, copy links, the
// engine call) as "custom:RRGGBB:RRGGBB,..." — background, then equally
// weighted word colors; src/palette.cc parses the same format. Saved
// palettes are {name, value} pairs in localStorage, listed in the
// Palette menu alongside the built-ins.
const isCustomPalette = (v) =>
    typeof v === 'string' && v.startsWith('custom:');
const CUSTOM_PALETTE_RE =
    /^custom:([0-9a-fA-F]{6}):([0-9a-fA-F]{6}(?:,[0-9a-fA-F]{6})*)$/;
function parseCustomPalette(value) {
  const m = CUSTOM_PALETTE_RE.exec(value || '');
  if (!m) return null;
  return {
    bg: '#' + m[1].toLowerCase(),
    colors: m[2].toLowerCase().split(',').map((c) => '#' + c),
  };
}
const serializeCustomPalette = (p) =>
    'custom:' + p.bg.slice(1) + ':' +
    p.colors.map((c) => c.slice(1)).join(',');

const SAVED_PALETTES_KEY = 'words-custom-palettes';
function savedPalettes() {
  try {
    const list = JSON.parse(localStorage.getItem(SAVED_PALETTES_KEY)) || [];
    return list.filter((p) => p && typeof p.name === 'string' &&
                              parseCustomPalette(p.value));
  } catch {
    return [];
  }
}
function storeSavedPalettes(list) {
  try {
    localStorage.setItem(SAVED_PALETTES_KEY, JSON.stringify(list));
  } catch (err) {
    console.error('saving palettes failed', err);
  }
}

// Local fonts: the user's own .ttf/.otf files, session-scoped. Each
// gets an id; spec.font carries "local:<id>", the bytes ride to the
// worker once (see sendSpec) and stage into MEMFS. Not reproducible by
// URL — Copy Link hides while one is in use, like user text.
const localFonts = new Map();  // id -> {name, bytes}
let localFontSeq = 0;
const isLocalFont = (v) => typeof v === 'string' && v.startsWith('local:');
const localFontId = (v) => v.slice('local:'.length);
const stagedLocalFonts = new Set();  // ids the worker already holds

// What 🎲 draws from. Orientation keeps a curated pool (the wilder
// strategies are opt-in); palettes exclude App Colors so Generate always
// shows an original palette.
// The original's Color Variance menu: how far each word's color may
// wander from its palette color (hue for chromatic colors, brightness
// for achromatic; saturation never moves). Slider index = table order;
// [slug, short label, full original menu label].
const VARIANCES = [
  ['exact', 'Exact', 'Exact Palette Colors'],
  ['little', 'A Little', 'A Little Variance'],
  ['some', 'Some', 'Some Variance'],
  ['lots', 'Lots', 'Lots of Variance'],
  ['wild', 'Wild', 'Wild Variance'],
];

// The original's Case menu: how case variants of a word combine. Always
// a fixed choice — Generate never rolls it (no 🎲 entry).
const CASE_FOLDS = [
  ['guess', 'Guess Case'],
  ['as-written', 'As Written'],
  ['lower', 'lowercase'],
  ['upper', 'UPPERCASE'],
];

const POOLS = {
  font: FONTS.map(([slug]) => slug),
  orientation: ['mostly-horizontal', 'horizontal', 'any-which-way'],
  placement: PLACEMENTS.map(([slug]) => slug),
  palette: PALETTES.map(([slug]) => slug).filter((s) => s !== ''),
};

const pick = (list) => list[Math.floor(Math.random() * list.length)];
const randomSeed = () => 1 + Math.floor(Math.random() * 9998);

// ---------------------------------------------------------------------------
// The cloud spec. Each style dimension carries a resolved value and a
// mode; the seed and text are always concrete. Booted from the URL:
// plain value = fixed, ~value = random-mode snapshot, absent = random
// (rolled now) in UI mode, the engine's stock defaults under ?no-ui so
// the golden harness stays deterministic.

const LEGACY_DEFAULTS = {
  font: 'sexsmith',
  orientation: 'mostly-horizontal',
  placement: 'center-line',
  palette: '',
};
// '' can't ride in a URL where "absent" means random, so App Colors gets
// a token.
const paletteToToken = (v) => (v === '' ? 'none' : v);
const tokenToPalette = (t) => (t === 'none' ? '' : t);

// The first cloud is curated, not rolled: the placement that best fills
// the canvas (wide window → center-line, tall → its transpose,
// near-square → square), the most readable angle mix, and Blue Meets
// Orange. The dims still boot in random mode — Generate rolls truly
// random from then on — and font stays a genuine roll. (The canvas is
// already laid out here: the sidebar was shown above, so this aspect is
// the one the cloud will actually get.)
const bootAspect = canvas.clientHeight > 0
    ? canvas.clientWidth / canvas.clientHeight
    : 1.6;
const FIRST_CLOUD = {
  placement: bootAspect > 1.2 ? 'center-line'
           : bootAspect < 0.8 ? 'vertical-center-line'
           : 'square',
  orientation: 'mostly-horizontal',
  palette: 'blue-meets-orange',
};

function bootDimension(dim) {
  const raw = params.get(dim);
  const decode = dim === 'palette' ? tokenToPalette : (x) => x;
  if (raw === null) {
    return showUi
        ? { mode: 'random', value: FIRST_CLOUD[dim] || pick(POOLS[dim]) }
        : { mode: 'fixed', value: LEGACY_DEFAULTS[dim] };
  }
  if (raw.startsWith('~')) {
    return { mode: 'random', value: decode(raw.slice(1)) };
  }
  return { mode: 'fixed', value: decode(raw) };
}

// Word-count bounds: measured in-browser (tools/profile-rebuild.mjs,
// 2026-07-20) — a 2000-word rebuild is ~0.5s warm / ~1.2s cold on a
// fast laptop, x1.3-1.5 for the heaviest outlines (Fridge), several
// times that on slow hardware. The far end is a deliberate drag with
// a progress bar; past it the wait stops feeling interactive anywhere.
const MIN_WORDS = 50;
const MAX_WORDS = 2000;
const DEFAULT_WORDS = 800;
const clampWords = (n) =>
    Math.min(MAX_WORDS, Math.max(MIN_WORDS, n | 0)) || DEFAULT_WORDS;

// Looseness: the user's multiplier on the search spiral's per-typeface
// base step (the engine calibrates dense faces like Fridge coarser on
// its own; this scales on top). 1 = as calibrated.
const clampLoose = (v) =>
    Number.isFinite(v) ? Math.min(20, Math.max(1, v)) : 1;

const spec = {
  seed: parseInt(params.get('seed'), 10) || 1447,
  maxWords: clampWords(parseInt(params.get('max'), 10) || DEFAULT_WORDS),
  loose: clampLoose(parseFloat(params.get('loose'))),
  // 'little' is the engine default, so a bare URL looks unchanged.
  variance: VARIANCES.some(([slug]) => slug === params.get('variance'))
      ? params.get('variance') : 'little',
  // 'guess' is the engine default. Always fixed-mode: case never rolls.
  caseFold: CASE_FOLDS.some(([slug]) => slug === params.get('case'))
      ? params.get('case') : 'guess',
  caseFoldMode: 'fixed',
  // Removed words ("nuisance words", via the right-click menu), folded
  // keys. IMMUTABLE: undo snapshots share the array, so actions must
  // replace it, never push into it.
  exclude: (params.get('exclude') || '').split(',').filter(Boolean),
  // Recolor: 0 = the engine's stock color assignment; nonzero redraws
  // the palette distribution from this seed, layout untouched.
  colorSeed: parseInt(params.get('recolor'), 10) || 0,
  text: '',
};
for (const dim of ['font', 'orientation', 'placement', 'palette']) {
  const { mode, value } = bootDimension(dim);
  spec[dim] = value;
  spec[dim + 'Mode'] = mode;
}

// forEngine drops the ~ markers: the engine wants plain, parseable
// values, while the feedback repro URL records mode as well.
function specUrl(forEngine) {
  const url = new URL(location);
  url.searchParams.set('seed', spec.seed);
  url.searchParams.set('max', spec.maxWords);
  url.searchParams.set('variance', spec.variance);
  if (spec.caseFold !== 'guess') url.searchParams.set('case', spec.caseFold);
  else url.searchParams.delete('case');
  if (spec.exclude.length) {
    url.searchParams.set('exclude', spec.exclude.join(','));
  } else {
    url.searchParams.delete('exclude');
  }
  if (spec.colorSeed) url.searchParams.set('recolor', spec.colorSeed);
  else url.searchParams.delete('recolor');
  if (spec.loose !== 1) url.searchParams.set('loose', spec.loose);
  else url.searchParams.delete('loose');
  if (spec.corpus) url.searchParams.set('corpus', spec.corpus);
  else url.searchParams.delete('corpus');
  for (const dim of ['font', 'orientation', 'placement', 'palette']) {
    const encode = dim === 'palette' ? paletteToToken : (x) => x;
    const tilde =
        !forEngine && spec[dim + 'Mode'] === 'random' ? '~' : '';
    url.searchParams.set(dim, tilde + encode(spec[dim]));
  }
  return url;
}

// ---------------------------------------------------------------------------
// Worker boot. The engine reads URL parameters, but in UI mode it gets
// the *resolved* boot spec (random dims rolled, ~ markers dropped) via
// the init message rather than the real query string; the address bar is
// left exactly as the user arrived at it.

// moby-dick is the default corpus; ?text= (or an explicit ?corpus=)
// overrides it. The corpus is part of the spec — "Use a Book" swaps it
// at runtime (the worker stages the TSV on demand).
const corpus =
    params.get('corpus') || (params.has('text') ? '' : 'moby-dick');
spec.corpus = corpus;
const lazyFiles = [];
if (corpus) {
  lazyFiles.push({
    url: 'corpus/' + encodeURIComponent(corpus) + '.tsv',
    path: '/corpus.tsv',
  });
}
if (spec.font !== 'sexsmith') {  // sexsmith is preloaded
  // Staged at the same canonical path rebuilds use, so the engine's
  // path-keyed shape memo survives from boot into the first Generate
  // (staging at a boot-only path made that first rebuild re-shape the
  // whole vocabulary).
  lazyFiles.push({
    url: 'fonts/' + encodeURIComponent(spec.font) + '.ttf',
    path: '/fonts/' + spec.font + '.ttf',
  });
}

const dpr = window.devicePixelRatio || 1;
canvas.width = Math.round(canvas.clientWidth * dpr);
canvas.height = Math.round(canvas.clientHeight * dpr);
const offscreen = canvas.transferControlToOffscreen();

// The build identifier, stamped into dist/build-info.js by the build;
// announced on the console (handy in bug reports), shown in the
// Feedback dialog, and handed to the engine for export metadata.
const BUILD = globalThis.__wordsBuild || { id: 'dev', date: '' };
console.info(`words build ${BUILD.id} (${BUILD.date})`);

const worker = new Worker('worker.js');
worker.postMessage(
    {
      type: 'init',
      canvas: offscreen,
      search: showUi ? specUrl(true).search : location.search,
      lazyFiles,
      corpus,
      build: BUILD.id,
    },
    [offscreen]);

// One command in flight at a time; the newest queued spec wins. The text
// payload (possibly megabytes) rides along only when it changed; the
// worker keeps the staged copy otherwise.
let busy = true;  // the initial build is in progress
let pendingSpec = null;
let stagedText = null;  // what the worker currently has
const specMsg = (s) => ({
  type: 'rebuild',
  seed: s.seed,
  orientation: s.orientation,
  placement: s.placement,
  palette: s.palette,
  font: s.font,
  maxWords: s.maxWords,
  variance: s.variance,
  caseFold: s.caseFold,
  exclude: s.exclude.join(','),
  colorSeed: s.colorSeed,
  loose: s.loose,
  corpus: s.corpus,
  useText: s.text !== '',
});
// JSON of the rebuild the engine last performed. Primed with the boot
// spec: in UI mode the worker's init message carries exactly these
// resolved values, so the engine's first cloud IS this spec — without
// the priming, the first color-only change (a variance nudge, a palette
// pick) couldn't prove itself recolorable and paid a full rebuild.
let lastSentMsg = showUi ? JSON.stringify(specMsg(spec)) : null;
const sendSpec = (s) => {
  const msg = specMsg(s);
  if (s.text !== '' && s.text !== stagedText) {
    msg.text = s.text;
    stagedText = s.text;
  }
  // The engine is a deterministic function of the spec, so a rebuild
  // identical to the last one is a no-op: skip it (also keeping the
  // camera, which a real rebuild resets). This is what makes "Use
  // Palette" free after the palette editor has already previewed the
  // same palette on the cloud.
  const key = JSON.stringify(msg);
  if (key === lastSentMsg) return;
  // A change confined to the color fields (palette, variance, recolor
  // seed) takes the engine's recolor fast path: colors are reassigned
  // on the laid-out scene — no shaping, no layout, camera kept. The
  // engine itself falls back to a full rebuild when the change would
  // alter layout (crossing between App Colors and a palette shifts the
  // shared RNG stream). `text` is excluded from the comparison: it only
  // rides along when it changed, and then the send is not recolorable
  // anyway.
  const nonColor = (m) => {
    const { text, palette, variance, colorSeed, ...rest } = m;
    return JSON.stringify(rest);
  };
  const prev = lastSentMsg === null ? null : JSON.parse(lastSentMsg);
  // Crossing between App Colors ('') and a palette is layout-changing
  // (see above), and the engine's fallback rebuild resets its camera —
  // so the page must treat it as a full rebuild to keep the camera
  // states agreeing.
  const recolorable = prev !== null && !('text' in msg) &&
      (msg.palette === '') === (prev.palette === '') &&
      nonColor(msg) === nonColor(prev);
  lastSentMsg = key;
  busy = true;
  window.__wordsIdle = false;
  if (recolorable) {
    worker.postMessage({
      type: 'recolor',
      palette: msg.palette,
      variance: msg.variance,
      colorSeed: msg.colorSeed,
    });
    return;
  }
  // A local font's bytes ride along on first use only (attached after
  // the dedupe key, which must stay stable across staged and later
  // sends); the worker keeps them in MEMFS from then on.
  if (isLocalFont(s.font) && !stagedLocalFonts.has(localFontId(s.font))) {
    const entry = localFonts.get(localFontId(s.font));
    if (entry) {
      msg.fontId = localFontId(s.font);
      msg.fontData = entry.bytes;
      stagedLocalFonts.add(msg.fontId);
    }
  }
  resetCamera();  // the engine resets its copy on rebuild
  worker.postMessage(msg);
};

// Request/reply channel for export payloads (SVG text, PDF bytes, scene
// dimensions) computed by the engine.
let replySeq = 0;
const replyWaiters = new Map();
function askWorker(msg) {
  return new Promise((resolve) => {
    const id = ++replySeq;
    replyWaiters.set(id, resolve);
    worker.postMessage({ ...msg, id });
  });
}

worker.onmessage = (e) => {
  const m = e.data;
  if (m.type === 'reply') {
    const waiter = replyWaiters.get(m.id);
    if (waiter) {
      replyWaiters.delete(m.id);
      waiter(m.payload);
    }
  } else if (m.type === 'timing') {
    // Per-stage rebuild timings (see logStageTimings in src/main.cc):
    // console + a global for scripted profiling.
    console.log(m.text);
    window.__wordsTiming = m.text;
  } else if (m.type === 'print') {
    console.log(m.text);
    status.textContent = m.text;
  } else if (m.type === 'printErr') {
    console.error(m.text);
  } else if (m.type === 'progress') {
    // Shaping is roughly the first third of the work; layout the rest.
    const frac = m.phase === 'shaping' ? 0.3 * (m.done / m.total)
                                       : 0.3 + 0.7 * (m.done / m.total);
    progress.style.opacity = 1;
    progress.style.width = (100 * frac).toFixed(1) + '%';
  } else if (m.type === 'idle') {
    busy = false;
    // The e2e harness (tools/e2e-shot.mjs) waits on this before taking
    // its screenshot. Set it directly: deferring via rAF starves in
    // headless (no frames, no rAF), and the capture itself forces a
    // composite of the worker's committed frame — the driver adds a
    // small grace period for the commit to land.
    window.__wordsIdle = true;
    progress.style.opacity = 0;
    progress.style.width = '0';
    // Fresh scene, fresh camera math (see the camera section below).
    askWorker({ type: 'sceneSize' }).then((size) => {
      if (size.width > 0) cameraScene = size;
    });
    if (pendingSpec !== null) {
      const s = pendingSpec;
      pendingSpec = null;
      sendSpec(s);
    }
  }
};

// Resize events flood during a drag; coalesce to at most one message per
// animation frame (the rAF always reads the *current* size, so the final
// geometry is never missed) to keep the worker from re-rendering dozens
// of intermediate sizes — that churn reads as flicker.
let resizeQueued = false;
window.addEventListener('resize', () => {
  if (typeof closeMenus === 'function') closeMenus();  // positions go stale
  if (resizeQueued) return;
  resizeQueued = true;
  requestAnimationFrame(() => {
    resizeQueued = false;
    worker.postMessage({
      type: 'resize',
      width: Math.round(canvas.clientWidth * dpr),
      height: Math.round(canvas.clientHeight * dpr),
    });
  });
});

// ---------------------------------------------------------------------------
// View camera: wheel zooms around the cursor, drag pans, double-click
// resets. The engine treats the camera as pure render state (and resets
// it on rebuild); this side owns the interaction math, mirroring the
// camera and the scene dimensions. Renders are one cached-buffer draw,
// cheap enough for wheel-rate updates (rAF-coalesced anyway).

const camera = { zoom: 1, cx: 0, cy: 0 };
let cameraScene = null;  // {width, height}, refreshed at idle
let cameraQueued = false;
const pushCamera = () => {
  if (cameraQueued) return;
  cameraQueued = true;
  requestAnimationFrame(() => {
    cameraQueued = false;
    worker.postMessage(
        { type: 'camera', zoom: camera.zoom, cx: camera.cx, cy: camera.cy });
  });
};
const resetCamera = () => {
  Object.assign(camera, { zoom: 1, cx: 0, cy: 0 });
  detenteHold = 0;
  canvas.style.cursor = '';
};

// CSS pixels per scene unit at the current zoom (the fit letterboxes).
const cameraScale = () => {
  const rect = canvas.getBoundingClientRect();
  const fit = Math.min(rect.width / cameraScene.width,
                       rect.height / cameraScene.height);
  return { rect, scale: fit * camera.zoom };
};

// Never let the view leave the scene: pan bounds shrink to zero at fit.
const clampCamera = () => {
  const { rect, scale } = cameraScale();
  const boundX = Math.max(0, (cameraScene.width - rect.width / scale) / 2);
  const boundY = Math.max(0, (cameraScene.height - rect.height / scale) / 2);
  camera.cx = Math.min(boundX, Math.max(-boundX, camera.cx));
  camera.cy = Math.min(boundY, Math.max(-boundY, camera.cy));
};

const updateCursor = () => {
  canvas.style.cursor = camera.zoom > 1 ? 'grab' : '';
};

// Zoom bounds, with a detente at the fit view: crossing 1 latches there
// until a little more gesture accumulates (log-zoom units), so the
// natural resting point is easy to hit and takes intent to pass. The
// latch only arms on a crossing — zooming away from rest is immediate.
const MIN_ZOOM = 0.5;
const MAX_ZOOM = 40;
const DETENTE = 0.25;
let detenteHold = 0;
const applyZoomFactor = (factor) => {
  if (camera.zoom === 1 && detenteHold > 0) {
    detenteHold -= Math.abs(Math.log(factor));
    if (detenteHold > 0) return;  // held at the detente
    detenteHold = 0;
  }
  const before = camera.zoom;
  let z = Math.min(MAX_ZOOM, Math.max(MIN_ZOOM, before * factor));
  if ((before - 1) * (z - 1) < 0) {
    z = 1;
    detenteHold = DETENTE;
  }
  camera.zoom = z;
};

canvas.addEventListener('wheel', (e) => {
  if (!cameraScene) return;
  e.preventDefault();
  const { rect, scale } = cameraScale();
  // The scene point under the cursor stays put through the zoom change.
  const px = e.clientX - rect.left - rect.width / 2;
  const py = e.clientY - rect.top - rect.height / 2;
  const sx = camera.cx + px / scale;
  const sy = camera.cy - py / scale;  // scene y is up
  applyZoomFactor(Math.exp(-e.deltaY * 0.002));
  const after = cameraScale().scale;
  camera.cx = sx - px / after;
  camera.cy = sy + py / after;
  clampCamera();
  updateCursor();
  pushCamera();
}, { passive: false });

// Pointers on the canvas: one (mouse or finger) drags to pan; two
// pinch-zoom around their moving midpoint. The map holds live pointer
// positions; pinch geometry recomputes from it on every move.
const pointers = new Map();
let cameraDrag = null;
canvas.addEventListener('pointerdown', (e) => {
  if (!cameraScene) return;
  try {
    canvas.setPointerCapture(e.pointerId);
  } catch (err) { /* synthetic events have no active pointer */ }
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  if (pointers.size === 2) {
    cameraDrag = null;  // the pinch owns both pointers now
  } else if (camera.zoom > 1) {
    cameraDrag = { x: e.clientX, y: e.clientY };
    canvas.style.cursor = 'grabbing';
  }
});
canvas.addEventListener('pointermove', (e) => {
  if (!pointers.has(e.pointerId)) return;
  if (pointers.size === 2) {
    const [a, b] = [...pointers.values()];
    const oldDist = Math.hypot(a.x - b.x, a.y - b.y);
    const oldMid = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    const [a2, b2] = [...pointers.values()];
    const newDist = Math.hypot(a2.x - b2.x, a2.y - b2.y);
    const newMid = { x: (a2.x + b2.x) / 2, y: (a2.y + b2.y) / 2 };
    // The scene point under the old midpoint follows the new midpoint
    // through the zoom change — pinch zooms and pans in one gesture.
    const { rect, scale } = cameraScale();
    const sx = camera.cx + (oldMid.x - rect.left - rect.width / 2) / scale;
    const sy = camera.cy - (oldMid.y - rect.top - rect.height / 2) / scale;
    applyZoomFactor(oldDist > 0 ? newDist / oldDist : 1);
    const after = cameraScale().scale;
    camera.cx = sx - (newMid.x - rect.left - rect.width / 2) / after;
    camera.cy = sy + (newMid.y - rect.top - rect.height / 2) / after;
    clampCamera();
    updateCursor();
    pushCamera();
    return;
  }
  pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
  if (!cameraDrag) return;
  const { scale } = cameraScale();
  camera.cx -= (e.clientX - cameraDrag.x) / scale;
  camera.cy += (e.clientY - cameraDrag.y) / scale;
  cameraDrag = { x: e.clientX, y: e.clientY };
  clampCamera();
  pushCamera();
});
const endCameraPointer = (e) => {
  pointers.delete(e.pointerId);
  if (pointers.size === 1 && camera.zoom > 1) {
    // A pinch releasing one finger hands off to a pan with the other.
    const p = [...pointers.values()][0];
    cameraDrag = { x: p.x, y: p.y };
  } else {
    cameraDrag = null;
    updateCursor();
  }
};
canvas.addEventListener('pointerup', endCameraPointer);
canvas.addEventListener('pointercancel', endCameraPointer);
// iOS Safari runs its own page pinch-zoom through proprietary gesture
// events and doesn't reliably honor touch-action for it; a page stuck
// half-zoomed looks exactly like a broken camera. Claim gestures that
// start on the canvas.
for (const type of ['gesturestart', 'gesturechange', 'gestureend']) {
  canvas.addEventListener(type, (e) => e.preventDefault());
}
// Belt for WKWebView-wrapped browsers (iOS Chrome et al.) whose own
// gesture recognizers can swallow the pinch before pointer events
// settle: refusing multi-touch touchmove keeps the gesture ours.
canvas.addEventListener('touchmove', (e) => {
  if (e.touches.length > 1) e.preventDefault();
}, { passive: false });
canvas.addEventListener('dblclick', () => {
  resetCamera();
  pushCamera();
});

// ---------------------------------------------------------------------------
// Export. Every format derives from the engine's vector output: SVG is
// served verbatim, PNG rasterizes the SVG in-page at the chosen size,
// and PDF comes from the engine with the same outlines as native path
// operators (vector, not raster).

async function rasterize(svgText, width, height, transparent) {
  // Give the SVG explicit dimensions so the browser rasterizes at
  // exactly the requested size. preserveAspectRatio="none": the target
  // is already aspect-matched to within rounding, and stretching that
  // final fraction of a pixel beats an antialiased letterbox sliver at
  // the edges.
  const sized = svgText.replace(
      '<svg ',
      `<svg width="${width}" height="${height}" preserveAspectRatio="none" `);
  const url = URL.createObjectURL(
      new Blob([sized], { type: 'image/svg+xml' }));
  try {
    const img = new Image();
    img.src = url;
    await img.decode();
    const surface = new OffscreenCanvas(width, height);
    const ctx = surface.getContext('2d');
    ctx.drawImage(img, 0, 0, width, height);
    return surface;
  } finally {
    URL.revokeObjectURL(url);
  }
}

// Canvas rasterization strips everything but pixels, so the PNG build
// stamp is spliced in afterwards: a tEXt chunk (PNG's metadata
// mechanism) inserted just before the closing IEND chunk.
const CRC_TABLE = (() => {
  const t = new Int32Array(256);
  for (let n = 0; n < 256; n++) {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    t[n] = c;
  }
  return t;
})();
function crc32(bytes) {
  let c = -1;
  for (const b of bytes) c = CRC_TABLE[(c ^ b) & 0xff] ^ (c >>> 8);
  return (c ^ -1) >>> 0;
}
async function pngWithText(blob, keyword, text) {
  const src = new Uint8Array(await blob.arrayBuffer());
  const body = new TextEncoder().encode(keyword + '\0' + text);
  const chunk = new Uint8Array(12 + body.length);
  const view = new DataView(chunk.buffer);
  view.setUint32(0, body.length);
  chunk.set([0x74, 0x45, 0x58, 0x74], 4);  // 'tEXt'
  chunk.set(body, 8);
  view.setUint32(8 + body.length, crc32(chunk.subarray(4, 8 + body.length)));
  const iend = src.length - 12;  // IEND is always the trailing 12 bytes
  const out = new Uint8Array(src.length + chunk.length);
  out.set(src.subarray(0, iend), 0);
  out.set(chunk, iend);
  out.set(src.subarray(iend), iend + chunk.length);
  return new Blob([out], { type: 'image/png' });
}

async function buildExport(mode, opts) {
  const name = `words-${spec.seed}`;
  if (mode === 'svg') {
    const svg = await askWorker(
        { type: 'exportSvg', background: !opts.transparent });
    return {
      blob: new Blob([svg], { type: 'image/svg+xml' }),
      filename: `${name}.svg`,
    };
  }
  if (mode === 'png') {
    const svg = await askWorker(
        { type: 'exportSvg', background: !opts.transparent });
    const surface =
        await rasterize(svg, opts.width, opts.height, opts.transparent);
    const png = await surface.convertToBlob({ type: 'image/png' });
    return {
      blob: await pngWithText(png, 'Software', `words build ${BUILD.id}`),
      filename: `${name}.png`,
    };
  }
  // pdf
  const bytes = await askWorker(
      { type: 'exportPdf', pointWidth: opts.inches * 72 });
  return {
    blob: new Blob([bytes], { type: 'application/pdf' }),
    filename: `${name}.pdf`,
  };
}

function download(blob, filename) {
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = filename;
  a.click();
  setTimeout(() => URL.revokeObjectURL(url), 10000);
}

// The engine's console handle, a shim over the worker:
// words._wordsLogScene() still dumps engine state to the console;
// buildExport is exposed for the harness and console experiments.
window.words = {
  _wordsLogScene: () => worker.postMessage({ type: 'logScene' }),
  buildExport,
  build: BUILD,
  camera,  // live view state, for console interrogation
};

// ---------------------------------------------------------------------------
// Undo machinery. Every mutation flows through apply() as an action
// holding before/after spec snapshots — an object that knows how to undo
// (and redo) itself — so any change to the cloud is reversible. The
// engine is a deterministic function of the spec.

const undoStack = [];
const redoStack = [];

// previewSpec rebuilds from a spec that isn't (yet) committed — the
// palette editor's live preview; submitSpec is the committed case. Both
// ride the same one-in-flight queue, newest wins.
const previewSpec = (s) => {
  busy ? pendingSpec = s : sendSpec(s);
};
const submitSpec = () => previewSpec({ ...spec });
const apply = (action) => {
  undoStack.push(action);
  redoStack.length = 0;
  Object.assign(spec, action.after);
  submitSpec();
  refreshUi();
};
const undo = () => {
  const action = undoStack.pop();
  if (!action) return;
  redoStack.push(action);
  Object.assign(spec, action.before);
  submitSpec();
  refreshUi();
};
const redo = () => {
  const action = redoStack.pop();
  if (!action) return;
  undoStack.push(action);
  Object.assign(spec, action.after);
  submitSpec();
  refreshUi();
};

// ---------------------------------------------------------------------------
// Toolbar.

const generateBtn = document.getElementById('generate');
const undoBtn = document.getElementById('undo');
const redoBtn = document.getElementById('redo');
const dialog = document.getElementById('words-dialog');

// ----- dropdown menus: the locks.

const MENUS = {
  font: { title: 'Font', options: FONTS },
  layout: { title: 'Layout', dim: 'placement', options: PLACEMENTS },
  // 'Angle', not 'Orientation': the shorter word keeps the sidebar's
  // disclosure triangles aligned. (The engine dimension and URL
  // parameter remain 'orientation'.)
  orientation: { title: 'Angle', options: ORIENTATIONS },
  palette: { title: 'Palette', options: PALETTES },
  case: { title: 'Case', dim: 'caseFold', options: CASE_FOLDS,
          noRandom: true },
};

// Tiny inline-SVG thumbnails: word-bars at representative angles for the
// orientation strategies, dot scatters for the placements.
const BAR_THUMBS = {
  'horizontal':
      [[4, 8, 22, 0], [30, 12, 18, 0], [8, 22, 26, 0], [38, 24, 14, 0]],
  'mostly-horizontal':
      [[4, 8, 22, 0], [30, 10, 16, 0], [10, 22, 24, 0], [46, 18, 14, 90]],
  'long-horizontal-likely':
      [[4, 10, 34, 0], [46, 16, 12, 90], [8, 24, 18, 0], [30, 26, 14, 0]],
  'half-and-half':
      [[4, 8, 20, 0], [32, 16, 16, 90], [8, 22, 20, 0], [46, 20, 16, 90]],
  'mostly-vertical':
      [[10, 16, 18, 90], [22, 12, 20, 0], [36, 18, 18, 90], [48, 16, 16, 90]],
  'vertical':
      [[10, 16, 18, 90], [22, 16, 22, 90], [36, 16, 18, 90], [48, 16, 20, 90]],
  'any-which-way':
      [[6, 12, 22, 25], [28, 10, 16, -40], [12, 26, 20, 70], [38, 24, 18, -15]],
};

function barThumb(slug) {
  const rects = (BAR_THUMBS[slug] || []).map(([x, y, w, a]) => {
    const cx = x + w / 2;
    const cy = y + 2.5;
    return `<rect x="${x}" y="${y}" width="${w}" height="5" rx="2.5"` +
           ` transform="rotate(${a} ${cx} ${cy})" fill="currentColor"/>`;
  }).join('');
  return `<svg class="thumb" viewBox="0 0 60 36" aria-hidden="true">${rects}</svg>`;
}

// Word-bar silhouettes of the two cloud shapes: center-line packs a wide
// lens hugging the horizontal midline; center packs a round blob.
const PLACEMENT_THUMBS = {
  'center-line': [
    [22, 2, 10],
    [10, 9, 12], [26, 9, 16], [46, 9, 8],
    [2, 16, 13], [18, 16, 17], [38, 16, 13], [53, 16, 6],
    [9, 23, 13], [26, 23, 14], [44, 23, 9],
    [24, 30, 11],
  ],
  // The transpose: a tall lens hugging the vertical midline.
  'vertical-center-line': [
    [26, 1, 9],
    [22, 6, 15],
    [18, 11, 24],
    [16, 16, 28],
    [19, 21, 22],
    [23, 26, 14],
    [27, 31, 8],
  ],
  'center': [
    [24, 2, 13],
    [16, 9, 13], [32, 9, 13],
    [12, 16, 17], [31, 16, 18],
    [16, 23, 12], [31, 23, 14],
    [23, 30, 13],
  ],
  // The blocky variant: even rows filling a square block edge to edge.
  'square': [
    [13, 2, 16], [31, 2, 16],
    [13, 9, 14], [29, 9, 18],
    [13, 16, 17], [32, 16, 15],
    [13, 23, 15], [30, 23, 17],
    [13, 30, 16], [31, 30, 16],
  ],
  // The A→Z lens: same wide band as center-line, with the reading order
  // spelled out beneath.
  'alphabetical': [
    [14, 4, 12], [32, 4, 14],
    [4, 11, 15], [22, 11, 18], [44, 11, 11],
    [9, 18, 14], [27, 18, 13], [44, 18, 9],
    [17, 25, 12], [33, 25, 11],
  ],
};

function scatterThumb(slug) {
  const rects = (PLACEMENT_THUMBS[slug] || []).map(([x, y, w]) =>
      `<rect x="${x}" y="${y}" width="${w}" height="5" rx="2.5"` +
      ` fill="currentColor"/>`).join('');
  const letters = slug === 'alphabetical'
      ? `<text x="3" y="36" font-size="9" fill="currentColor"` +
        ` font-family="ui-monospace,monospace">A</text>` +
        `<text x="28" y="36" font-size="9" fill="currentColor"` +
        ` font-family="ui-monospace,monospace">M</text>` +
        `<text x="52" y="36" font-size="9" fill="currentColor"` +
        ` font-family="ui-monospace,monospace">Z</text>`
      : '';
  return `<svg class="thumb" viewBox="0 0 60 36" aria-hidden="true">${rects}${letters}</svg>`;
}

function paletteThumb(bg, colors) {
  const dots = colors.map((c, i) =>
      `<circle cx="${10 + i * 9}" cy="10" r="3.5" fill="${c}"/>`).join('');
  const w = 20 + colors.length * 9 - 9;
  // A fixed-width slot centers the variable-width swatch, so the palette
  // names line up in a clean left-aligned column.
  return `<span class="sw-slot"><svg class="thumb sw" viewBox="0 0 ${w} 20"` +
         ` aria-hidden="true" style="width:${w * 1.6}px">` +
         `<rect x="0.5" y="0.5" width="${w - 1}" height="19" rx="4"` +
         ` fill="${bg}" stroke="#555"/>${dots}</svg></span>`;
}

// Menu fonts load lazily the first time they're needed; each font option
// renders its family name in the face itself, straight from the .ttf the
// site already serves.
let menuFontsRequested = false;
function ensureMenuFonts() {
  if (menuFontsRequested) return;
  menuFontsRequested = true;
  for (const [slug] of FONTS) {
    const face = new FontFace(
        'menu-' + slug, `url(fonts/${encodeURIComponent(slug)}.ttf)`);
    face.load().then((f) => document.fonts.add(f)).catch(() => {});
  }
}

// The hidden picker behind "Use My Font…". Adoption validates in the
// browser first: FontFace parses the whole file, so anything it rejects
// would only shape to an empty cloud engine-side. FreeType can't decode
// woff2, hence the .ttf/.otf restriction.
const localFontInput = document.createElement('input');
localFontInput.type = 'file';
localFontInput.accept = '.ttf,.otf';
localFontInput.hidden = true;
document.body.appendChild(localFontInput);
localFontInput.addEventListener('change', async () => {
  const file = localFontInput.files[0];
  localFontInput.value = '';  // re-picking the same file must re-fire
  if (!file) return;
  useLocalFont(file.name, await file.arrayBuffer());
});

// Adopt font bytes (from a picked file or an installed font's blob) as
// the fixed font. FontFace validation up front: anything it rejects
// would only shape to an empty cloud engine-side.
async function adoptLocalFont(name, bytes) {
  const id = String(++localFontSeq);
  const face = new FontFace(`menu-local-${id}`, bytes);
  try {
    await face.load();
  } catch (err) {
    console.error('local font rejected', err);
    status.textContent = `couldn't read ${name} as a font`;
    return false;
  }
  document.fonts.add(face);  // menu entries render in the face itself
  localFonts.set(id, { name, bytes });
  apply({
    label: 'Use My Font',
    before: { ...spec },
    after: { ...spec, font: 'local:' + id, fontMode: 'fixed' },
  });
  return true;
}

async function useLocalFont(fileName, bytes) {
  if (!/\.(ttf|otf)$/i.test(fileName)) {
    status.textContent = 'font files must be .ttf or .otf';
    return false;
  }
  return adoptLocalFont(fileName.replace(/\.(ttf|otf)$/i, ''), bytes);
}

// ----- Installed fonts (Chromium's Local Font Access API; the menu
// entry only exists where queryLocalFonts does). The first open asks
// permission and lists every installed family, searchable, each row
// shown in its own face — installed fonts are addressable from CSS by
// family name, so no bytes load until one is picked. Picking fetches
// that font's blob and adopts it through the same local-font path as a
// picked file. (For .ttc collections the engine shapes face 0, which
// may be a different style than the one listed.)
const sysFontDialog = document.getElementById('sysfont-dialog');
const sysFontSearch = document.getElementById('sysfont-search');
const sysFontList = document.getElementById('sysfont-list');
let sysFontFamilies = null;  // [{family, data}], sorted; null until asked

async function openSystemFonts() {
  if (sysFontFamilies === null) {
    try {
      const fonts = await window.queryLocalFonts();
      const byFamily = new Map();
      for (const f of fonts) {
        // One entry per family; prefer the Regular style's file.
        if (!byFamily.has(f.family) || /^regular$/i.test(f.style)) {
          byFamily.set(f.family, f);
        }
      }
      sysFontFamilies = [...byFamily.entries()]
          .map(([family, data]) => ({ family, data }))
          .sort((a, b) => a.family.localeCompare(b.family));
    } catch (err) {
      console.error('installed fonts unavailable', err);
      status.textContent = 'access to installed fonts was denied';
      return;
    }
  }
  renderSysFontList();
  sysFontDialog.showModal();
  sysFontSearch.select();
}

function renderSysFontList() {
  const q = sysFontSearch.value.trim().toLowerCase();
  sysFontList.replaceChildren(
      ...sysFontFamilies
          .filter(({ family }) => !q || family.toLowerCase().includes(q))
          .map(({ family, data }) => {
            const row = document.createElement('button');
            row.className = 'book-row';
            const label = document.createElement('span');
            label.className = 'book-title';
            label.textContent = family;
            label.style.fontFamily = `'${family.replace(/'/g, "\\'")}'`;
            label.style.fontSize = '17px';
            row.appendChild(label);
            row.addEventListener('click', async () => {
              sysFontDialog.close();
              try {
                const bytes = await (await data.blob()).arrayBuffer();
                adoptLocalFont(family, bytes);
              } catch (err) {
                console.error('installed font read failed', err);
                status.textContent = `couldn't read ${family}`;
              }
            });
            return row;
          }));
}
sysFontSearch.addEventListener('input', renderSysFontList);
document.getElementById('sysfont-cancel').addEventListener('click', () => {
  sysFontDialog.close();
});

function refreshLocalFontMenu(panel) {
  const box = panel.querySelector('.dd-saved');
  box.replaceChildren(...[...localFonts].map(([id, { name }]) => {
    const opt = document.createElement('button');
    opt.className = 'dd-opt';
    opt.dataset.slug = 'local:' + id;
    opt.innerHTML = `<span class="opt-label" style="font-family:` +
        `'menu-local-${id}',ui-monospace,monospace;font-size:17px"></span>`;
    opt.querySelector('.opt-label').textContent = name;  // user text
    opt.addEventListener('click', () => choose('font', 'fixed', 'local:' + id));
    return opt;
  }));
}

function optionContent(menuName, slug, label, extra) {
  if (menuName === 'font') {
    return `<span class="opt-label" style="font-family:'menu-${slug}',` +
           `ui-monospace,monospace;font-size:17px">${label}</span>`;
  }
  if (menuName === 'orientation') {
    return barThumb(slug) + `<span class="opt-label">${label}</span>`;
  }
  if (menuName === 'layout') {
    return scatterThumb(slug) + `<span class="opt-label">${label}</span>`;
  }
  if (menuName === 'palette') {
    return paletteThumb(extra[0], extra[1]) +
           `<span class="opt-label">${label}</span>`;
  }
  return `<span class="opt-label">${label}</span>`;  // case: the label is
                                                     // its own thumbnail
}

function menuLabel(menuName) {
  const menu = MENUS[menuName];
  const dim = menu.dim || menuName;
  if (spec[dim + 'Mode'] === 'random') return `🎲 Random ${menu.title}`;
  const entry = menu.options.find(([slug]) => slug === spec[dim]);
  if (entry) return entry[1];
  if (menuName === 'palette' && isCustomPalette(spec.palette)) {
    const saved = savedPalettes().find((p) => p.value === spec.palette);
    return saved ? saved.name : 'Custom Palette';
  }
  if (menuName === 'font' && isLocalFont(spec.font)) {
    const local = localFonts.get(localFontId(spec.font));
    return local ? local.name : 'My Font';
  }
  return spec[dim];
}

function choose(menuName, mode, value) {
  closeMenus();
  const menu = MENUS[menuName];
  const dim = menu.dim || menuName;
  if (spec[dim + 'Mode'] === mode && spec[dim] === value) return;
  apply({
    label: mode === 'random' ? `Random ${menu.title}` : `Set ${menu.title}`,
    before: { ...spec },
    after: { ...spec, [dim]: value, [dim + 'Mode']: mode },
  });
}

function closeMenus() {
  for (const panel of document.querySelectorAll('.dd-panel')) {
    panel.hidden = true;
  }
}

// Desktop: fly out to the right of the sidebar, vertically centered on
// the button but clamped to the viewport. Phone-portrait: pop down
// below the top bar, spanning the screen. Call with the panel visible
// (offsetHeight must be real).
const phoneMode = () =>
    matchMedia('(max-width: 600px) and (orientation: portrait)').matches;
function positionMenuPanel(btn, panel) {
  const margin = 8;
  const bar = document.getElementById('panel').getBoundingClientRect();
  if (phoneMode()) {
    panel.style.left = `${margin}px`;
    panel.style.top = `${bar.bottom + margin}px`;
    panel.style.maxWidth = `calc(100vw - ${2 * margin}px)`;
    panel.style.maxHeight =
        `${window.innerHeight - bar.bottom - 2 * margin}px`;
    return;
  }
  panel.style.maxWidth = '';
  panel.style.maxHeight = '';
  const btnRect = btn.getBoundingClientRect();
  const height = panel.offsetHeight;
  const top = Math.min(
      Math.max(btnRect.top + btnRect.height / 2 - height / 2, margin),
      window.innerHeight - height - margin);
  panel.style.left = `${bar.right + margin}px`;
  panel.style.top = `${top}px`;
}

function buildMenu(menuName) {
  const menu = MENUS[menuName];
  const dim = menu.dim || menuName;
  const root = document.getElementById('dd-' + menuName);
  const btn = root.querySelector('.dd-btn');
  const panel = root.querySelector('.dd-panel');

  if (!menu.noRandom) {
    const randomOpt = document.createElement('button');
    randomOpt.className = 'dd-opt dd-random';
    randomOpt.innerHTML =
        `<span class="opt-label">🎲 Random ${menu.title}</span>`;
    randomOpt.addEventListener('click', () =>
        choose(menuName, 'random', pick(POOLS[dim])));
    panel.appendChild(randomOpt);
  }

  if (menuName === 'font') {
    // The user's own fonts sit up top with 🎲: installed fonts (where
    // the Local Font Access API exists), a font-file picker everywhere;
    // fonts adopted this session follow (refreshed each open), then the
    // built-ins.
    if ('queryLocalFonts' in window) {
      const sys = document.createElement('button');
      sys.className = 'dd-opt dd-action';
      sys.title = 'Cloud with a font installed on this computer';
      sys.innerHTML = '<span class="opt-label">🖥️ Installed Fonts…</span>';
      sys.addEventListener('click', () => {
        closeMenus();
        openSystemFonts();
      });
      panel.appendChild(sys);
    }
    const pickFont = document.createElement('button');
    pickFont.className = 'dd-opt dd-action';
    pickFont.title = 'Cloud with a .ttf or .otf file';
    pickFont.innerHTML = '<span class="opt-label">📁 Font File…</span>';
    pickFont.addEventListener('click', () => {
      closeMenus();
      localFontInput.click();
    });
    panel.appendChild(pickFont);
    const local = document.createElement('div');
    local.className = 'dd-saved';
    panel.appendChild(local);
  }

  for (const [slug, label, ...extra] of menu.options) {
    const opt = document.createElement('button');
    opt.className = 'dd-opt';
    opt.dataset.slug = slug;
    opt.innerHTML = optionContent(menuName, slug, label, extra);
    opt.addEventListener('click', () => choose(menuName, 'fixed', slug));
    panel.appendChild(opt);
  }

  if (menuName === 'palette') {
    // Saved custom palettes (refreshed each open — the editor may have
    // added or deleted some), then the editor itself.
    const saved = document.createElement('div');
    saved.className = 'dd-saved';
    panel.appendChild(saved);
    const edit = document.createElement('button');
    edit.className = 'dd-opt dd-edit';
    edit.innerHTML = '<span class="opt-label">✏️ Custom Palette…</span>';
    edit.addEventListener('click', () => {
      closeMenus();
      openPaletteEditor();
    });
    panel.appendChild(edit);
  }

  btn.addEventListener('click', (e) => {
    e.stopPropagation();
    const wasHidden = panel.hidden;
    closeMenus();
    if (wasHidden) {
      if (menuName === 'font') {
        ensureMenuFonts();
        refreshLocalFontMenu(panel);
      }
      if (menuName === 'palette') refreshSavedPaletteMenu(panel);
      for (const opt of panel.querySelectorAll('.dd-opt')) {
        opt.classList.toggle('selected',
            spec[dim + 'Mode'] === 'fixed' && opt.dataset.slug === spec[dim]);
      }
      const randomBtn = panel.querySelector('.dd-random');
      if (randomBtn) {
        randomBtn.classList.toggle('selected',
            spec[dim + 'Mode'] === 'random');
      }
      panel.hidden = false;
      positionMenuPanel(btn, panel);
    }
  });
}

function refreshUi() {
  undoBtn.disabled = undoStack.length === 0;
  redoBtn.disabled = redoStack.length === 0;
  // A book cloud is exactly reproducible from its URL; a user-text or
  // local-font cloud isn't, so the share button hides.
  document.getElementById('copy-link').hidden =
      spec.text !== '' || isLocalFont(spec.font);
  for (const s of document.querySelectorAll('.max-input')) {
    s.value = spec.maxWords;
  }
  for (const v of document.querySelectorAll('.max-val')) {
    v.textContent = spec.maxWords;
  }
  document.getElementById('words-btn-label').textContent =
      `${spec.maxWords} words`;
  for (const s of document.querySelectorAll('.loose-input')) {
    s.value = spec.loose;
  }
  for (const v of document.querySelectorAll('.loose-val')) {
    v.textContent = `×${spec.loose}`;
  }
  document.getElementById('loose-btn-label').textContent =
      `Looseness ×${spec.loose}`;
  const vIndex = VARIANCES.findIndex(([slug]) => slug === spec.variance);
  const [, vShort, vFull] = VARIANCES[vIndex < 0 ? 1 : vIndex];
  for (const s of document.querySelectorAll('.var-input')) {
    s.value = vIndex < 0 ? 1 : vIndex;
  }
  for (const v of document.querySelectorAll('.var-val')) {
    v.textContent = vShort;
  }
  document.getElementById('variance-btn-label').textContent = vFull;
  for (const menuName of Object.keys(MENUS)) {
    const cur = document.querySelector(`#dd-${menuName} .dd-cur`);
    cur.textContent = menuLabel(menuName);
    if (menuName === 'font') {
      if (spec.fontMode === 'fixed') {
        ensureMenuFonts();
        const family = isLocalFont(spec.font)
            ? `menu-local-${localFontId(spec.font)}` : `menu-${spec.font}`;
        cur.style.fontFamily = `'${family}',ui-monospace,monospace`;
      } else {
        cur.style.fontFamily = '';
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Custom palette editor. The in-progress palette lives here as
// {bg: '#rrggbb', colors: [...]}; every edit previews live on the real
// cloud (a rebuild through the one-in-flight queue — spec itself stays
// untouched, so Cancel just rebuilds from spec). "Use Palette" applies
// one undoable action carrying the serialized custom:… value — the same
// string that rides in spec.palette, copy links, and ?palette=.

const paletteDialog = document.getElementById('palette-dialog');
const palDragBar = document.getElementById('pal-drag');
const palBgRow = document.getElementById('pal-bg-row');
const palColorsRow = document.getElementById('pal-colors');
const palNameInput = document.getElementById('pal-name');
const palSaveBtn = document.getElementById('pal-save-btn');
const palLoadSelect = document.getElementById('pal-load');
const palDeleteBtn = document.getElementById('pal-delete');
const palUseBtn = document.getElementById('pal-use');
const cpBox = document.getElementById('cp');
const cpSv = document.getElementById('cp-sv');
const cpSvDot = document.getElementById('cp-sv-dot');
const cpHue = document.getElementById('cp-hue');
const cpHueDot = document.getElementById('cp-hue-dot');
const cpHex = document.getElementById('cp-hex');

let palEdit = { bg: '#000000', colors: [] };
let palSlot = null;  // 'bg' | word-color index | null (picker hidden)
// The selected slot's color as HSV (h in [0,360), s and v in [0,1]):
// the picker's working state, so hue survives a trip through gray.
let palHsv = { h: 0, s: 0, v: 0 };

function hexToHsv(hex) {
  const r = parseInt(hex.slice(1, 3), 16) / 255;
  const g = parseInt(hex.slice(3, 5), 16) / 255;
  const b = parseInt(hex.slice(5, 7), 16) / 255;
  const max = Math.max(r, g, b);
  const d = max - Math.min(r, g, b);
  let h = 0;
  if (d > 0) {
    if (max === r) h = 60 * (((g - b) / d) % 6);
    else if (max === g) h = 60 * ((b - r) / d + 2);
    else h = 60 * ((r - g) / d + 4);
    if (h < 0) h += 360;
  }
  return { h, s: max === 0 ? 0 : d / max, v: max };
}
function hsvToHex({ h, s, v }) {
  const channel = (n) => {
    const k = (n + h / 60) % 6;
    const c = v - v * s * Math.max(0, Math.min(k, 4 - k, 1));
    return Math.round(c * 255).toString(16).padStart(2, '0');
  };
  return '#' + channel(5) + channel(3) + channel(1);
}

// The live preview: rebuild the real cloud with the in-progress palette
// whenever it actually changes. Drags fire this at pointer-move rate;
// the one-in-flight queue coalesces them (newest wins), so the cloud
// tracks the picker as fast as rebuilds complete. An empty palette
// never previews (the engine would fall back to App Colors).
let palPreviewValue = null;  // last previewed serialization
let palPreviewShown = false;
let palCommitted = false;
function sendPalPreview() {
  if (!paletteDialog.open || palEdit.colors.length === 0) return;
  const value = serializeCustomPalette(palEdit);
  if (value === palPreviewValue) return;
  palPreviewValue = value;
  palPreviewShown = true;
  previewSpec({ ...spec, palette: value });
}

function renderPalSwatches() {
  const bgSwatch = document.createElement('button');
  bgSwatch.className = 'pal-swatch';
  bgSwatch.classList.toggle('selected', palSlot === 'bg');
  bgSwatch.style.background = palEdit.bg;
  bgSwatch.title = 'Background color';
  bgSwatch.addEventListener('click', () => selectPalSlot('bg'));
  palBgRow.replaceChildren(bgSwatch);

  const slots = palEdit.colors.map((color, i) => {
    const slot = document.createElement('div');
    slot.className = 'pal-slot';
    const sw = document.createElement('button');
    sw.className = 'pal-swatch';
    sw.classList.toggle('selected', palSlot === i);
    sw.style.background = color;
    sw.addEventListener('click', () => selectPalSlot(i));
    const del = document.createElement('button');
    del.className = 'pal-del';
    del.textContent = '−';
    del.title = 'Remove this color';
    del.addEventListener('click', () => {
      palEdit.colors.splice(i, 1);
      if (palSlot === i) hidePalPicker();
      else if (typeof palSlot === 'number' && palSlot > i) palSlot -= 1;
      renderPalEditor();
    });
    slot.append(sw, del);
    return slot;
  });
  const add = document.createElement('button');
  add.className = 'pal-swatch pal-add';
  add.textContent = '+';
  add.title = 'Add a color';
  add.addEventListener('click', () => {
    palEdit.colors.push('#ffffff');
    selectPalSlot(palEdit.colors.length - 1);
  });
  palColorsRow.replaceChildren(...slots, add);
}

function renderPalPicker() {
  cpSv.style.background =
      'linear-gradient(to top, #000, rgba(0,0,0,0)), ' +
      `linear-gradient(to right, #fff, hsl(${palHsv.h}, 100%, 50%))`;
  cpSvDot.style.left = `${palHsv.s * 100}%`;
  cpSvDot.style.top = `${(1 - palHsv.v) * 100}%`;
  cpSvDot.style.background = hsvToHex(palHsv);
  cpHueDot.style.top = `${(palHsv.h / 360) * 100}%`;
  // Don't rewrite the hex field under the user's cursor mid-typing.
  if (document.activeElement !== cpHex) cpHex.value = hsvToHex(palHsv);
}

function renderPalEditor() {
  renderPalSwatches();
  sendPalPreview();
  if (palSlot !== null) renderPalPicker();
  const empty = palEdit.colors.length === 0;
  palUseBtn.disabled = empty;
  palSaveBtn.disabled = empty;
}

function selectPalSlot(slot) {
  palSlot = slot;
  palHsv = hexToHsv(slot === 'bg' ? palEdit.bg : palEdit.colors[slot]);
  cpBox.hidden = false;
  renderPalEditor();
}
function hidePalPicker() {
  palSlot = null;
  cpBox.hidden = true;
}

// Editing the picker writes the selected swatch (and everything that
// shows it) live.
function applyPalPickerColor() {
  const hex = hsvToHex(palHsv);
  if (palSlot === 'bg') palEdit.bg = hex;
  else if (typeof palSlot === 'number') palEdit.colors[palSlot] = hex;
  renderPalEditor();
}

function palPickerDrag(el, onPoint) {
  const point = (e) => {
    const r = el.getBoundingClientRect();
    onPoint(Math.min(1, Math.max(0, (e.clientX - r.left) / r.width)),
            Math.min(1, Math.max(0, (e.clientY - r.top) / r.height)));
  };
  el.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    try {
      el.setPointerCapture(e.pointerId);
    } catch (err) { /* synthetic events have no active pointer */ }
    point(e);
  });
  el.addEventListener('pointermove', (e) => {
    if (e.buttons & 1) point(e);
  });
}
palPickerDrag(cpSv, (x, y) => {
  palHsv.s = x;
  palHsv.v = 1 - y;
  applyPalPickerColor();
});
palPickerDrag(cpHue, (x, y) => {
  palHsv.h = Math.min(359.9, y * 360);
  applyPalPickerColor();
});
cpHex.addEventListener('input', () => {
  const m = /^#?([0-9a-fA-F]{6})$/.exec(cpHex.value.trim());
  if (!m || palSlot === null) return;
  palHsv = hexToHsv('#' + m[1].toLowerCase());
  applyPalPickerColor();
});

function refreshSavedPaletteMenu(panel) {
  const box = panel.querySelector('.dd-saved');
  box.replaceChildren(...savedPalettes().map(({ name, value }) => {
    const { bg, colors } = parseCustomPalette(value);
    const opt = document.createElement('button');
    opt.className = 'dd-opt';
    opt.dataset.slug = value;
    opt.innerHTML = paletteThumb(bg, colors) + '<span class="opt-label"></span>';
    opt.querySelector('.opt-label').textContent = name;  // user text, not HTML
    opt.addEventListener('click', () => choose('palette', 'fixed', value));
    return opt;
  }));
}

function refreshPalLoadSelect(selectedName) {
  const list = savedPalettes();
  palLoadSelect.replaceChildren(
      new Option('Saved palettes…', ''),
      ...list.map((p) => new Option(p.name, p.name)));
  palLoadSelect.value =
      list.some((p) => p.name === selectedName) ? selectedName : '';
  palDeleteBtn.disabled = palLoadSelect.value === '';
}

palSaveBtn.addEventListener('click', () => {
  const name = palNameInput.value.trim() || 'My Palette';
  palNameInput.value = name;
  const list = savedPalettes();
  const value = serializeCustomPalette(palEdit);
  const existing = list.findIndex((p) => p.name === name);
  if (existing >= 0) list[existing] = { name, value };
  else list.push({ name, value });
  storeSavedPalettes(list);
  refreshPalLoadSelect(name);
  refreshUi();  // the sidebar's palette label may now be this name
});
palLoadSelect.addEventListener('change', () => {
  const entry = savedPalettes().find((p) => p.name === palLoadSelect.value);
  palDeleteBtn.disabled = !entry;
  if (!entry) return;
  palEdit = parseCustomPalette(entry.value);
  palNameInput.value = entry.name;
  hidePalPicker();
  renderPalEditor();
});
palDeleteBtn.addEventListener('click', () => {
  const name = palLoadSelect.value;
  if (!name) return;
  storeSavedPalettes(savedPalettes().filter((p) => p.name !== name));
  refreshPalLoadSelect('');
});

// Dragging a dialog's title bar moves it (clamped on screen); position
// (and any resize) survives reopening within the session. Returns a
// pin function for callers to invoke right after showModal(): it fixes
// the dialog at its current spot with explicit coordinates — re-clamped
// if the window shrank — so a later CSS resize anchors at the top-left
// instead of fighting the centering margins.
function makeDialogDraggable(dialog, bar) {
  const position = (left, top) => {
    const margin = 8;
    const maxLeft =
        Math.max(window.innerWidth - dialog.offsetWidth - margin, margin);
    const maxTop =
        Math.max(window.innerHeight - dialog.offsetHeight - margin, margin);
    dialog.style.margin = '0';
    dialog.style.inset = 'auto';
    dialog.style.left = `${Math.min(Math.max(left, margin), maxLeft)}px`;
    dialog.style.top = `${Math.min(Math.max(top, margin), maxTop)}px`;
  };
  let offset = null;  // pointer position within the dialog
  bar.addEventListener('pointerdown', (e) => {
    e.preventDefault();
    const r = dialog.getBoundingClientRect();
    offset = { x: e.clientX - r.left, y: e.clientY - r.top };
    try {
      bar.setPointerCapture(e.pointerId);
    } catch (err) { /* synthetic events have no active pointer */ }
  });
  bar.addEventListener('pointermove', (e) => {
    if (offset) position(e.clientX - offset.x, e.clientY - offset.y);
  });
  const end = () => { offset = null; };
  bar.addEventListener('pointerup', end);
  bar.addEventListener('pointercancel', end);
  return () => {
    const r = dialog.getBoundingClientRect();
    position(r.left, r.top);
  };
}
const pinPaletteDialog = makeDialogDraggable(paletteDialog, palDragBar);

// Opens editing the current palette when it's custom, else fresh: black
// background, no word colors yet — just the + tile.
function openPaletteEditor() {
  const current = parseCustomPalette(spec.palette);
  palEdit = current || { bg: '#000000', colors: [] };
  const saved =
      current && savedPalettes().find((p) => p.value === spec.palette);
  palNameInput.value = saved ? saved.name : '';
  refreshPalLoadSelect(saved ? saved.name : '');
  hidePalPicker();
  palCommitted = false;
  palPreviewShown = false;
  palPreviewValue = isCustomPalette(spec.palette) ? spec.palette : null;
  renderPalEditor();
  paletteDialog.showModal();
  pinPaletteDialog();
}

palUseBtn.addEventListener('click', () => {
  if (palEdit.colors.length === 0) return;
  palCommitted = true;
  paletteDialog.close();
  const value = serializeCustomPalette(palEdit);
  if (spec.paletteMode === 'fixed' && spec.palette === value) return;
  apply({
    label: 'Custom Palette',
    before: { ...spec },
    after: { ...spec, palette: value, paletteMode: 'fixed' },
  });
});
// Any close that isn't a commit (Cancel, Escape) rebuilds the cloud
// back from the untouched spec — but only if a preview ever showed.
// The Cancel button restores directly rather than waiting for the
// dialog's close event (a queued user-interaction task, which e.g.
// hidden tabs delay); the flags make the event's follow-up a no-op.
const palRestoreIfPreviewed = () => {
  if (!palCommitted && palPreviewShown) submitSpec();
  palPreviewShown = false;
};
document.getElementById('pal-cancel').addEventListener('click', () => {
  palRestoreIfPreviewed();
  paletteDialog.close();
});
paletteDialog.addEventListener('close', palRestoreIfPreviewed);

// ----- wiring.

if (showUi) {
  for (const menuName of Object.keys(MENUS)) buildMenu(menuName);
  refreshUi();
  window.addEventListener('click', closeMenus);
  window.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') closeMenus();
  });

  // Generate: a fresh seed, plus new rolls for every unlocked dimension.
  generateBtn.addEventListener('click', () => {
    const after = { ...spec, seed: randomSeed() };
    for (const menuName of Object.keys(MENUS)) {
      const dim = MENUS[menuName].dim || menuName;
      if (spec[dim + 'Mode'] === 'random') after[dim] = pick(POOLS[dim]);
    }
    apply({ label: 'Generate', before: { ...spec }, after });
  });
  undoBtn.addEventListener('click', undo);
  redoBtn.addEventListener('click', redo);

  // Word-count sliders (inline on desktop, pop-down on phone): the
  // labels track the drag live; the rebuild (one undoable action) fires
  // on release.
  for (const slider of document.querySelectorAll('.max-input')) {
    slider.addEventListener('input', () => {
      for (const v of document.querySelectorAll('.max-val')) {
        v.textContent = slider.value;
      }
    });
    slider.addEventListener('change', () => {
      const maxWords = clampWords(+slider.value);
      if (maxWords === spec.maxWords) return;
      apply({
        label: 'Word Count',
        before: { ...spec },
        after: { ...spec, maxWords },
      });
    });
  }

  // Looseness sliders (inline on desktop, pop-down on phone): the
  // labels track the drag live; the relayout (one undoable action)
  // fires on release, like the word-count slider — every step is a
  // full rebuild, too heavy to preview per step.
  for (const slider of document.querySelectorAll('.loose-input')) {
    slider.addEventListener('input', () => {
      for (const v of document.querySelectorAll('.loose-val')) {
        v.textContent = `×${+slider.value}`;
      }
    });
    slider.addEventListener('change', () => {
      const loose = clampLoose(+slider.value);
      if (loose === spec.loose) return;
      apply({
        label: 'Looseness',
        before: { ...spec },
        after: { ...spec, loose },
      });
    });
  }

  // Recolor: same palette, fresh distribution — a new color seed and
  // nothing else. Repeated presses keep rerolling; undo walks back.
  document.getElementById('recolor').addEventListener('click', () => {
    apply({
      label: 'Recolor',
      before: { ...spec },
      after: { ...spec, colorSeed: randomSeed() },
    });
  });

  // Variance sliders (inline on desktop, pop-down on phone): index into
  // VARIANCES. The drag applies live — each step previews through the
  // recolor fast path (spec untouched, like the palette editor) — and
  // release commits the one undoable action (deduped: the preview
  // already showed it).
  for (const slider of document.querySelectorAll('.var-input')) {
    slider.addEventListener('input', () => {
      const [variance, short] = VARIANCES[+slider.value];
      for (const v of document.querySelectorAll('.var-val')) {
        v.textContent = short;
      }
      previewSpec({ ...spec, variance });
    });
    slider.addEventListener('change', () => {
      const [variance] = VARIANCES[+slider.value];
      if (variance === spec.variance) return;
      apply({
        label: 'Variance',
        before: { ...spec },
        after: { ...spec, variance },
      });
    });
  }

  // The phone-portrait pop-down buttons (word count, variance): a
  // dd-style button whose panel holds the slider.
  for (const rootId of ['dd-words', 'dd-variance', 'dd-loose']) {
    const root = document.getElementById(rootId);
    const btn = root.querySelector('.dd-btn');
    const panel = root.querySelector('.dd-panel');
    btn.addEventListener('click', (e) => {
      e.stopPropagation();
      const wasHidden = panel.hidden;
      closeMenus();
      if (wasHidden) {
        panel.hidden = false;
        positionMenuPanel(btn, panel);
      }
    });
    // Slider drags end with a click that must not dismiss the panel.
    panel.addEventListener('click', (e) => e.stopPropagation());
  }
  window.addEventListener('keydown', (e) => {
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'z' &&
        !dialog.open && !bookDialog.open && !exportDialog.open &&
        !creditsDialog.open && !feedbackDialog.open &&
        !paletteDialog.open && !sysFontDialog.open) {
      e.preventDefault();
      e.shiftKey ? redo() : undo();
    }
  });

  // "Use My Words": paste or load a local file (FileReader — the file
  // never leaves the machine), then cloud it. An empty textarea returns
  // to the current corpus. Undoable like everything else.
  const useBtn = document.getElementById('use-words');
  const userText = document.getElementById('user-text');
  const userFile = document.getElementById('user-file');
  const pinWordsDialog =
      makeDialogDraggable(dialog, document.getElementById('words-drag'));
  useBtn.addEventListener('click', () => {
    userText.value = spec.text;
    dialog.showModal();
    pinWordsDialog();
  });
  userFile.addEventListener('change', () => {
    const file = userFile.files[0];
    if (!file) return;
    const reader = new FileReader();
    reader.onload = () => { userText.value = reader.result; };
    reader.onerror = () => console.error('file read failed', reader.error);
    reader.readAsText(file);
  });
  document.getElementById('use-cancel').addEventListener('click', () => {
    dialog.close();
  });
  document.getElementById('use-go').addEventListener('click', () => {
    dialog.close();
    const text = userText.value.trim();
    if (text === spec.text) return;
    apply({
      label: 'Use My Words',
      before: { ...spec },
      after: { ...spec, text },
    });
  });

  // ----- "Use a Book": the bundled public-domain library
  // (tests/corpus/, built by tools/make-corpus.py). The manifest is
  // fetched once, on first open; the list filters as you type and sorts
  // by either column. Picking a book is one undoable action — the
  // worker stages the TSV on demand.
  const bookDialog = document.getElementById('book-dialog');
  const bookList = document.getElementById('book-list');
  const bookSearch = document.getElementById('book-search');
  let bookIndex = null;  // [{slug, title, author, category}]
  let bookSort = { key: 'title', asc: true };
  const fold = (s) =>
      s.normalize('NFD').replace(/\p{M}/gu, '').toLowerCase();
  async function loadBookIndex() {
    if (bookIndex) return;
    const r = await fetch('corpus/index.tsv');
    if (!r.ok) throw new Error(r.status);
    bookIndex = (await r.text()).split('\n')
        .filter((line) => line && !line.startsWith('#'))
        .map((line) => {
          const [slug, title, author, category] = line.split('\t');
          return { slug, title, author, category };
        });
  }
  function renderBookList() {
    const q = fold(bookSearch.value.trim());
    const rows = bookIndex
        .filter((b) => !q || fold(b.title + ' ' + b.author).includes(q))
        .sort((a, b) => {
          const cmp = a[bookSort.key].localeCompare(b[bookSort.key]) ||
              a.title.localeCompare(b.title);
          return bookSort.asc ? cmp : -cmp;
        });
    bookList.replaceChildren(...rows.map((b) => {
      const row = document.createElement('button');
      row.className = 'book-row';
      row.classList.toggle('selected',
          spec.text === '' && b.slug === spec.corpus);
      const title = document.createElement('span');
      title.className = 'book-title';
      title.textContent = b.title;
      const author = document.createElement('span');
      author.className = 'book-author';
      author.textContent = b.author;
      row.append(title, author);
      row.addEventListener('click', () => {
        bookDialog.close();
        if (spec.text === '' && spec.corpus === b.slug) return;
        apply({
          label: 'Use a Book',
          before: { ...spec },
          after: { ...spec, corpus: b.slug, text: '' },
        });
      });
      return row;
    }));
    for (const h of document.querySelectorAll('.book-head button')) {
      const on = h.dataset.key === bookSort.key;
      h.textContent =
          h.dataset.label + (on ? (bookSort.asc ? ' ▲' : ' ▼') : '');
      h.classList.toggle('selected', on);
    }
  }
  document.getElementById('use-book').addEventListener('click', () => {
    loadBookIndex().then(() => {
      renderBookList();
      bookDialog.showModal();
      bookSearch.select();
    }).catch((err) => console.error('book index: ' + err));
  });
  bookSearch.addEventListener('input', renderBookList);
  for (const h of document.querySelectorAll('.book-head button')) {
    h.addEventListener('click', () => {
      bookSort = {
        key: h.dataset.key,
        asc: bookSort.key === h.dataset.key ? !bookSort.asc : true,
      };
      renderBookList();
    });
  }
  document.getElementById('book-cancel').addEventListener('click', () => {
    bookDialog.close();
  });

  // ----- Word context menu: right-click a word to remove it from the
  // cloud (a nuisance word that means nothing to the text). The engine
  // hit-tests in scene space (camera-aware); removal adds the folded
  // key to spec.exclude — one undoable action, carried in copy links.
  // The menu is a .dd-panel, so closeMenus / Escape / click-away all
  // dismiss it for free.
  const wordMenu = document.getElementById('word-menu');
  function openWordMenu(word, clientX, clientY) {
    wordMenu.replaceChildren();
    if (word) {
      const remove = document.createElement('button');
      remove.className = 'dd-opt';
      remove.textContent = `Remove “${word}”`;
      remove.addEventListener('click', () => {
        closeMenus();
        const key = word.toLowerCase();
        if (spec.exclude.includes(key)) return;
        apply({
          label: 'Remove Word',
          before: { ...spec },
          after: { ...spec, exclude: [...spec.exclude, key] },
        });
      });
      wordMenu.appendChild(remove);
    }
    if (spec.exclude.length) {
      const n = spec.exclude.length;
      const restore = document.createElement('button');
      restore.className = 'dd-opt';
      restore.textContent =
          `Restore ${n} removed ${n === 1 ? 'word' : 'words'}`;
      restore.addEventListener('click', () => {
        closeMenus();
        apply({
          label: 'Restore Removed Words',
          before: { ...spec },
          after: { ...spec, exclude: [] },
        });
      });
      wordMenu.appendChild(restore);
    }
    if (!wordMenu.childElementCount) return;
    wordMenu.hidden = false;
    // At the cursor, clamped to the viewport.
    const margin = 8;
    wordMenu.style.left = '0px';
    wordMenu.style.top = '0px';
    const w = wordMenu.offsetWidth;
    const h = wordMenu.offsetHeight;
    wordMenu.style.left =
        `${Math.min(clientX, window.innerWidth - w - margin)}px`;
    wordMenu.style.top =
        `${Math.min(clientY, window.innerHeight - h - margin)}px`;
  }
  canvas.addEventListener('contextmenu', (e) => {
    e.preventDefault();
    closeMenus();
    if (busy) return;
    const rect = canvas.getBoundingClientRect();
    askWorker({
      type: 'hitTest',
      x: (e.clientX - rect.left) * dpr,
      y: (e.clientY - rect.top) * dpr,
    }).then((word) => openWordMenu(word, e.clientX, e.clientY));
  });
  // The click that opens the menu must not immediately close it.
  wordMenu.addEventListener('click', (e) => e.stopPropagation());

  // ----- Copy Link: the full-state URL for the current book cloud
  // (corpus, seed, every dimension with its lock/random mode, words,
  // variance). Only visible in book mode — see refreshUi.
  // navigator.clipboard exists only in secure contexts (https or
  // literal localhost) — a LAN-IP dev session needs the textarea +
  // execCommand fallback. Feedback swaps the icon too, since the text
  // label is hidden in the phone-portrait bar.
  const copyLinkBtn = document.getElementById('copy-link');
  const copyLinkIcon = document.getElementById('copy-link-icon');
  const copyLinkLabel = copyLinkBtn.querySelector('.btn-label');
  let copyLinkTimer = 0;
  const copyLinkFlash = (icon, label) => {
    copyLinkIcon.textContent = icon;
    copyLinkLabel.textContent = label;
    clearTimeout(copyLinkTimer);
    copyLinkTimer = setTimeout(() => {
      copyLinkIcon.textContent = '🔗';
      copyLinkLabel.textContent = ' Copy Link';
    }, 1500);
  };
  const legacyCopy = (text) => {
    const ta = document.createElement('textarea');
    ta.value = text;
    ta.style.position = 'fixed';
    ta.style.opacity = '0';
    document.body.append(ta);
    ta.select();
    ta.setSelectionRange(0, text.length);  // iOS needs the explicit range
    let ok = false;
    try { ok = document.execCommand('copy'); } catch { /* fall through */ }
    ta.remove();
    return ok;
  };
  copyLinkBtn.addEventListener('click', () => {
    const link = specUrl().href;
    const done = () => copyLinkFlash('✓', ' Copied!');
    const fail = () => {
      copyLinkFlash('✗', ' Copy failed');
      console.error('copy link failed; the link is: ' + link);
      status.textContent = link;  // last resort: copy it from here
    };
    if (navigator.clipboard && navigator.clipboard.writeText) {
      navigator.clipboard.writeText(link)
          .then(done)
          .catch(() => legacyCopy(link) ? done() : fail());
    } else {
      legacyCopy(link) ? done() : fail();
    }
  });

  // ----- Export dialog.
  const exportDialog = document.getElementById('export-dialog');
  const modeButtons = [...document.querySelectorAll('#export-mode button')];
  const sections = {
    png: document.getElementById('ex-png'),
    svg: document.getElementById('ex-svg'),
    pdf: document.getElementById('ex-pdf'),
  };
  const pngTransparent = document.getElementById('png-transparent');
  const svgTransparent = document.getElementById('svg-transparent');
  const pngSize = document.getElementById('png-size');
  const pngCustom = document.getElementById('png-custom');
  const pngW = document.getElementById('png-w');
  const pngH = document.getElementById('png-h');
  const pngDim = document.getElementById('png-dim');
  const pdfInches = document.getElementById('pdf-inches');
  const pdfDim = document.getElementById('pdf-dim');
  let exportMode = 'png';
  let aspect = 1.6;  // refreshed from the engine when the dialog opens

  const setMode = (mode) => {
    exportMode = mode;
    for (const b of modeButtons) {
      b.classList.toggle('selected', b.dataset.mode === mode);
    }
    for (const [key, el] of Object.entries(sections)) {
      el.hidden = key !== mode;
    }
  };
  for (const b of modeButtons) {
    b.addEventListener('click', () => setMode(b.dataset.mode));
  }

  const pngDims = () => {
    if (pngSize.value === 'custom') {
      return { width: Math.round(+pngW.value) || 16,
               height: Math.round(+pngH.value) || 16 };
    }
    const width = +pngSize.value;
    return { width, height: Math.round(width / aspect) };
  };
  const refreshDims = () => {
    const { width, height } = pngDims();
    pngDim.textContent =
        pngSize.value === 'custom' ? '' : `= ${width} × ${height} px`;
    const inches = +pdfInches.value || 11;
    pdfDim.textContent = `= ${inches} × ${(inches / aspect).toFixed(2)} in`;
  };

  pngSize.addEventListener('change', () => {
    const custom = pngSize.value === 'custom';
    pngCustom.hidden = !custom;
    if (custom && !pngW.value) {
      pngW.value = 1920;
      pngH.value = Math.round(1920 / aspect);
    }
    refreshDims();
  });
  // Custom dimensions stay aspect-locked: editing either edits both.
  pngW.addEventListener('input', () => {
    pngH.value = Math.round((+pngW.value || 0) / aspect) || '';
  });
  pngH.addEventListener('input', () => {
    pngW.value = Math.round((+pngH.value || 0) * aspect) || '';
  });
  pdfInches.addEventListener('input', refreshDims);

  document.getElementById('export-btn').addEventListener('click', async () => {
    const size = await askWorker({ type: 'sceneSize' });
    if (size.width > 0 && size.height > 0) {
      aspect = size.width / size.height;
    }
    if (pngSize.value === 'custom' && pngW.value) {
      pngH.value = Math.round(+pngW.value / aspect);
    }
    refreshDims();
    exportDialog.showModal();
  });
  document.getElementById('export-cancel').addEventListener('click', () => {
    exportDialog.close();
  });
  document.getElementById('export-go').addEventListener('click', async () => {
    exportDialog.close();
    const opts = exportMode === 'png'
        ? { transparent: pngTransparent.checked, ...pngDims() }
        : exportMode === 'svg'
        ? { transparent: svgTransparent.checked }
        : { inches: +pdfInches.value || 11 };
    const { blob, filename } = await buildExport(exportMode, opts);
    download(blob, filename);
  });

  // ----- Credits dialog: content/credits.md, rendered at build time and
  // fetched once.
  const creditsDialog = document.getElementById('credits-dialog');
  const creditsBody = document.getElementById('credits-body');
  let creditsLoaded = false;
  document.getElementById('credits-btn').addEventListener('click', async () => {
    if (!creditsLoaded) {
      try {
        const resp = await fetch('credits.html');
        if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
        creditsBody.innerHTML = await resp.text();
        creditsLoaded = true;
      } catch (err) {
        console.error('credits fetch failed', err);
        creditsBody.textContent = 'Credits are unavailable.';
      }
    }
    creditsDialog.showModal();
  });
  document.getElementById('credits-close').addEventListener('click', () => {
    creditsDialog.close();
  });
  // Belt and suspenders: the native Escape-cancel doesn't fire reliably
  // for this dialog everywhere.
  creditsDialog.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      e.preventDefault();
      creditsDialog.close();
    }
  });

  // ----- Feedback dialog: choose a category, compose right here, and
  // Send POSTs to the report relay (report-relay/), which files a GitHub
  // issue as the bot — no GitHub account needed. If the relay is
  // unreachable the old path survives as a fallback link: the prefilled
  // GitHub issue form (via the form field's id as a query parameter).
  const RELAY_URL = 'https://words-report.vercel.app/api/v1/report';
  const feedbackDialog = document.getElementById('feedback-dialog');
  document.getElementById('fb-build').textContent =
      `build ${BUILD.id} · ${BUILD.date}`;
  // navigator.platform is frozen ("MacIntel" even on Apple Silicon), as
  // is the userAgent's OS version; ask Client Hints for the real
  // platform and architecture where supported.
  const envReport = (platform) => {
    // visualViewport catches page-level pinch zoom (scale != 1), which
    // masquerades as rendering bugs in reports.
    const vv = window.visualViewport;
    const view = vv
        ? `${Math.round(vv.width)}x${Math.round(vv.height)} ` +
          `@scale ${vv.scale.toFixed(2)}`
        : 'n/a';
    return [
      `build: ${BUILD.id} (${BUILD.date})`,
      `url: ${location.href}`,
      `spec: ${specUrl().href}`,
      `platform: ${platform}`,
      `userAgent: ${navigator.userAgent}`,
      `viewport: ${innerWidth}x${innerHeight} @${devicePixelRatio}x`,
      `visualViewport: ${view}`,
      `canvas: ${canvas.clientWidth}x${canvas.clientHeight} css, ` +
          `${canvas.width}x${canvas.height} buffer`,
    ].join('\n');
  };
  // The report is refreshed when the dialog opens; userAgentData's
  // high-entropy values arrive async, so start early and patch envText
  // (and the compose preview) when they land.
  let envText = envReport(navigator.platform);
  const fbEnvPre = document.getElementById('fb-env-text');
  const refreshEnv = () => {
    envText = envReport(navigator.platform);
    fbEnvPre.textContent = envText;
    if (navigator.userAgentData) {
      navigator.userAgentData
          .getHighEntropyValues(['platformVersion', 'architecture', 'bitness'])
          .then((hints) => {
            envText = envReport(
                `${navigator.userAgentData.platform} ${hints.platformVersion} ` +
                `${hints.architecture}${hints.bitness}`);
            fbEnvPre.textContent = envText;
          })
          .catch(() => { /* keep the legacy value */ });
    }
  };
  const fbViews = ['fb-choose', 'fb-compose', 'fb-done']
      .map((id) => document.getElementById(id));
  const fbShow = (id) => {
    for (const v of fbViews) v.hidden = v.id !== id;
  };
  const fbText = document.getElementById('fb-text');
  const fbSend = document.getElementById('fb-send');
  const fbHints = {
    bug: 'What went wrong, and what did you expect instead?',
    feature: 'What would you like the app to do?',
    feedback: 'What’s on your mind?',
  };
  let fbCategory = 'feedback';
  let fbTemplate = 'feedback.yml';
  document.getElementById('feedback-btn').addEventListener('click', () => {
    refreshEnv();
    fbShow('fb-choose');
    feedbackDialog.showModal();
  });
  document.getElementById('feedback-cancel').addEventListener('click', () => {
    feedbackDialog.close();
  });
  feedbackDialog.addEventListener('keydown', (e) => {
    if (e.key === 'Escape') {
      e.preventDefault();
      feedbackDialog.close();
    }
    // Enter in the textarea must insert a newline, not submit-and-close.
    if (e.key === 'Enter' && e.target === fbText) e.stopPropagation();
  });
  for (const b of document.querySelectorAll('.fb-opt')) {
    b.addEventListener('click', () => {
      fbCategory = b.dataset.category;
      fbTemplate = b.dataset.template;
      document.getElementById('fb-compose-hint').textContent =
          fbHints[fbCategory];
      fbShow('fb-compose');
      fbText.focus();
    });
  }
  document.getElementById('fb-back').addEventListener('click', () => {
    fbShow('fb-choose');
  });
  const fbFinish = (msg, failed) => {
    document.getElementById('fb-done-msg').innerHTML = msg;
    document.getElementById('fb-fail-actions').hidden = !failed;
    fbShow('fb-done');
  };
  fbSend.addEventListener('click', async () => {
    const text = fbText.value.trim();
    if (!text) {
      fbText.focus();
      return;
    }
    fbSend.disabled = true;
    fbSend.textContent = 'Sending…';
    // The fallback link is a real <a>, so its click is its own user
    // activation — the await here can't get its popup blocked.
    document.getElementById('fb-github-link').href =
        'https://github.com/jdf/words/issues/new?template=' + fbTemplate +
        '&environment=' + encodeURIComponent(envText);
    let ok = false;
    let issue = null;
    try {
      const res = await fetch(RELAY_URL, {
        method: 'POST',
        headers: {'Content-Type': 'application/json'},
        body: JSON.stringify(
            {category: fbCategory, text, environment: envText, log: ''}),
      });
      if (res.status === 201) {
        issue = await res.json();
        ok = true;
      }
    } catch (_) { /* network error: fall through to the fallback */ }
    fbSend.disabled = false;
    fbSend.textContent = 'Send';
    if (ok) {
      fbText.value = '';
      fbFinish(
          `Reported as <a href="${issue.url}" target="_blank" ` +
              `rel="noopener">#${issue.number}</a> — thank you!`,
          false);
    } else {
      fbFinish(
          'Couldn’t reach the report service. Your text is kept if you ' +
              'want to retry later; meanwhile you can open the GitHub ' +
              'form (needs an account) or copy the report.',
          true);
    }
  });
  document.getElementById('fb-copy').addEventListener('click', (e) => {
    e.preventDefault();
    navigator.clipboard
        .writeText(`${fbText.value.trim()}\n\n### Environment\n${envText}`)
        .then(() => { e.target.textContent = 'Copied!'; })
        .catch(() => { e.target.textContent = 'Copy failed'; });
    setTimeout(() => { e.target.textContent = 'Copy report'; }, 1500);
  });
  document.getElementById('fb-close').addEventListener('click', () => {
    feedbackDialog.close();
  });
}
