#!/bin/sh
#
# Watches the NootRX GFX diagnostics dump published on the GFX0 registry node and
# appends every new snapshot to a log file, so evidence survives a crash and reboot.
#
# Usage:
#   Scripts/WatchGfxDiag.sh [--out-dir DIR] [--interval SECONDS] [--once]
#
# Defaults:
#   out-dir   ~/Desktop/NootRX-diagnostics
#   interval  20
#   node      GFX0 (override with NOOTRX_GFX_NODE)
#
set -eu

script_dir="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"

out_dir="$HOME/Desktop/NootRX-diagnostics"
interval=20
once=0
node="${NOOTRX_GFX_NODE:-GFX0}"

# Prints the accepted command line so the script can be used without reading the header.
usage() { sed -n '3,12p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --out-dir) out_dir="$2"; shift ;;
        --interval) interval="$2"; shift ;;
        --once) once=1 ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'FAIL: unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

mkdir -p "$out_dir" "$out_dir/reports"
out_file="$out_dir/gfxdiag.log"
reports_dir="$out_dir/reports"

# Reads the current dump sequence number; prints nothing while the property is absent.
currentSeq() {
    ioreg -n "$node" -l -w0 2>/dev/null | sed -n 's/.*"NootRX_GfxDiagDumpSeq" = \([0-9][0-9]*\).*/\1/p' | head -1
}

# Appends the full GPU node dump and archives new GPU restart and panic reports.
capture() {
    seq="$1"
    {
        printf '===== %s dump seq=%s =====\n' "$(date '+%Y-%m-%d %H:%M:%S')" "$seq"
        ioreg -n "$node" -l -w0 2>/dev/null
    } >> "$out_file"

    for report in /Library/Logs/DiagnosticReports/Kernel_*.gpuRestart /Library/Logs/DiagnosticReports/*.panic; do
        [ -f "$report" ] || continue
        base="$(basename "$report")"
        [ -f "$reports_dir/$base" ] || cp -p "$report" "$reports_dir/$base"
    done
}

last=""
while true; do
    seq="$(currentSeq)"
    if [ -n "$seq" ] && [ "$seq" != "$last" ]; then
        capture "$seq"
        last="$seq"
    fi
    [ "$once" -eq 1 ] && exit 0
    sleep "$interval"
done
