#!/usr/bin/env bash
set -euo pipefail

readonly ORIGINAL_COMMAND="${SSH_ORIGINAL_COMMAND:-}"
if [[ "${ORIGINAL_COMMAND}" =~ ^publish\ ([0-9a-f]{40})$ ]]; then
    exec sudo -n /usr/local/sbin/deploy-blog-content-publish "${BASH_REMATCH[1]}"
fi
if [[ "${ORIGINAL_COMMAND}" =~ ^ai-publish\ ([0-9a-f]{40})$ ]]; then
    exec sudo -n /usr/local/sbin/deploy-blog-ai-publish "${BASH_REMATCH[1]}"
fi
if [[ "${ORIGINAL_COMMAND}" =~ ^deploy\ ([0-9a-f]{40})$ ]]; then
    exec sudo -n /usr/local/sbin/deploy-blog-content "${BASH_REMATCH[1]}"
fi

printf 'Only deploy, publish, or ai-publish with a 40-character SHA is allowed.\n' >&2
exit 64
