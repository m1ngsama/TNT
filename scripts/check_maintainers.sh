#!/bin/sh
# Verify that repository paths are covered by MAINTAINERS.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GET_MAINTAINER="$ROOT/scripts/get_maintainer.sh"

usage() {
    cat <<'USAGE'
Usage: scripts/check_maintainers.sh [PATH ...]

With no paths, checks git-tracked files plus untracked, non-ignored files.
Fails if any path maps only to UNKNOWN.
USAGE
}

fail() {
    echo "check-maintainers: $*" >&2
    exit 1
}

is_generated_path() {
    path=$1

    case "$path" in
        obj/*|dist/*|tnt|tntctl|tests/*.log|tests/host_key*|tests/messages.log|tests/unit/*.o|tests/unit/test_messages.log)
            return 0
            ;;
        tests/unit/test_*)
            case "$path" in
                *.c)
                    return 1
                    ;;
            esac
            [ -x "$ROOT/$path" ] && return 0
            ;;
    esac

    return 1
}

list_repo_files() {
    if [ "${TNT_CHECK_MAINTAINERS_NO_GIT:-0}" != "1" ] &&
       command -v git >/dev/null 2>&1 &&
       git -C "$ROOT" rev-parse --is-inside-work-tree >/dev/null 2>&1; then
        git -C "$ROOT" ls-files --cached --others --exclude-standard
    else
        find "$ROOT" -type f ! -path "$ROOT/.git/*" |
            sed "s#^$ROOT/##" |
            while IFS= read -r path; do
                is_generated_path "$path" && continue
                printf '%s\n' "$path"
            done
    fi
}

check_path() {
    path=$1
    output=$("$GET_MAINTAINER" "$path")
    if printf '%s\n' "$output" |
       awk -F'\t' '$2 != "UNKNOWN" { found = 1 } END { exit found ? 0 : 1 }'; then
        return 0
    fi
    printf '%s\n' "$path"
    return 1
}

[ "${1:-}" != "-h" ] && [ "${1:-}" != "--help" ] || {
    usage
    exit 0
}

[ -x "$GET_MAINTAINER" ] || fail "missing executable: $GET_MAINTAINER"

missing=0
if [ "$#" -gt 0 ]; then
    for path in "$@"; do
        check_path "$path" || missing=1
    done
else
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        check_path "$path" >/dev/null || {
            printf '%s\n' "$path"
            missing=1
        }
    done <<EOF
$(list_repo_files)
EOF
fi

[ "$missing" -eq 0 ] || fail "some paths are not covered by MAINTAINERS"
echo "check-maintainers: ok"
