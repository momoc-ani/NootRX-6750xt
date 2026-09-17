#!/bin/sh

set -eu

framebuffer_source="${1:-NootRX/X6000FB.cpp}"
hwlibs_source="${2:-NootRX/HWLibs.cpp}"
binary="${3:-}"
main_header="NootRX/NootRX.hpp"
main_source="NootRX/NootRX.cpp"
test_tmp_dir="$(mktemp -d "${TMPDIR:-/private/tmp}/nootrx-ddi-caps.XXXXXX")"
effective_framebuffer_source="$test_tmp_dir/X6000FB.effective.cpp"
effective_hwlibs_source="$test_tmp_dir/HWLibs.effective.cpp"

# Reports one integration-policy failure and stops the test immediately.
fail() {
    printf '%s\n' "FAIL: $1"
    exit 1
}

# Removes comments and inactive preprocessor branches before structural source checks.
preprocess_source() {
    input_source="$1"
    output_source="$2"
    sdk_path="$3"

    clang -E -P -fdirectives-only -x c++ -std=c++17 -nostdinc \
        -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
        -I . -I NootRX -I Lilu/Lilu -I MacKernelSDK/Headers \
        -I "$sdk_path/System/Library/Frameworks/Kernel.framework/PrivateHeaders" \
        -isysroot "$sdk_path" "$input_source" -o "$output_source" ||
        fail "failed to preprocess $input_source"
}

# Removes only the temporary directory created by this test invocation.
cleanup() {
    case "$test_tmp_dir" in
        "${TMPDIR:-/private/tmp}"/nootrx-ddi-caps.*) rm -rf -- "$test_tmp_dir" ;;
        *) printf '%s\n' "FAIL: refusing to remove unexpected temporary path $test_tmp_dir" >&2 ;;
    esac
}

trap cleanup EXIT HUP INT TERM

sdk_path="$(xcrun --sdk macosx --show-sdk-path)" || fail "failed to locate the macOS SDK"
preprocess_source "$framebuffer_source" "$effective_framebuffer_source" "$sdk_path"
preprocess_source "$hwlibs_source" "$effective_hwlibs_source" "$sdk_path"

# X6000Framebuffer must select from its donor first and write the result back to that same entry.
if ! rg -U -q '^[[:space:]]*const auto \*selectedCaps = DDICapabilityPolicy::select\(donorEntry->caps, ddiCapsNavi2Universal,\n[[:space:]]+getKernelVersion\(\) == KernelVersion::Tahoe, NootRXMain::callback->attributes\.isNavi22\(\)\);' \
    "$effective_framebuffer_source"; then
    fail "X6000Framebuffer must select donor DDI caps before the universal table"
fi
rg -n '^[[:space:]]*donorEntry->caps = selectedCaps;$' "$effective_framebuffer_source" >/dev/null ||
    fail "X6000Framebuffer must write selected DDI caps back to its donor entry"
if ! rg -U -q 'publishDDICapabilitySelection\("NootRX_DDICaps_X6000FB", donorDeviceId,\n[[:space:]]+donorEntry->caps\);' \
    "$effective_framebuffer_source"; then
    fail "X6000Framebuffer must publish its final donor caps pointer"
fi

# HWLibs must apply the same input order, final writeback, init-table link, and published pointer.
if ! rg -U -q '^[[:space:]]*selectedCaps = DDICapabilityPolicy::select\(orgCapsTable->caps, ddiCapsNavi2Universal,\n[[:space:]]+getKernelVersion\(\) == KernelVersion::Tahoe, NootRXMain::callback->attributes\.isNavi22\(\)\);' \
    "$effective_hwlibs_source"; then
    fail "HWLibs must select donor DDI caps before the universal table"
fi
rg -n '^[[:space:]]*orgCapsTable->caps = selectedCaps;$' "$effective_hwlibs_source" >/dev/null ||
    fail "HWLibs must write selected DDI caps back to its donor entry"
rg -n '^[[:space:]]*\.caps = orgCapsTable->caps,$' "$effective_hwlibs_source" >/dev/null ||
    fail "HWLibs init and main capability tables are not linked"
if ! rg -U -q '^[[:space:]]*NootRXMain::callback->publishDDICapabilitySelection\("NootRX_DDICaps_HWLibs", targetDeviceId,\n[[:space:]]+orgCapsTable->caps\);' \
    "$effective_hwlibs_source"; then
    fail "HWLibs must publish its final caps table pointer"
fi

# Direct universal-table assignments would bypass the Tahoe Navi22 preservation rule.
if rg -n -- '->caps = ddiCapsNavi2Universal' "$effective_framebuffer_source" "$effective_hwlibs_source" >/dev/null; then
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
