#!/usr/bin/env bash
# Status line for Claude Code: the project's pinned links plus the live
# jj change, always visible under the input box. Configured by
# .claude/settings.json (statusLine).
cat > /dev/null  # consume the context JSON; everything here is static-ish
change=$(jj log --no-graph -r @ -T 'change_id.short(8)' 2>/dev/null)
printf 'words%s · github.com/jdf/words · mrfeinberg.com/words' \
  "${change:+ @ $change}"
