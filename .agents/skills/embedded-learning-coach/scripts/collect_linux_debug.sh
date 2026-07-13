#!/usr/bin/env bash
set -euo pipefail

OUT_DIR="${1:-debug-$(date +%Y%m%d-%H%M%S)}"
mkdir -p "$OUT_DIR"

run_capture() {
  local name="$1"
  shift
  {
    echo "\$ $*"
    "$@"
  } >"$OUT_DIR/$name.txt" 2>&1 || true
}

run_shell_capture() {
  local name="$1"
  shift
  {
    echo "\$ $*"
    bash -lc "$*"
  } >"$OUT_DIR/$name.txt" 2>&1 || true
}

run_capture date date
run_capture uname uname -a
run_capture uptime uptime
run_capture ps ps aux
run_capture memory free -h
run_capture vmstat vmstat 1 5
run_capture ip_addr ip addr
run_capture ip_route ip route
run_capture sockets ss -lntup
run_shell_capture dmesg "dmesg | tail -n 500"
run_shell_capture journal "journalctl -b --no-pager | tail -n 500"
run_shell_capture interrupts "cat /proc/interrupts"
run_shell_capture modules "cat /proc/modules"
run_shell_capture meminfo "cat /proc/meminfo"

cat >"$OUT_DIR/README.txt" <<'EOF'
This bundle contains diagnostic snapshots only.
Review files before sharing because process lists, routes, usernames,
device names, and logs may contain sensitive information.
EOF

if command -v tar >/dev/null 2>&1; then
  tar czf "${OUT_DIR}.tar.gz" "$OUT_DIR"
  echo "Saved ${OUT_DIR}.tar.gz"
else
  echo "Saved directory $OUT_DIR"
fi
