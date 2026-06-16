#!/bin/sh
# Interactive TNT setup wizard. Generates an environment file; does not install,
# restart, or enable services by itself.

set -eu

PROFILE=${TNT_SETUP_PROFILE:-}
PORT_VALUE=${TNT_SETUP_PORT:-2222}
BIND_ADDR=${TNT_SETUP_BIND_ADDR:-0.0.0.0}
STATE_DIR=${TNT_SETUP_STATE_DIR:-/var/lib/tnt}
PUBLIC_HOST=${TNT_SETUP_PUBLIC_HOST:-}
MAX_CONNECTIONS=${TNT_SETUP_MAX_CONNECTIONS:-64}
MODULE_ROOT=${TNT_SETUP_MODULE_ROOT:-/opt/tnt-modules}
MODULE_PATHS=${TNT_SETUP_MODULE_PATHS:-}
MODULE_SELECTION=${TNT_SETUP_MODULE_SELECTION:-}
OUTPUT=${TNT_SETUP_OUTPUT:-}
NON_INTERACTIVE=0
PRINT_MODULES=0

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
CHECKER="$SCRIPT_DIR/module_check.sh"
TMPDIR_ROOT=$(mktemp -d "${TMPDIR:-/tmp}/tnt-install-wizard.XXXXXX")
MODULE_INDEX="$TMPDIR_ROOT/modules.index"

cleanup() {
    rm -rf "$TMPDIR_ROOT"
}
trap cleanup EXIT INT TERM

usage() {
    cat <<'USAGE'
Usage: scripts/install_wizard.sh [--non-interactive] [--output FILE] [--module-root DIR]
       scripts/install_wizard.sh --print-modules [--module-root DIR]

Generates a TNT environment file for review. It never installs binaries,
rewrites systemd units, restarts TNT, or downloads modules.

Profiles:
  core      no modules
  all       enable every valid module under --module-root
  select    choose valid modules from --module-root
  manual    use TNT_SETUP_MODULE_PATHS as a colon-separated module path list

Non-interactive environment:
  TNT_SETUP_PROFILE=core|all|select|manual
  TNT_SETUP_PORT=2222
  TNT_SETUP_BIND_ADDR=0.0.0.0
  TNT_SETUP_STATE_DIR=/var/lib/tnt
  TNT_SETUP_PUBLIC_HOST=chat.example.com
  TNT_SETUP_MAX_CONNECTIONS=64
  TNT_SETUP_MODULE_ROOT=/opt/tnt-modules
  TNT_SETUP_MODULE_SELECTION=1,3
  TNT_SETUP_MODULE_PATHS=/opt/tnt-modules/a:/opt/tnt-modules/b
USAGE
}

fail() {
    echo "install-wizard: $*" >&2
    exit 1
}

is_uint() {
    case "${1:-}" in
        ''|*[!0-9]*)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

is_port() {
    is_uint "$1" && [ "$1" -ge 1 ] && [ "$1" -le 65535 ]
}

safe_scalar() {
    case "${1:-}" in
        *[!A-Za-z0-9._:/@-]*)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

safe_optional_scalar() {
    [ -z "${1:-}" ] || safe_scalar "$1"
}

safe_module_path() {
    case "${1:-}" in
        ''|*:*)
            return 1
            ;;
        *[!A-Za-z0-9._/@+-]*)
            return 1
            ;;
        *)
            return 0
            ;;
    esac
}

shell_quote() {
    printf "'"
    printf '%s' "$1" | sed "s/'/'\\\\''/g"
    printf "'"
}

json_string_field() {
    key=$1
    file=$2
    sed -n "s/.*\"$key\"[[:space:]]*:[[:space:]]*\"\\([^\"]*\\)\".*/\\1/p" "$file" |
        head -n 1
}

prompt_value() {
    label=$1
    default=$2
    printf '%s [%s]: ' "$label" "$default" >&2
    IFS= read -r answer || answer=
    [ -n "$answer" ] || answer=$default
    printf '%s\n' "$answer"
}

prompt_optional() {
    label=$1
    default=$2
    if [ -n "$default" ]; then
        printf '%s [%s]: ' "$label" "$default" >&2
    else
        printf '%s: ' "$label" >&2
    fi
    IFS= read -r answer || answer=
    [ -n "$answer" ] || answer=$default
    printf '%s\n' "$answer"
}

print_profiles() {
    cat >&2 <<'PROFILES'

Deployment profile:
  1) core    - run TNT without modules
  2) all     - enable every valid module under the module root
  3) select  - choose valid modules from the module root
  4) manual  - enter a colon-separated module path list
PROFILES
}

profile_from_choice() {
    case "$1" in
        1|core) printf 'core\n' ;;
        2|all) printf 'all\n' ;;
        3|select) printf 'select\n' ;;
        4|manual) printf 'manual\n' ;;
        *) fail "unknown profile: $1" ;;
    esac
}

scan_modules() {
    : > "$MODULE_INDEX"
    [ -d "$MODULE_ROOT" ] || return 0

    find "$MODULE_ROOT" -mindepth 1 -maxdepth 1 -type d | sort |
    while IFS= read -r module_dir; do
        [ -f "$module_dir/tnt-module.json" ] || continue
        safe_module_path "$module_dir" || continue
        name=$(json_string_field name "$module_dir/tnt-module.json")
        [ -n "$name" ] || name=$(basename "$module_dir")
        index=$(($(wc -l < "$MODULE_INDEX" | tr -d ' ') + 1))
        if [ -x "$CHECKER" ] && "$CHECKER" "$module_dir" >/dev/null 2>&1; then
            status=ok
        else
            status=invalid
        fi
        printf '%s|%s|%s|%s\n' "$index" "$name" "$module_dir" "$status" >> "$MODULE_INDEX"
    done
}

print_modules() {
    if [ ! -s "$MODULE_INDEX" ]; then
        echo "No modules found under $MODULE_ROOT" >&2
        return
    fi

    echo "" >&2
    echo "Modules under $MODULE_ROOT:" >&2
    while IFS='|' read -r index name path status; do
        printf '  %s) [%s] %s %s\n' "$index" "$status" "$name" "$path" >&2
    done < "$MODULE_INDEX"
}

print_modules_stdout() {
    if [ ! -s "$MODULE_INDEX" ]; then
        return 0
    fi

    while IFS='|' read -r index name path status; do
        printf '%s\t%s\t%s\t%s\n' "$index" "$status" "$name" "$path"
    done < "$MODULE_INDEX"
}

join_path_file() {
    result=
    while IFS= read -r path; do
        [ -n "$path" ] || continue
        if [ -z "$result" ]; then
            result=$path
        else
            result="$result:$path"
        fi
    done < "$1"
    printf '%s\n' "$result"
}

valid_module_paths_from_scan() {
    paths="$TMPDIR_ROOT/valid.paths"
    awk -F'|' '$4 == "ok" { print $3 }' "$MODULE_INDEX" > "$paths"
    join_path_file "$paths"
}

selected_module_paths_from_scan() {
    selection=$1
    selected="$TMPDIR_ROOT/selected.paths"
    : > "$selected"

    [ -n "$selection" ] || fail "no modules selected"
    for item in $(printf '%s\n' "$selection" | tr ',' ' '); do
        is_uint "$item" || fail "invalid module selection: $item"
        line=$(awk -F'|' -v n="$item" '$1 == n { print; exit }' "$MODULE_INDEX")
        [ -n "$line" ] || fail "module selection not found: $item"
        status=$(printf '%s\n' "$line" | awk -F'|' '{ print $4 }')
        [ "$status" = "ok" ] || fail "selected module is invalid: $item"
        printf '%s\n' "$line" | awk -F'|' '{ print $3 }' >> "$selected"
    done

    join_path_file "$selected"
}

validate_module_path_list() {
    list=$1
    [ -n "$list" ] || return 0

    printf '%s\n' "$list" | tr ':' '\n' |
    while IFS= read -r module_dir; do
        [ -n "$module_dir" ] || continue
        safe_module_path "$module_dir" ||
            fail "unsafe module path: $module_dir"
        [ -d "$module_dir" ] ||
            fail "module directory does not exist: $module_dir"
        [ -x "$CHECKER" ] ||
            fail "module checker is missing or not executable: $CHECKER"
        "$CHECKER" "$module_dir" >/dev/null ||
            fail "module failed validation: $module_dir"
    done
}

write_config() {
    destination=$1
    {
        echo "# Generated by scripts/install_wizard.sh"
        echo "# Review before installing as /etc/default/tnt or a systemd EnvironmentFile."
        printf 'PORT=%s\n' "$(shell_quote "$PORT_VALUE")"
        printf 'TNT_BIND_ADDR=%s\n' "$(shell_quote "$BIND_ADDR")"
        printf 'TNT_STATE_DIR=%s\n' "$(shell_quote "$STATE_DIR")"
        printf 'TNT_MAX_CONNECTIONS=%s\n' "$(shell_quote "$MAX_CONNECTIONS")"
        if [ -n "$PUBLIC_HOST" ]; then
            printf 'TNT_PUBLIC_HOST=%s\n' "$(shell_quote "$PUBLIC_HOST")"
        fi
        if [ -n "$MODULE_PATHS" ]; then
            printf 'TNT_MODULE_PATHS=%s\n' "$(shell_quote "$MODULE_PATHS")"
        else
            echo "# TNT_MODULE_PATHS intentionally unset: core-only deployment."
        fi
    } > "$destination"
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --non-interactive)
            NON_INTERACTIVE=1
            shift
            ;;
        --output)
            [ "$#" -ge 2 ] || fail "missing value for --output"
            OUTPUT=$2
            shift 2
            ;;
        --module-root)
            [ "$#" -ge 2 ] || fail "missing value for --module-root"
            MODULE_ROOT=$2
            shift 2
            ;;
        --print-modules)
            PRINT_MODULES=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            fail "unknown option: $1"
            ;;
    esac
done

if [ "$PRINT_MODULES" -eq 1 ]; then
    safe_scalar "$MODULE_ROOT" || fail "unsafe module root: $MODULE_ROOT"
    scan_modules
    print_modules_stdout
    exit 0
fi

if [ "$NON_INTERACTIVE" -eq 0 ]; then
    echo "=== TNT Setup Wizard ===" >&2
    print_profiles
    choice=$(prompt_value "Choose profile" "${PROFILE:-core}")
    PROFILE=$(profile_from_choice "$choice")
    PORT_VALUE=$(prompt_value "Port" "$PORT_VALUE")
    BIND_ADDR=$(prompt_value "Bind address" "$BIND_ADDR")
    STATE_DIR=$(prompt_value "State directory" "$STATE_DIR")
    PUBLIC_HOST=$(prompt_optional "Public host" "$PUBLIC_HOST")
    MAX_CONNECTIONS=$(prompt_value "Max connections" "$MAX_CONNECTIONS")
    case "$PROFILE" in
        all|select)
            MODULE_ROOT=$(prompt_value "Module root" "$MODULE_ROOT")
            ;;
        manual)
            MODULE_PATHS=$(prompt_optional "Module paths" "$MODULE_PATHS")
            ;;
    esac
else
    [ -n "$PROFILE" ] || PROFILE=core
    PROFILE=$(profile_from_choice "$PROFILE")
fi

is_port "$PORT_VALUE" || fail "invalid port: $PORT_VALUE"
safe_scalar "$BIND_ADDR" || fail "unsafe bind address: $BIND_ADDR"
safe_scalar "$STATE_DIR" || fail "unsafe state directory: $STATE_DIR"
safe_optional_scalar "$PUBLIC_HOST" || fail "unsafe public host: $PUBLIC_HOST"
is_uint "$MAX_CONNECTIONS" && [ "$MAX_CONNECTIONS" -gt 0 ] ||
    fail "invalid max connections: $MAX_CONNECTIONS"

case "$PROFILE" in
    core)
        MODULE_PATHS=
        ;;
    all)
        safe_scalar "$MODULE_ROOT" || fail "unsafe module root: $MODULE_ROOT"
        scan_modules
        [ "$NON_INTERACTIVE" -eq 1 ] || print_modules
        MODULE_PATHS=$(valid_module_paths_from_scan)
        ;;
    select)
        safe_scalar "$MODULE_ROOT" || fail "unsafe module root: $MODULE_ROOT"
        scan_modules
        print_modules
        if [ "$NON_INTERACTIVE" -eq 0 ]; then
            MODULE_SELECTION=$(prompt_optional "Select module numbers, comma-separated" "$MODULE_SELECTION")
        fi
        MODULE_PATHS=$(selected_module_paths_from_scan "$MODULE_SELECTION")
        ;;
    manual)
        validate_module_path_list "$MODULE_PATHS"
        ;;
esac

validate_module_path_list "$MODULE_PATHS"

if [ -n "$OUTPUT" ]; then
    tmp="$OUTPUT.tmp.$$"
    write_config "$tmp"
    mv "$tmp" "$OUTPUT"
    echo "install-wizard: wrote $OUTPUT" >&2
else
    write_config /dev/stdout
fi

cat >&2 <<'NEXT'

Next steps:
  1. Review the generated environment file.
  2. Install it manually, for example: sudo install -m 600 FILE /etc/default/tnt
  3. Restart TNT manually when ready: sudo systemctl restart tnt
  4. Roll back modules by removing TNT_MODULE_PATHS and restarting TNT.
NEXT
