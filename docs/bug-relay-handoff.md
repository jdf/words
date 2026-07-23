# Bug-relay handoff: file issues from the feedback dialog, no GitHub login

This note is for a fresh Claude session in `~/words`. It explains a thing we
built for the sibling project **spdsx-patchedit** — an in-app "Report a Bug"
dialog that files a GitHub issue through a tiny serverless relay, so the user
never needs a GitHub account — and how to bring the same capability to the
`words` app at https://mrfeinberg.com/words/.

## The idea

Today the words feedback dialog (`web/app.js`, search `feedback-dialog`) opens
`https://github.com/jdf/words/issues/new?...` in a new tab. That prefills a
GitHub issue form, but the user must be logged into GitHub to actually submit
it. Most people aren't, so the report is lost.

The relay removes that wall. A **~30-line Vercel function** holds a GitHub token
server-side and exposes one endpoint:

```
POST /api/v1/report   { category, text, environment, log }
  -> 201 { number, url }      // the filed issue
```

The dialog POSTs the user's words plus the environment block it already
assembles, and the relay files the issue as the bot. Nobody logs in. The token
lives only on Vercel, never in the shipped page.

## What already exists to copy from

Everything is in the spdsx-patchedit repo under `report-relay/`
(`~/hax/spdsx-patchedit/report-relay/`):

- `api/v1/report.js` — the whole function. Validates the payload, enforces
  payload caps (20 KB text / 4 KB env / 40 KB log) and a best-effort
  per-instance rate limit (10/hour/IP), **fences** the user's text in four
  backticks so `@mentions`, `#refs` and markdown render inert, and files the
  issue with labels `in-app` + `bug`/`enhancement`/`feedback`. On a 422 (e.g. a
  label that doesn't exist) it retries once without labels, so a report is never
  lost to a missing label.
- `vercel.json` — one line:
  `{ "functions": { "api/v1/report.js": { "maxDuration": 10 } } }`.
- `smoke.js` — a local harness that exercises the handler with a fake `fetch`.
- `README.md` — env vars and the deploy/smoke recipe.

Copy that directory into `~/words/report-relay/` as the starting point, then
make the changes in "Porting to words" below.

## How we deployed it (spdsx-patchedit)

For reference — the exact steps that worked. In words, the relay is its own
separate Vercel project; the words app's own deploy (`./deploy` rsync + GitHub
Pages) is unchanged.

1. `npm install -g vercel` (CLI wasn't installed; Node was).
2. `vercel whoami` triggered the browser device-code login flow (the user had
   already created a Vercel account).
3. From `report-relay/`: `vercel deploy --prod --yes` — creates the project and
   deploys. First deploy gives an ugly per-deploy URL.
4. Attach a **stable** domain so the app never has to change:
   `vercel domains add <name>.vercel.app <project>` then
   `vercel alias set <deploy-url> <name>.vercel.app`. After that, every
   `vercel deploy --prod` updates that domain automatically.
5. Set the token as a Sensitive env var **without pasting it into the
   conversation**. The `!`-prefixed shell is non-interactive, so pipe it in:
   `pbpaste | vercel env add GITHUB_TOKEN production` (copy the PAT to the
   clipboard first).
6. `vercel deploy --prod --yes` again so the running function picks up the new
   env var. Confirm with `vercel inspect <domain>` that the domain points at the
   fresh deployment.

### Environment variables

| var             | required | meaning                                                                                                                      |
| --------------- | -------- | ---------------------------------------------------------------------------------------------------------------------------- |
| `GITHUB_TOKEN`  | yes      | fine-grained PAT scoped to **one** repo, permission Issues: Read/write, nothing else                                         |
| `GITHUB_REPO`   | no       | target repo; **set it to `jdf/words`** (the code's default was the spdsx repo)                                               |
| `REPORT_SECRET` | no       | if set, requests must carry it in `x-report-secret`. See the browser caveat below — in a web app this is close to worthless. |

The PAT: https://github.com/settings/personal-access-tokens/new → Repository
access **Only select repositories** → `jdf/words` → Permissions **Issues: Read
and write** only.

## Gotchas we already hit

- **`GITHUB_REPO` default.** The function defaulted to a repo name that didn't
  exist on GitHub, so every report would have 404'd. For words the default must
  be `jdf/words` (the directory name and the GitHub repo name match here, unlike
  spdsx). Fix it in `report.js` **and** set the env var, belt and suspenders.
- **Labels must already exist.** GitHub does **not** auto-create labels on issue
  creation. If `in-app` / `bug` / `enhancement` / `feedback` don't exist in
  `jdf/words`, the first POST 422s and the relay's fallback re-files the issue
  **without** labels — the report survives but lands unlabeled. Create the four
  labels once (`gh label create in-app -R jdf/words -c 5319e7`, etc.) for clean
  triage.
- **The permission classifier blocks Claude from POSTing to the live endpoint.**
  Curl POSTs (and even running the local `smoke.js`) got denied mid-session.
  Have the user run the smoke test themselves via `! curl ...`, or just test
  from the app. Then close the throwaway issue.
- Both spdsx repos use **jj**, commit-only for Claude — the user pushes. Same in
  words (`jj push-main` runs `./presubmit.sh`; the user pushes and deploys).

## Porting to words — the real differences

spdsx-patchedit is a native JUCE app that POSTs with `juce::URL`. words is a
**browser** app, and that changes three things.

### 1. CORS — the relay must allow the browser origin

A native app has no origin; a browser does, and the page at `mrfeinberg.com`
POSTing to `*.vercel.app` is cross-origin. The `Content-Type: application/json`
body also triggers a CORS **preflight** `OPTIONS`. Add this to `report.js`:

```js
const ALLOW_ORIGINS = new Set([
  "https://mrfeinberg.com",
  "https://www.mrfeinberg.com",
  "https://jdf.github.io", // the GitHub Pages mirror
  "http://localhost:8787", // ./dev
]);

function cors(req, res) {
  const origin = req.headers.origin;
  if (origin && ALLOW_ORIGINS.has(origin)) {
    res.setHeader("Access-Control-Allow-Origin", origin);
    res.setHeader("Vary", "Origin");
  }
  res.setHeader("Access-Control-Allow-Methods", "POST, OPTIONS");
  res.setHeader("Access-Control-Allow-Headers", "Content-Type");
}

// at the very top of module.exports:
module.exports = async (req, res) => {
  cors(req, res);
  if (req.method === "OPTIONS") return res.status(204).end();
  // ...existing POST handling...
};
```

### 2. The endpoint is public — `REPORT_SECRET` can't hide

In the native app the secret ships inside a compiled binary — a weak filter, but
something. In a web app any secret in `app.js` is visible in DevTools, so it
deters nothing. Lean on the rate limit, the origin allowlist, and the `in-app`
label for triage instead. If real abuse ever shows up, put Cloudflare Turnstile
(or hCaptcha) in front of the Send button and verify the token in the relay, or
move the rate limit to Upstash.

### 3. The dialog needs a compose step, then a `fetch`

Right now each option button just opens a GitHub URL. To file directly you need
to (a) collect the user's text in-app and (b) POST it. The env block is already
built (`envReport()` in `app.js` — keep it verbatim; the `spec:` repro URL in it
is the best part). Sketch, to adapt into the existing `<dialog>`:

```js
const RELAY_URL = "https://words-report.vercel.app/api/v1/report";
const CATEGORY = {
  "bug-report.yml": "bug",
  "feature-request.yml": "feature",
  "feedback.yml": "feedback",
};

async function fileReport(category, text) {
  try {
    const res = await fetch(RELAY_URL, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ category, text, environment: envText, log: "" }),
    });
    if (res.status === 201) {
      const { number, url } = await res.json();
      // show "Reported as #<number>" with a link to <url>
      return true;
    }
  } catch (_) {
    /* network error falls through */
  }
  return false; // caller falls back to the GitHub form (below)
}
```

Recommended UX, mirroring what we shipped in spdsx: clicking a category no
longer opens a tab — it reveals a `<textarea>` and a **Send** button in the same
dialog; Send calls `fileReport()`; on success show `Reported as #N` with a link
to the issue; on failure keep the report recoverable. The cleanest fallback is
the current behavior: if the POST fails, open the prefilled
`issues/new?template=...&environment=...` GitHub URL you already build, so a
determined user with an account loses nothing. The `data-template` values map to
categories via `CATEGORY` above.

One nicety the native app had: a "Copy report" button on failure that puts the
text + environment on the clipboard. Cheap to add and worth it.

Note the existing popup-activation gotcha (an `await` between click and
`window.open` gets the popup blocked on iOS) **stops mattering** once Send files
via `fetch` instead of opening a tab — but it still applies to the GitHub-form
fallback, so build the fallback URL synchronously if you keep it.

## Suggested order of work in words

1. `cp -r ~/hax/spdsx-patchedit/report-relay ~/words/report-relay`, then edit
   `report.js`: default `GITHUB_REPO` to `jdf/words` and add the CORS block.
2. Create the four labels in `jdf/words`.
3. Deploy the relay as a new Vercel project; give it a stable domain
   (`words-report.vercel.app` or similar); set `GITHUB_TOKEN` (PAT scoped to
   `jdf/words`); redeploy.
4. Add the compose `<textarea>` + Send to `#feedback-dialog` in
   `web/index.html`, and the `fileReport()` wiring in `web/app.js`, keeping the
   GitHub-form path as the failure fallback. Point `RELAY_URL` at the domain
   from step 3.
5. Test from `./dev` (localhost:8787 is in the CORS allowlist) — file a real
   test issue, confirm it lands in `jdf/words`, then close it.
6. Commit (jj). The user pushes and runs `./deploy`.

## The spdsx-patchedit reference, in one place

- Relay source: `~/hax/spdsx-patchedit/report-relay/`
- Live relay (spdsx): `https://spdsx-patchedit-report.vercel.app/api/v1/report`
  → files to `jdf/spdsx-pro-kitedit`
- The native-side dialog we built alongside it, for UX reference:
  `~/hax/spdsx-patchedit/source/feedback_dialog.{h,cc}` (three pages: choose
  category → compose with a verbatim preview of what's attached → done, with the
  issue link or a copy-to-clipboard fallback).
