// The engine's home: this worker owns the wasm module and the
// OffscreenCanvas, so text analysis, shaping, layout, and rendering all
// happen off the main thread. The page (index.html) sends an init message
// with the transferred canvas, its URL query string, and the list of
// lazily fetched resources; afterwards, commands (setSeed, resize,
// logScene) dispatch to the engine's exports. The engine posts progress
// and idle messages itself (see src/main.cc); print/printErr are relayed
// here.
'use strict';

let engine = null;
const pending = [];  // commands that arrive while the module boots

function handle(msg) {
  switch (msg.type) {
    case 'rebuild': rebuild(msg); break;
    case 'resize': engine._wordsResize(msg.width, msg.height); break;
    case 'logScene': engine._wordsLogScene(); break;
    default: console.warn('worker: unknown message', msg);
  }
}

// Rebuild the cloud from a spec {seed, orientation, palette, font,
// useText, text?}. Fonts are staged into MEMFS on first use (the page
// only ever names them); on a failed fetch the engine keeps its current
// font ("" path). User text arrives in `text` only when it changed —
// once staged, `useText` alone selects it.
const kUserTextPath = '/user-text.txt';
async function rebuild(msg) {
  let path = '/fonts/' + msg.font + '.ttf';
  if (!engine.FS.analyzePath(path).exists) {
    try {
      const r = await fetch('fonts/' + encodeURIComponent(msg.font) + '.ttf');
      if (!r.ok) throw new Error(r.status);
      engine.FS.writeFile(path, new Uint8Array(await r.arrayBuffer()));
    } catch (err) {
      postMessage({ type: 'printErr', text: 'font ' + msg.font + ': ' + err });
      path = '';
    }
  }
  if (msg.text !== undefined) {
    engine.FS.writeFile(kUserTextPath, new TextEncoder().encode(msg.text));
  }
  engine.ccall('wordsRebuild', null,
               ['number', 'string', 'string', 'string', 'string'],
               [msg.seed, msg.orientation, msg.palette, path,
                msg.useText ? kUserTextPath : '']);
}

function init(msg) {
  // The engine reads URL parameters from here (a worker's own location is
  // the script URL, not the page's).
  self.__wordsSearch = msg.search;
  importScripts('words.js');
  // createWordsModule adopts this object as the Module, so the exported
  // runtime methods (FS, add/removeRunDependency) appear on it by the
  // time the preRun callback fires.
  const config = {
    canvas: msg.canvas,
    print: (text) => postMessage({ type: 'print', text }),
    printErr: (text) => postMessage({ type: 'printErr', text }),
    preRun: [() => {
      for (const f of msg.lazyFiles) {
        config.addRunDependency(f.url);
        fetch(f.url)
          .then((r) => {
            if (!r.ok) throw new Error(r.status + ' ' + f.url);
            return r.arrayBuffer();
          })
          .then((buf) => {
            config.FS.writeFile(f.path, new Uint8Array(buf));
            config.removeRunDependency(f.url);
          })
          .catch((err) => {
            postMessage({ type: 'printErr', text: 'failed to load ' + f.url + ': ' + err });
            // Release the boot anyway: the engine falls back (sample text
            // for a missing corpus, the preloaded font) rather than
            // hanging the module forever.
            config.removeRunDependency(f.url);
          });
      }
    }],
  };
  createWordsModule(config).then((module) => {
    engine = module;
    for (const p of pending) handle(p);
    pending.length = 0;
  }).catch((err) => {
    postMessage({ type: 'printErr', text: 'failed to start: ' + err });
  });
}

onmessage = (e) => {
  const msg = e.data;
  if (msg.type === 'init') {
    init(msg);
  } else if (engine) {
    handle(msg);
  } else {
    pending.push(msg);
  }
};
