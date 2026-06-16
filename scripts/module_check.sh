#!/bin/sh
# Validate a TNT external-process module directory without starting TNT.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
TNT_VERSION_OVERRIDE=${TNT_MODULE_CHECK_TNT_VERSION:-}

fail() {
    echo "module-check: $*" >&2
    exit 1
}

usage() {
    cat <<'USAGE'
Usage: scripts/module_check.sh [--tnt-version VERSION] MODULE_DIR

Checks:
  - tnt-module.json exists and declares tnt.module.v1
  - optional tnt_min_version is compatible with the target TNT version
  - module name follows a-z, 0-9, and '-' rules, max 56 chars
  - entrypoint is safe, relative, and executable
  - required message read/create permissions and message.created event exist
  - entrypoint answers the TNT JSONL handshake with handshake.ok
USAGE
}

json_string_field() {
    key=$1
    file=$2
    sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p" "$file" |
        head -n 1
}

normalize_version() {
    version=${1#v}
    case "$version" in
        ''|*[!0-9.]*|.*|*.)
            return 1
            ;;
    esac
    printf '%s\n' "$version"
}

version_ge() {
    current=$(normalize_version "$1") || return 1
    required=$(normalize_version "$2") || return 1

    current_major=$(printf '%s\n' "$current" | awk -F. '{ print $1 + 0 }')
    current_minor=$(printf '%s\n' "$current" | awk -F. '{ print $2 + 0 }')
    current_patch=$(printf '%s\n' "$current" | awk -F. '{ print $3 + 0 }')
    required_major=$(printf '%s\n' "$required" | awk -F. '{ print $1 + 0 }')
    required_minor=$(printf '%s\n' "$required" | awk -F. '{ print $2 + 0 }')
    required_patch=$(printf '%s\n' "$required" | awk -F. '{ print $3 + 0 }')

    [ "$current_major" -gt "$required_major" ] && return 0
    [ "$current_major" -lt "$required_major" ] && return 1
    [ "$current_minor" -gt "$required_minor" ] && return 0
    [ "$current_minor" -lt "$required_minor" ] && return 1
    [ "$current_patch" -ge "$required_patch" ]
}

target_tnt_version() {
    if [ -n "$TNT_VERSION_OVERRIDE" ]; then
        normalize_version "$TNT_VERSION_OVERRIDE" || fail "invalid TNT version: $TNT_VERSION_OVERRIDE"
        return
    fi

    version_file="$SCRIPT_DIR/../include/common.h"
    if [ -f "$version_file" ]; then
        version=$(sed -n 's/^#define TNT_VERSION "\([^"]*\)".*/\1/p' "$version_file" | head -n 1)
        if [ -n "$version" ]; then
            normalize_version "$version" || fail "invalid TNT version: $version"
            return
        fi
    fi

    printf '0.0.0\n'
}

valid_module_name() {
    [ "${#1}" -le 56 ] || return 1
    printf '%s\n' "$1" |
        awk '/^[a-z0-9]([a-z0-9-]*[a-z0-9])?$/ { ok = 1 } END { exit ok ? 0 : 1 }'
}

safe_entrypoint() {
    printf '%s\n' "$1" |
        awk '
            length($0) == 0 { exit 1 }
            substr($0, 1, 1) == "/" { exit 1 }
            index($0, "..") > 0 { exit 1 }
            /[[:space:][:cntrl:]\|;&`$<>\\]/ { exit 1 }
            { exit 0 }
        '
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --tnt-version)
            [ "$#" -ge 2 ] || fail "missing value for --tnt-version"
            TNT_VERSION_OVERRIDE=$2
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        --)
            shift
            break
            ;;
        -*)
            fail "unknown option: $1"
            ;;
        *)
            break
            ;;
    esac
done

[ "$#" -le 1 ] || fail "too many arguments"

module_dir=${1:-}
[ -n "$module_dir" ] || {
    usage >&2
    exit 2
}

[ -d "$module_dir" ] || fail "module directory does not exist: $module_dir"
manifest="$module_dir/tnt-module.json"
[ -f "$manifest" ] || fail "missing manifest: $manifest"

protocol=$(json_string_field protocol "$manifest")
name=$(json_string_field name "$manifest")
entrypoint=$(json_string_field entrypoint "$manifest")
tnt_min_version=$(json_string_field tnt_min_version "$manifest")
tnt_version=$(target_tnt_version)

[ "$protocol" = "tnt.module.v1" ] ||
    fail "unsupported protocol: ${protocol:-missing}"
[ -z "$tnt_min_version" ] ||
    version_ge "$tnt_version" "$tnt_min_version" ||
    fail "module requires TNT >= $tnt_min_version, target is $tnt_version"
valid_module_name "$name" ||
    fail "invalid module name: ${name:-missing}"
safe_entrypoint "$entrypoint" ||
    fail "unsafe entrypoint: ${entrypoint:-missing}"

grep -q '"message:read"' "$manifest" ||
    fail "missing permission: message:read"
grep -q '"message:create"' "$manifest" ||
    fail "missing permission: message:create"
grep -q '"message.created"' "$manifest" ||
    fail "missing event: message.created"

entry_path="$module_dir/$entrypoint"
[ -f "$entry_path" ] || fail "entrypoint does not exist: $entry_path"
[ -x "$entry_path" ] || fail "entrypoint is not executable: $entry_path"
case "$entrypoint" in
    */*) entry_run=$entrypoint ;;
    *) entry_run="./$entrypoint" ;;
esac

tmpdir=$(mktemp -d "${TMPDIR:-/tmp}/tnt-module-check.XXXXXX")
cleanup() {
    [ -z "${writer_pid:-}" ] || kill "$writer_pid" 2>/dev/null || true
    [ -z "${module_pid:-}" ] || kill "$module_pid" 2>/dev/null || true
    rm -rf "$tmpdir"
}
trap cleanup EXIT INT TERM

in_pipe="$tmpdir/stdin"
out_file="$tmpdir/stdout"
err_file="$tmpdir/stderr"
mkfifo "$in_pipe"

(
    cd "$module_dir"
    "$entry_run" <"$in_pipe" >"$out_file" 2>"$err_file"
) &
module_pid=$!

printf '%s\n' "{\"type\":\"handshake\",\"protocol\":\"tnt.module.v1\",\"server\":{\"name\":\"tnt\",\"version\":\"$tnt_version\"}}" >"$in_pipe" &
writer_pid=$!

i=0
while [ "$i" -lt 20 ]; do
    if [ -s "$out_file" ]; then
        break
    fi
    if ! kill -0 "$module_pid" 2>/dev/null; then
        break
    fi
    i=$((i + 1))
    sleep 0.1
done

line=$(sed -n '1p' "$out_file" 2>/dev/null || true)
printf '%s\n' "$line" | grep -q '"type"[[:space:]]*:[[:space:]]*"handshake.ok"' ||
    fail "entrypoint did not return handshake.ok"
printf '%s\n' "$line" | grep -q '"protocol"[[:space:]]*:[[:space:]]*"tnt.module.v1"' ||
    fail "entrypoint handshake used the wrong protocol"

echo "module-check: ok $name"
