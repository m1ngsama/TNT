#!/bin/sh
# Print MAINTAINERS sections matching one or more repository paths.

set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
MAINTAINERS_FILE=${TNT_MAINTAINERS_FILE:-"$ROOT/MAINTAINERS"}

usage() {
    cat <<'USAGE'
Usage: scripts/get_maintainer.sh PATH [...]

Output columns:
  path<TAB>section<TAB>status<TAB>matched-pattern
USAGE
}

fail() {
    echo "get-maintainer: $*" >&2
    exit 1
}

normalize_path() {
    path=$1
    case "$path" in
        "$ROOT"/*)
            path=${path#"$ROOT"/}
            ;;
        ./*)
            path=${path#./}
            ;;
    esac
    printf '%s\n' "$path"
}

emit_matches() {
    target=$1
    section=
    status=
    matched=0

    while IFS= read -r line || [ -n "$line" ]; do
        case "$line" in
            ''|'#'*)
                continue
                ;;
            'S: '*)
                status=${line#S: }
                ;;
            'F: '*)
                pattern=${line#F: }
                case "$target" in
                    $pattern)
                        printf '%s\t%s\t%s\t%s\n' "$target" "$section" "$status" "$pattern"
                        matched=1
                        ;;
                esac
                ;;
            [A-Z]*)
                section=$line
                status=
                ;;
        esac
    done < "$MAINTAINERS_FILE"

    [ "$matched" -eq 1 ] || printf '%s\t%s\t%s\t%s\n' "$target" UNKNOWN unknown -
}

[ "${1:-}" != "-h" ] && [ "${1:-}" != "--help" ] || {
    usage
    exit 0
}

[ "$#" -gt 0 ] || {
    usage >&2
    exit 64
}

[ -f "$MAINTAINERS_FILE" ] || fail "missing MAINTAINERS file: $MAINTAINERS_FILE"

for path in "$@"; do
    emit_matches "$(normalize_path "$path")"
done
