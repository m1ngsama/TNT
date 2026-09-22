#!/bin/sh
. ./lib.sh

PORT=${PORT:-12369}

if ! command -v bun >/dev/null 2>&1; then
    echo "bun not installed; skipping gateway end-to-end test"
    exit 0
fi
tnt_skip_without_expect "gateway end-to-end test"
tnt_require_binary

cd ../gateway || exit 1
bun install --frozen-lockfile >/dev/null || exit 1
bun run build >/dev/null || exit 1
TNT_E2E=1 PORT="$PORT" bun test e2e
