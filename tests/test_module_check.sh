#!/bin/sh
# Regression tests for scripts/module_check.sh.

set -eu

PASS=0
FAIL=0
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
STATE_DIR=$(mktemp -d "${TMPDIR:-/tmp}/tnt-module-check-test.XXXXXX")

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

echo "=== TNT Module Check Tests ==="

valid_dir="$STATE_DIR/valid"
write_module "$valid_dir" "echo-module"
if "$ROOT/scripts/module_check.sh" "$valid_dir" >/dev/null; then
    pass "valid module passes"
else
    fail_case "valid module passes"
fi

plain_entry_dir="$STATE_DIR/plain-entry"
write_module "$plain_entry_dir" "plain-entry"
sed 's#"entrypoint": "./module.sh"#"entrypoint": "module.sh"#' \
    "$plain_entry_dir/tnt-module.json" >"$plain_entry_dir/tnt-module.json.tmp"
mv "$plain_entry_dir/tnt-module.json.tmp" "$plain_entry_dir/tnt-module.json"
if "$ROOT/scripts/module_check.sh" "$plain_entry_dir" >/dev/null; then
    pass "relative entrypoint without slash passes"
else
    fail_case "relative entrypoint without slash passes"
fi

compatible_dir="$STATE_DIR/compatible"
write_module "$compatible_dir" "compatible-module" "1.0.1"
if "$ROOT/scripts/module_check.sh" --tnt-version 1.0.1 "$compatible_dir" >/dev/null; then
    pass "compatible TNT minimum version passes"
else
    fail_case "compatible TNT minimum version passes"
fi

future_dir="$STATE_DIR/future"
write_module "$future_dir" "future-module" "9.0.0"
if "$ROOT/scripts/module_check.sh" --tnt-version 1.0.1 "$future_dir" >/dev/null 2>&1; then
    fail_case "future TNT minimum version is rejected"
else
    pass "future TNT minimum version is rejected"
fi

bad_name_dir="$STATE_DIR/bad-name"
write_module "$bad_name_dir" "Echo_Module"
if "$ROOT/scripts/module_check.sh" "$bad_name_dir" >/dev/null 2>&1; then
    fail_case "invalid module name is rejected"
else
    pass "invalid module name is rejected"
fi

long_name_dir="$STATE_DIR/long-name"
write_module "$long_name_dir" "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"
if "$ROOT/scripts/module_check.sh" "$long_name_dir" >/dev/null 2>&1; then
    fail_case "overlong module name is rejected"
else
    pass "overlong module name is rejected"
fi

bad_protocol_dir="$STATE_DIR/bad-protocol"
write_module "$bad_protocol_dir" "bad-protocol"
sed 's/tnt.module.v1/tnt.module.v2/' "$bad_protocol_dir/tnt-module.json" \
    >"$bad_protocol_dir/tnt-module.json.tmp"
mv "$bad_protocol_dir/tnt-module.json.tmp" "$bad_protocol_dir/tnt-module.json"
if "$ROOT/scripts/module_check.sh" "$bad_protocol_dir" >/dev/null 2>&1; then
    fail_case "wrong protocol is rejected"
else
    pass "wrong protocol is rejected"
fi

bad_handshake_dir="$STATE_DIR/bad-handshake"
write_module "$bad_handshake_dir" "bad-handshake"
cat >"$bad_handshake_dir/module.sh" <<'SH'
#!/bin/sh
printf '%s\n' '{"type":"event.ok"}'
SH
chmod +x "$bad_handshake_dir/module.sh"
if "$ROOT/scripts/module_check.sh" "$bad_handshake_dir" >/dev/null 2>&1; then
    fail_case "bad handshake is rejected"
else
    pass "bad handshake is rejected"
fi

silent_dir="$STATE_DIR/silent"
write_module "$silent_dir" "silent-module"
cat >"$silent_dir/module.sh" <<'SH'
#!/bin/sh
exit 0
SH
chmod +x "$silent_dir/module.sh"
if "$ROOT/scripts/module_check.sh" "$silent_dir" >/dev/null 2>&1; then
    fail_case "silent module is rejected"
else
    pass "silent module is rejected"
fi

printf '\nPASSED: %d\nFAILED: %d\n' "$PASS" "$FAIL"
[ "$FAIL" -eq 0 ]
