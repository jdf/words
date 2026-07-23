# report-relay

The tiny Vercel function that lets words users file bug reports without a GitHub
account: the app's Feedback dialog (`web/app.js`) POSTs a report here, and this
relay — which holds the GitHub token — files it as an issue in `jdf/words`.

One endpoint: `POST /api/v1/report` with `{category, text, environment, log}`;
replies `201 {number, url}`. Reports are labeled `in-app` plus
`bug`/`enhancement`/`feedback`, with the user's text and the environment block
fenced (so markdown, `@mentions` and `#refs` render inert).

Because the caller is a browser page, the relay answers CORS preflights and
echoes `Access-Control-Allow-Origin` only for the allowlisted origins
(mrfeinberg.com, the GitHub Pages mirror, and `./dev`'s localhost:8787) — see
`ALLOW_ORIGINS` in `api/v1/report.js`.

This is its own Vercel project, deployed independently of the words app
(`./deploy` / GitHub Pages are unchanged).

## Deploy

```sh
cd report-relay
vercel deploy --prod
```

Environment variables (Vercel dashboard ▸ Settings ▸ Environment Variables):

| var            | required | meaning                                                                          |
| -------------- | -------- | -------------------------------------------------------------------------------- |
| `GITHUB_TOKEN` | yes      | fine-grained PAT scoped to ONE repo, permission Issues: read/write, nothing else |
| `GITHUB_REPO`  | no       | target repo, default `jdf/words`                                                 |

There is no shared-secret gate: any secret shipped in `app.js` is visible in
DevTools, so it would deter nothing. The abuse posture is the rate limit, the
origin allowlist, and the `in-app` label for triage.

The app points at the relay via `RELAY_URL` in `web/app.js`.

## Smoke test

Local, no network (fake `fetch`):

```sh
node smoke.js
```

Live:

```sh
curl -sS -X POST "$URL/api/v1/report" \
  -H 'Content-Type: application/json' \
  -d '{"category":"feedback","text":"relay smoke test — please close me",
       "environment":"app: smoke","log":""}'
```

Expect `201` and the issue URL in the reply (then close the issue).

## Abuse posture

Payload caps (20 KB text / 4 KB env / 40 KB log), a best-effort per-instance
rate limit (10/hour/IP), and everything it creates carries the `in-app` label
for one-click triage. If real abuse ever shows up: put Cloudflare Turnstile in
front of the Send button and verify the token here, move the rate limit to
Upstash, or point `GITHUB_REPO` at a dedicated reports repo so the blast radius
is an issue list and nothing else.
