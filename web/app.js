// The page is pure UI: the engine (wasm + WebGL) lives in a worker
// with an OffscreenCanvas, so layout never blocks this thread. The
// engine posts progress/idle; we relay prints, gate inputs, and keep
// the URL naming what's on screen.
const status = document.getElementById('status');
const progress = document.getElementById('progress');
const canvas = document.getElementById('canvas');
const params = new URLSearchParams(location.search);

// The toolbar is on by default; tests (and anyone wanting a bare
// canvas) pass ?no-ui. ?ui is still accepted as a no-op.
const showUi = !params.has('no-ui');

// Show the toolbar before measuring the canvas: it takes layout space,
// and the OffscreenCanvas is sized from the measurement below.
if (showUi) {
  document.getElementById('panel').style.display = 'flex';
}

// moby-dick is the default corpus; ?text= (or an explicit ?corpus=)
// overrides it.
const corpus =
    params.get('corpus') || (params.has('text') ? '' : 'moby-dick');
const lazyFiles = [];
if (corpus) {
  lazyFiles.push({
    url: 'corpus/' + encodeURIComponent(corpus) + '.tsv',
    path: '/corpus.tsv',
  });
}
if (params.get('font')) {
  lazyFiles.push({
    url: 'fonts/' + encodeURIComponent(params.get('font')) + '.ttf',
    path: '/font-override.ttf',
  });
}

const dpr = window.devicePixelRatio || 1;
canvas.width = Math.round(canvas.clientWidth * dpr);
canvas.height = Math.round(canvas.clientHeight * dpr);
const offscreen = canvas.transferControlToOffscreen();

const worker = new Worker('worker.js');
worker.postMessage(
    { type: 'init', canvas: offscreen, search: location.search, lazyFiles },
    [offscreen]);

// One command in flight at a time; the newest queued spec wins. The
// text payload (possibly megabytes) rides along only when it changed;
// the worker keeps the staged copy otherwise.
let busy = true;  // the initial build is in progress
let pendingSpec = null;
let stagedText = null;  // what the worker currently has
const sendSpec = (s) => {
  busy = true;
  window.__wordsIdle = false;
  const msg = {
    type: 'rebuild',
    seed: s.seed,
    orientation: s.orientation,
    palette: s.palette,
    font: s.font,
    useText: s.text !== '',
  };
  if (s.text !== '' && s.text !== stagedText) {
    msg.text = s.text;
    stagedText = s.text;
  }
  worker.postMessage(msg);
};

worker.onmessage = (e) => {
  const m = e.data;
  if (m.type === 'print') {
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
    // its screenshot. The idle message can outrun the worker's canvas
    // commit (separate threads), so let two compositor frames pass
    // before declaring the pixels ready.
    requestAnimationFrame(() => requestAnimationFrame(() => {
      window.__wordsIdle = true;
    }));
    progress.style.opacity = 0;
    progress.style.width = '0';
    if (pendingSpec !== null) {
      const s = pendingSpec;
      pendingSpec = null;
      sendSpec(s);
    }
  }
};

// Resize events flood during a drag; coalesce to at most one message
// per animation frame (the rAF always reads the *current* size, so
// the final geometry is never missed) to keep the worker from
// re-rendering dozens of intermediate sizes — that churn reads as
// flicker.
let resizeQueued = false;
window.addEventListener('resize', () => {
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

// The engine's console handle, a shim over the worker:
// words._wordsLogScene() still dumps engine state to the console.
window.words = {
  _wordsLogScene: () => worker.postMessage({ type: 'logScene' }),
};

// The cloud spec and its undo machinery. Every mutation flows through
// apply() as an action holding before/after spec snapshots — an object
// that knows how to undo (and redo) itself — so any change to the
// cloud is reversible. The engine is a deterministic function of the
// spec: submitting is just sending the current values, and the URL
// mirrors them so the address bar always names the cloud on screen.
const spec = {
  seed: parseInt(params.get('seed'), 10) || 1447,
  orientation: params.get('orientation') || 'mostly-horizontal',
  palette: params.get('palette') || '',  // '' = the built-in dark scheme
  font: params.get('font') || 'sexsmith',
  text: '',  // user-pasted words; '' = corpus / ?text= / sample
};
const undoStack = [];
const redoStack = [];

const submitSpec = () => {
  const url = new URL(location);
  url.searchParams.set('seed', spec.seed);
  url.searchParams.set('orientation', spec.orientation);
  url.searchParams.set('font', spec.font);
  spec.palette ? url.searchParams.set('palette', spec.palette)
               : url.searchParams.delete('palette');
  history.replaceState(null, '', url);
  busy ? pendingSpec = { ...spec } : sendSpec({ ...spec });
};
const apply = (action) => {
  undoStack.push(action);
  redoStack.length = 0;
  Object.assign(spec, action.after);
  submitSpec();
  refreshButtons();
};
const undo = () => {
  const action = undoStack.pop();
  if (!action) return;
  redoStack.push(action);
  Object.assign(spec, action.before);
  submitSpec();
  refreshButtons();
};
const redo = () => {
  const action = redoStack.pop();
  if (!action) return;
  undoStack.push(action);
  Object.assign(spec, action.after);
  submitSpec();
  refreshButtons();
};

// ?ui shows the dev panel: 🔄 quietly bumps the seed for a fresh
// layout; undo/redo walk the action stacks.
const relayoutBtn = document.getElementById('relayout');
const undoBtn = document.getElementById('undo');
const redoBtn = document.getElementById('redo');
const dialog = document.getElementById('words-dialog');
const refreshButtons = () => {
  undoBtn.disabled = undoStack.length === 0;
  redoBtn.disabled = redoStack.length === 0;
};
// Scramble candidates — a stopgap until real selection UI exists.
// Fonts are the original collection's Basic Latin faces
// (assets/fonts/capabilities.txt); moby-dick and the sample text are
// English, so any of them can render the cloud.
const scrambleOrientations =
    ['mostly-horizontal', 'horizontal', 'any-which-way'];
const scramblePalettes = [
  'bw', 'wb', 'wordly', 'asparagus', 'bluesugar', 'heat', 'ghostly',
  'chilled-summer', 'blue-meets-orange', 'yramirp',
];
const scrambleFonts = [
  'primerprintmedium', 'grilledcheese', 'owned', 'teen', 'berylium',
  'telephoto', 'coolvetica', 'gnuolane', 'steelfish', 'kenyan', 'king',
  'goudy', 'duality', 'sexsmith', 'mailrays', 'exprswy_free', 'powell',
  'fridge', 'tanklite', 'melochergbold', 'vigo', 'leaguegothic',
  'chunkfive', 'enamel', 'jblack', 'opensansbold',
];
const pick = (list) => list[Math.floor(Math.random() * list.length)];

if (showUi) {
  relayoutBtn.addEventListener('click', () => apply({
    label: 'Scramble',
    before: { ...spec },
    after: {
      ...spec,
      seed: spec.seed + 1,
      orientation: pick(scrambleOrientations),
      palette: pick(scramblePalettes),
      font: pick(scrambleFonts),
    },
  }));
  undoBtn.addEventListener('click', undo);
  redoBtn.addEventListener('click', redo);
  window.addEventListener('keydown', (e) => {
    if ((e.metaKey || e.ctrlKey) && e.key.toLowerCase() === 'z' &&
        !dialog.open) {
      e.preventDefault();
      e.shiftKey ? redo() : undo();
    }
  });

  // "Use My Words": paste or load a local file (FileReader — the file
  // never leaves the machine), then cloud it. An empty textarea
  // returns to the default corpus. Undoable like everything else.
  const useBtn = document.getElementById('use-words');
  const userText = document.getElementById('user-text');
  const userFile = document.getElementById('user-file');
  useBtn.addEventListener('click', () => {
    userText.value = spec.text;
    dialog.showModal();
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
}
