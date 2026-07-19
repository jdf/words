#!/usr/bin/env python3
"""Post a GitHub issue comment as the claude-at-words[bot] App.

Comments are attributed to whoever's token hits the API, so Claude's
comments through jdf's gh login would read as jdf. This posts through
the claude-at-words GitHub App instead, giving Claude's words their own
[bot] identity — the comment-world analog of the Co-Authored-By trailer
on commits.

Credentials live outside the repo in ~/.config/words-bot/ (override with
WORDS_BOT_DIR): app-id, and app.pem (the App's private key). Flow: sign
a short-lived RS256 JWT with openssl, look up the repo installation,
mint an installation token, post.

Usage:
  tools/bot-comment.py ISSUE_NUMBER          # body from stdin
  tools/bot-comment.py ISSUE_NUMBER FILE     # body from FILE
"""

import base64
import json
import os
import pathlib
import subprocess
import sys
import time
import urllib.request

REPO = "jdf/words"


def b64url(data: bytes) -> str:
    return base64.urlsafe_b64encode(data).rstrip(b"=").decode()


def api(url: str, token: str, scheme: str, payload=None):
    req = urllib.request.Request(
        url,
        data=json.dumps(payload).encode() if payload is not None else None,
        headers={
            "Authorization": f"{scheme} {token}",
            "Accept": "application/vnd.github+json",
        },
        method="POST" if payload is not None else "GET",
    )
    with urllib.request.urlopen(req) as resp:
        return json.load(resp)


def main() -> None:
    if len(sys.argv) not in (2, 3):
        sys.exit(__doc__)
    issue = int(sys.argv[1])
    body = (pathlib.Path(sys.argv[2]).read_text()
            if len(sys.argv) == 3 else sys.stdin.read()).strip()
    if not body:
        sys.exit("empty comment body")

    conf = pathlib.Path(os.environ.get("WORDS_BOT_DIR",
                                       pathlib.Path.home() / ".config" /
                                       "words-bot"))
    app_id = (conf / "app-id").read_text().strip()
    pem = conf / "app.pem"

    now = int(time.time())
    header = b64url(json.dumps({"alg": "RS256", "typ": "JWT"}).encode())
    claims = b64url(json.dumps(
        {"iat": now - 60, "exp": now + 540, "iss": app_id}).encode())
    signing_input = f"{header}.{claims}".encode()
    signature = subprocess.run(
        ["openssl", "dgst", "-sha256", "-sign", str(pem)],
        input=signing_input, capture_output=True, check=True).stdout
    jwt = f"{header}.{claims}.{b64url(signature)}"

    installation = api(f"https://api.github.com/repos/{REPO}/installation",
                       jwt, "Bearer")
    token = api(
        f"https://api.github.com/app/installations/{installation['id']}"
        "/access_tokens", jwt, "Bearer", payload={})["token"]

    comment = api(
        f"https://api.github.com/repos/{REPO}/issues/{issue}/comments",
        token, "token", payload={"body": body})
    print(f"{comment['user']['login']}: {comment['html_url']}")


if __name__ == "__main__":
    main()
