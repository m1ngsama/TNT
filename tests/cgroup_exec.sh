#!/bin/sh

set -eu

[ "$#" -ge 2 ] || exit 64
group=$1
shift
case "$group" in
    /sys/fs/cgroup/*) ;;
    *) exit 64 ;;
esac
[ -f "$group/cgroup.procs" ] || exit 1
if ! printf '%s\n' "$$" >"$group/cgroup.procs" 2>/dev/null; then
    command -v sudo >/dev/null 2>&1 || exit 1
    printf '%s\n' "$$" | sudo -n tee "$group/cgroup.procs" >/dev/null
fi
exec "$@"
