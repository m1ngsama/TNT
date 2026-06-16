#!/bin/sh
# Manual regression tests for scripts/install_wizard.sh.

set -u

PASS=0
FAIL=0
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-install-wizard-test.XXXXXX")

cleanup() {
    rm -rf "$STATE_DIR"
}
trap cleanup EXIT

pass() {
    echo "PASS $1"
    PASS=$((PASS + 1))
}

fail_case() {
    echo "FAIL $1"
    FAIL=$((FAIL + 1))
}

write_module() {
    dir=$1
    name=$2
    min_version=${3:-}
    mkdir -p "$dir"
    min_line=
    [ -z "$min_version" ] || min_line="  \"tnt_min_version\": \"$min_version\","
    cat >"$dir/tnt-module.json" <<JSON
{
  "protocol": "tnt.module.v1",
  "name": "$name",
  "version": "0.1.0",
${min_line}
  "entrypoint": "./module.sh",
  "permissions": ["message:read", "message:create"],
  "events": ["message.created"]
}
JSON
    cat >"$dir/module.sh" <<'SH'
#!/bin/sh
while IFS= read -r line; do
  case "$line" in
    *'"type":"handshake"'*|*'"type": "handshake"'*)
      printf '%s\n' '{"type":"handshake.ok","protocol":"tnt.module.v1","module":{"name":"test","version":"0.1.0"}}'
      ;;
    *)
      printf '%s\n' '{"type":"event.ok"}'
      ;;
  esac
done
SH
    chmod +x "$dir/module.sh"
}

echo "=== TNT Install Wizard Tests ==="

out="$STATE_DIR/core.env"
TNT_SETUP_PROFILE=core \
TNT_SETUP_PORT=3333 \
TNT_SETUP_BIND_ADDR=127.0.0.1 \
TNT_SETUP_STATE_DIR=/tmp/tnt-state \
TNT_SETUP_MAX_CONNECTIONS=12 \
    "$ROOT/scripts/install_wizard.sh" --non-interactive --output "$out" >/dev/null 2>&1
if grep -q "^PORT='3333'$" "$out" &&
   grep -q "^TNT_BIND_ADDR='127.0.0.1'$" "$out" &&
   ! grep -q '^TNT_MODULE_PATHS=' "$out"; then
    pass "core profile writes env without modules"
else
    fail_case "core profile writes env without modules"
    cat "$out"
fi

module_root="$STATE_DIR/modules"
write_module "$module_root/echo-module" "echo-module"
write_module "$module_root/bad-module" "Bad_Module"
write_module "$module_root/future-module" "future-module" "9.0.0"

MODULES_OUTPUT=$("$ROOT/scripts/install_wizard.sh" --print-modules \
    --module-root "$module_root")
if printf '%s\n' "$MODULES_OUTPUT" |
       awk -F'\t' -v path="$module_root/echo-module" \
           '$2 == "ok" && $3 == "echo-module" && $4 == path { found = 1 } END { exit found ? 0 : 1 }' &&
   printf '%s\n' "$MODULES_OUTPUT" |
       awk -F'\t' -v path="$module_root/bad-module" \
           '$2 == "invalid" && $3 == "Bad_Module" && $4 == path { found = 1 } END { exit found ? 0 : 1 }' &&
   printf '%s\n' "$MODULES_OUTPUT" |
       awk -F'\t' -v path="$module_root/future-module" \
           '$2 == "invalid" && $3 == "future-module" && $4 == path { found = 1 } END { exit found ? 0 : 1 }'; then
    pass "module scan prints compatibility status"
else
    fail_case "module scan prints compatibility status"
    printf '%s\n' "$MODULES_OUTPUT"
fi

out="$STATE_DIR/all.env"
TNT_SETUP_PROFILE=all \
TNT_SETUP_MODULE_ROOT="$module_root" \
    "$ROOT/scripts/install_wizard.sh" --non-interactive --output "$out" >/dev/null 2>&1
if grep -q "^TNT_MODULE_PATHS='$module_root/echo-module'$" "$out" &&
   ! grep -q 'bad-module' "$out" &&
   ! grep -q 'future-module' "$out"; then
    pass "all profile enables only compatible valid modules"
else
    fail_case "all profile enables only compatible valid modules"
    cat "$out"
fi

out="$STATE_DIR/manual.env"
TNT_SETUP_PROFILE=manual \
TNT_SETUP_MODULE_PATHS="$module_root/echo-module" \
    "$ROOT/scripts/install_wizard.sh" --non-interactive --output "$out" >/dev/null 2>&1
if grep -q "^TNT_MODULE_PATHS='$module_root/echo-module'$" "$out"; then
    pass "manual profile validates explicit module paths"
else
    fail_case "manual profile validates explicit module paths"
    cat "$out"
fi

out="$STATE_DIR/select.env"
if TNT_SETUP_PROFILE=select \
   TNT_SETUP_MODULE_ROOT="$module_root" \
   TNT_SETUP_MODULE_SELECTION=1 \
       "$ROOT/scripts/install_wizard.sh" --non-interactive --output "$out" >/dev/null 2>&1; then
    fail_case "select profile rejects invalid selected modules"
else
    pass "select profile rejects invalid selected modules"
fi

if TNT_SETUP_PROFILE=core TNT_SETUP_PORT=70000 \
    "$ROOT/scripts/install_wizard.sh" --non-interactive --output "$STATE_DIR/bad.env" >/dev/null 2>&1; then
    fail_case "invalid port is rejected"
else
    pass "invalid port is rejected"
fi

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
