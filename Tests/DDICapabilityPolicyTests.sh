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

# X6000Framebuffer must select from its donor first and write the result back to that same entry.
if ! rg -U -q '^[[:space:]]*const auto \*selectedCaps = DDICapabilityPolicy::select\(donorEntry->caps, ddiCapsNavi2Universal,\n[[:space:]]+getKernelVersion\(\) == KernelVersion::Tahoe, NootRXMain::callback->attributes\.isNavi22\(\)\);' \
    "$framebuffer_source"; then
    fail "X6000Framebuffer must select donor DDI caps before the universal table"
fi
rg -n '^[[:space:]]*donorEntry->caps = selectedCaps;$' "$framebuffer_source" >/dev/null ||
    fail "X6000Framebuffer must write selected DDI caps back to its donor entry"
if ! rg -U -q 'publishDDICapabilitySelection\("NootRX_DDICaps_X6000FB", donorDeviceId,\n[[:space:]]+donorEntry->caps\);' \
    "$framebuffer_source"; then
    fail "X6000Framebuffer must publish its final donor caps pointer"
fi

# HWLibs must apply the same input order, final writeback, init-table link, and published pointer.
if ! rg -U -q '^[[:space:]]*selectedCaps = DDICapabilityPolicy::select\(orgCapsTable->caps, ddiCapsNavi2Universal,\n[[:space:]]+getKernelVersion\(\) == KernelVersion::Tahoe, NootRXMain::callback->attributes\.isNavi22\(\)\);' \
    "$hwlibs_source"; then
    fail "HWLibs must select donor DDI caps before the universal table"
fi
rg -n '^[[:space:]]*orgCapsTable->caps = selectedCaps;$' "$hwlibs_source" >/dev/null ||
    fail "HWLibs must write selected DDI caps back to its donor entry"
rg -n '^[[:space:]]*\.caps = orgCapsTable->caps,$' "$hwlibs_source" >/dev/null ||
    fail "HWLibs init and main capability tables are not linked"
if ! rg -U -q '^[[:space:]]*NootRXMain::callback->publishDDICapabilitySelection\("NootRX_DDICaps_HWLibs", targetDeviceId,\n[[:space:]]+orgCapsTable->caps\);' \
    "$hwlibs_source"; then
    fail "HWLibs must publish its final caps table pointer"
fi

# Direct universal-table assignments would bypass the Tahoe Navi22 preservation rule.
if rg -n -- '->caps = ddiCapsNavi2Universal' "$framebuffer_source" "$hwlibs_source" >/dev/null; then
    fail "driver tables must not assign ddiCapsNavi2Universal directly"
fi

# The shared publisher must remain available to both patched tables.
rg -F 'publishDDICapabilitySelection' "$main_header" >/dev/null ||
    fail "DDI capability publisher declaration is missing"
rg -F 'NootRXMain::publishDDICapabilitySelection' "$main_source" >/dev/null ||
    fail "DDI capability publisher implementation is missing"

if [ -n "$binary" ]; then
    strings -a "$binary" | rg -F 'NootRX_DDICaps_X6000FB' >/dev/null ||
        fail "built kext is missing the X6000Framebuffer DDI property"
    strings -a "$binary" | rg -F 'NootRX_DDICaps_HWLibs' >/dev/null ||
        fail "built kext is missing the HWLibs DDI property"
fi

printf '%s\n' "PASS: Tahoe Navi22 preserves native DDI caps in both Apple driver tables"
