#!/bin/bash
# Install TNT maintenance scripts and cron jobs without crossing the service
# account's writable state-directory boundary.

set -euo pipefail

fail() {
    echo "setup-cron: $*" >&2
    exit 1
}

usage() {
    cat <<'EOF'
Usage: scripts/setup_cron.sh

Environment:
  DESTDIR        Stage files below this directory instead of installing live
EOF
}

if [[ $# -ne 0 ]]; then
    usage >&2
    exit 64
fi

DESTDIR=${DESTDIR:-}
TNT_USER=tnt
TNT_GROUP=tnt

if [[ -n "$DESTDIR" ]]; then
    [[ "$DESTDIR" == /* && "$DESTDIR" != / ]] ||
        fail "DESTDIR must be an absolute staging directory other than /"
    DESTDIR=${DESTDIR%/}
fi
LIVE_INSTALL=0
if [[ -z "$DESTDIR" ]]; then
    LIVE_INSTALL=1
    [[ $(id -u) -eq 0 ]] || fail "live installation must run as root"
    id "$TNT_USER" >/dev/null 2>&1 || fail "missing service user: $TNT_USER"
    id -Gn "$TNT_USER" | tr ' ' '\n' | grep -qx "$TNT_GROUP" ||
        fail "service user $TNT_USER is not a member of group $TNT_GROUP"
fi

SCRIPT_DIR=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
LIBEXEC_DIR=/usr/local/libexec/tnt
STATE_DIR=/var/lib/tnt
PORT=2222
CRON_FILE=/etc/cron.d/tnt
LOGROTATE_LOG=/var/log/tnt-logrotate.log
HEALTH_LOG=/var/log/tnt-health.log

INSTALL_LIBEXEC_DIR="$DESTDIR$LIBEXEC_DIR"
INSTALL_CRON_DIR="$DESTDIR/etc/cron.d"
INSTALL_CRON_FILE="$DESTDIR$CRON_FILE"
INSTALL_LOG_DIR="$DESTDIR/var/log"

install_owned() {
    if [[ "$LIVE_INSTALL" -eq 1 ]]; then
        install -o root -g root "$@"
    else
        install "$@"
    fi
}

prepare_log_file() {
    local path=$1

    if [[ -L "$path" || ( -e "$path" && ! -f "$path" ) ]]; then
        fail "refusing non-regular maintenance log: $path"
    fi
    if [[ ! -e "$path" ]]; then
        install -m 0660 /dev/null "$path"
    else
        chmod 0660 "$path"
    fi
    if [[ "$LIVE_INSTALL" -eq 1 ]]; then
        chown root:"$TNT_GROUP" "$path"
    fi
}

echo "Setting up TNT maintenance cron jobs..."

# Executable code lives outside /var/lib/tnt, which is writable by the service
# account.  The cron jobs also run as that unprivileged account so paths derived
# from messages.log can never be used to make root follow attacker-made links.
install_owned -d -m 0755 "$INSTALL_LIBEXEC_DIR"
install_owned -m 0755 "$SCRIPT_DIR/logrotate.sh" \
    "$INSTALL_LIBEXEC_DIR/logrotate.sh"
install_owned -m 0755 "$SCRIPT_DIR/healthcheck.sh" \
    "$INSTALL_LIBEXEC_DIR/healthcheck.sh"

if [[ "$LIVE_INSTALL" -eq 0 ]]; then
    install -d -m 0755 "$INSTALL_LOG_DIR" "$INSTALL_CRON_DIR"
else
    [[ -d "$INSTALL_LOG_DIR" ]] || fail "missing log directory: $INSTALL_LOG_DIR"
    [[ -d "$INSTALL_CRON_DIR" ]] || fail "missing cron directory: $INSTALL_CRON_DIR"
fi
prepare_log_file "$DESTDIR$LOGROTATE_LOG"
prepare_log_file "$DESTDIR$HEALTH_LOG"

# Build the replacement beside its destination, then rename it into place so
# cron never observes a partially written configuration.
CRON_TMP=$(mktemp "$INSTALL_CRON_DIR/.tnt.XXXXXX")
cleanup() {
    [[ -z "${CRON_TMP:-}" ]] || rm -f -- "$CRON_TMP"
}
trap cleanup EXIT INT TERM

cat > "$CRON_TMP" <<EOF
# TNT Chat Server Maintenance Tasks
SHELL=/bin/sh
PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

# Log rotation - daily at 3 AM
0 3 * * * $TNT_USER $LIBEXEC_DIR/logrotate.sh $STATE_DIR/messages.log 100 10000 >> $LOGROTATE_LOG 2>&1

# Health check - every 5 minutes
*/5 * * * * $TNT_USER $LIBEXEC_DIR/healthcheck.sh $PORT 5 >> $HEALTH_LOG 2>&1
EOF

chmod 0644 "$CRON_TMP"
if [[ "$LIVE_INSTALL" -eq 1 ]]; then
    chown root:root "$CRON_TMP"
fi
mv -f -- "$CRON_TMP" "$INSTALL_CRON_FILE"
CRON_TMP=

echo "Cron jobs installed:"
cat "$INSTALL_CRON_FILE"
echo
echo "Done! Maintenance tasks will run as $TNT_USER."
echo "- Scripts: $LIBEXEC_DIR"
echo "- Log rotation: Daily at 3 AM"
echo "- Health check: Every 5 minutes"
