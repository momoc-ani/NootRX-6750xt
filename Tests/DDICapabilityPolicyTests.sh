#!/bin/sh

set -eu

framebuffer_source="${1:-NootRX/X6000FB.cpp}"
hwlibs_source="${2:-NootRX/HWLibs.cpp}"
binary="${3:-}"
main_header="NootRX/NootRX.hpp"
main_source="NootRX/NootRX.cpp"

# Reports one integration-policy failure and stops the test immediately.
fail() {
    printf '%s\n' "FAIL: $1"
    exit 1
}

# Both Apple driver tables must use the same tested capability selection policy.
framebuffer_policy_count="$(rg -F -c 'DDICapabilityPolicy::select' "$framebuffer_source" || true)"
hwlibs_policy_count="$(rg -F -c 'DDICapabilityPolicy::select' "$hwlibs_source" || true)"
[ -n "$framebuffer_policy_count" ] || framebuffer_policy_count=0
[ -n "$hwlibs_policy_count" ] || hwlibs_policy_count=0
[ "$framebuffer_policy_count" -eq 1 ] ||
    fail "X6000Framebuffer must select DDI caps through DDICapabilityPolicy"
[ "$hwlibs_policy_count" -eq 1 ] ||
    fail "HWLibs must select DDI caps through DDICapabilityPolicy"

# Direct universal-table assignments would bypass the Tahoe Navi22 preservation rule.
if rg -n -- '->caps = ddiCapsNavi2Universal' "$framebuffer_source" "$hwlibs_source" >/dev/null; then
    fail "driver tables must not assign ddiCapsNavi2Universal directly"
fi

# Each patched table publishes its effective feature word for post-boot verification.
rg -F 'NootRX_DDICaps_X6000FB' "$framebuffer_source" >/dev/null ||
    fail "X6000Framebuffer DDI capability property is missing"
rg -F 'NootRX_DDICaps_HWLibs' "$hwlibs_source" >/dev/null ||
    fail "HWLibs DDI capability property is missing"
rg -F 'publishDDICapabilitySelection' "$main_header" >/dev/null ||
    fail "DDI capability publisher declaration is missing"
rg -F 'NootRXMain::publishDDICapabilitySelection' "$main_source" >/dev/null ||
    fail "DDI capability publisher implementation is missing"

# The HWLibs init table must consume the same final pointer as its main capability table.
rg -F '.caps = orgCapsTable->caps' "$hwlibs_source" >/dev/null ||
    fail "HWLibs init and main capability tables are not linked"

if [ -n "$binary" ]; then
    strings -a "$binary" | rg -F 'NootRX_DDICaps_X6000FB' >/dev/null ||
        fail "built kext is missing the X6000Framebuffer DDI property"
    strings -a "$binary" | rg -F 'NootRX_DDICaps_HWLibs' >/dev/null ||
        fail "built kext is missing the HWLibs DDI property"
fi

printf '%s\n' "PASS: Tahoe Navi22 preserves native DDI caps in both Apple driver tables"
