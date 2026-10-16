#!/bin/sh
#
# GPU hang reproduction harness for the RX 6750 XT diagnostics build.
#
# Keeps heavy WebGL/WebGPU pages alive in a dedicated Chrome window, cycles the
# display between off and on, switches tabs to churn compositing, and alerts as
# soon as a new GPU restart/panic report appears. It never changes system
# configuration and can be stopped with Ctrl-C at any time.
#
# Usage:
#   Scripts/ReproGpuHang.sh [--rounds N] [--sleep-seconds S] [--awake-seconds S]
#                           [--tab-seconds S] [--no-display] [--no-chrome]
#
# Defaults: rounds 20, sleep 60, awake 60, tab switch 30.
#
set -eu

rounds=20
sleep_seconds=60
awake_seconds=60
tab_seconds=30
do_display=1
do_chrome=1

# Prints the accepted command line so the script can be used without reading the header.
usage() { sed -n '3,14p' "$0" | sed 's/^# \{0,1\}//'; }

while [ $# -gt 0 ]; do
    case "$1" in
        --rounds) rounds="$2"; shift ;;
        --sleep-seconds) sleep_seconds="$2"; shift ;;
        --awake-seconds) awake_seconds="$2"; shift ;;
        --tab-seconds) tab_seconds="$2"; shift ;;
        --no-display) do_display=0 ;;
        --no-chrome) do_chrome=0 ;;
        -h|--help) usage; exit 0 ;;
        *) printf 'FAIL: unknown argument: %s\n' "$1" >&2; exit 2 ;;
    esac
    shift
done

reports_dir="/Library/Logs/DiagnosticReports"
state_dir="${TMPDIR:-/tmp}/nootrx-repro"
seen_reports="$state_dir/seen-reports.txt"
action_log="$state_dir/actions.log"
mkdir -p "$state_dir"
touch "$seen_reports"

# Seeds the known-report list so only reports created from now on count as new.
seed_reports() {
    for report in "$reports_dir"/Kernel_*.gpuRestart "$reports_dir"/*.panic; do
        [ -f "$report" ] || continue
        printf '%s\n' "$report" >> "$seen_reports"
    done
    sort -u "$seen_reports" -o "$seen_reports"
}

# Announces one failure loudly, once per artifact.
alert() {
    printf '\n!!! %s\n' "$1"
    if command -v say >/dev/null 2>&1; then say "GPU reset detected" >/dev/null 2>&1 & fi
}

# Checks for new restart or panic reports; prints them and returns 1 when found.
check_failure() {
    found=0
    for report in "$reports_dir"/Kernel_*.gpuRestart "$reports_dir"/*.panic; do
        [ -f "$report" ] || continue
        if ! grep -Fqx "$report" "$seen_reports"; then
            printf '%s\n' "$report" >> "$seen_reports"
            alert "new GPU failure report: $report"
            found=1
        fi
    done
    [ "$found" -eq 0 ]
}

# Prints the diagnostics counters that show whether the recorder is still armed.
print_state() {
    counters=$(ioreg -n GFX0 -l -w0 2>/dev/null | grep -E '"NootRX_GfxDiag(Enabled|DumpSeq)"' \
        | sed 's/^[^"]*//; s/ *$//' | tr '\n' ' ')
    printf '[%s] round %s/%s  %s\n' "$(date '+%H:%M:%S')" "$1" "$rounds" "$counters"
}

# Opens the heavy repro pages in a dedicated Chrome window.
open_pages() {
    osascript >/dev/null 2>&1 <<'EOF' || printf 'WARN: Chrome automation unavailable (tabs not opened)\n'
tell application "Google Chrome"
    if it is not running then launch
    set heavyUrls to {"https://webglsamples.org/aquarium/aquarium.html", "https://threejs.org/examples/webgl_animation_keyframes.html", "https://webgpu.github.io/webgpu-samples/?sample=animometer"}
    make new window
    set URL of active tab of front window to (item 1 of heavyUrls)
    repeat with heavyUrl in (rest of heavyUrls)
        make new tab at end of tabs of front window with properties {URL:heavyUrl}
    end repeat
end tell
EOF
}

# Cycles the active tab inside the front Chrome window to churn compositing.
cycle_tab() {
    osascript >/dev/null 2>&1 <<'EOF' || true
tell application "Google Chrome"
    if it is not running then return
    if (count of windows) = 0 then return
    set tabCount to count of tabs of front window
    if tabCount < 2 then return
    set currentIndex to active tab index of front window
    set active tab index of front window to (currentIndex mod tabCount) + 1
end tell
EOF
}

# Turns the display off, waits, then simulates user activity to wake it again.
cycle_display() {
    pmset displaysleepnow >/dev/null 2>&1 || true
    sleep "$sleep_seconds"
    caffeinate -u -t 2 >/dev/null 2>&1 || true
    sleep "$awake_seconds"
}

seed_reports
printf 'Seeded %s existing reports; artifacts stay in %s and ~/Desktop/NootRX-diagnostics\n' \
    "$(wc -l < "$seen_reports" | tr -d ' ')" "$reports_dir"

if [ "$do_chrome" -eq 1 ]; then
    open_pages
    printf 'Chrome repro window opened (aquarium WebGL, three.js, WebGPU animometer).\n'
fi

round=1
while [ "$round" -le "$rounds" ]; do
    printf 'round %s\n' "$round" >> "$action_log"
    if ! check_failure; then
        printf 'Failure captured. Keep the machine running and check ~/Desktop/NootRX-diagnostics/gfxdiag.log.gz\n'
    fi
    print_state "$round"

    if [ "$do_chrome" -eq 1 ]; then
        cycle_tab
    fi

    if [ "$do_display" -eq 1 ]; then
        cycle_display
    else
        sleep "$tab_seconds"
    fi
    round=$((round + 1))
done

printf 'Harness finished. Reports: %s\n' "$reports_dir"
