#!/usr/bin/env bash
set -euo pipefail

readonly ORIGINAL_COMMAND="${SSH_ORIGINAL_COMMAND:-}"
if [[ "$ORIGINAL_COMMAND" =~ ^publish\ ([0-9a-f]{40})$ ]]; then
    exec sudo -n /usr/local/sbin/deploy-blog-content publish "${BASH_REMATCH[1]}"
fi

printf 'Only "publish <40-hex content digest>" is allowed.\n' >&2
exit 64
