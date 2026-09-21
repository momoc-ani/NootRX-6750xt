#!/bin/sh

set -eu

main_source="${1:-NootRX/NootRX.cpp}"
binary="${2:-}"
test_tmp_dir="$(mktemp -d "${TMPDIR:-/private/tmp}/nootrx-dcc-integration.XXXXXX")"
effective_source="$test_tmp_dir/NootRX.effective.cpp"

# Reports one DCC integration failure and stops the test immediately.
fail() {
    printf '%s\n' "FAIL: $1"
    exit 1
}

# Removes only the temporary directory created by this test invocation.
cleanup() {
    case "$test_tmp_dir" in
        "${TMPDIR:-/private/tmp}"/nootrx-dcc-integration.*) rm -rf -- "$test_tmp_dir" ;;
        *) printf '%s\n' "FAIL: refusing to remove unexpected temporary path $test_tmp_dir" >&2 ;;
    esac
}

trap cleanup EXIT HUP INT TERM

sdk_path="$(xcrun --sdk macosx --show-sdk-path)" || fail "failed to locate the macOS SDK"
clang -E -P -fdirectives-only -x c++ -std=c++17 -nostdinc \
    -DKERNEL -DKERNEL_PRIVATE -DDRIVER_PRIVATE -DAPPLE -DNeXT \
    -I . -I NootRX -I Lilu/Lilu -I MacKernelSDK/Headers \
    -I "$sdk_path/System/Library/Frameworks/Kernel.framework/PrivateHeaders" \
    -isysroot "$sdk_path" "$main_source" -o "$effective_source" ||
    fail "failed to preprocess $main_source"

rg -F 'GPUDCCDisplayablePolicy::isTarget(' "$effective_source" >/dev/null ||
    fail "Tahoe RX 6750 XT target gate is missing"
rg -F 'lilu_get_boot_args(kGPUDCCDisplayableArg' "$effective_source" >/dev/null ||
    fail "DCC displayable boot argument is not parsed"
rg -F 'AMDRadeonX6000_AMDNavi23GraphicsAccelerator' "$effective_source" >/dev/null ||
    fail "Navi23 accelerator personality gate is missing"
rg -F '0x73DF1002' "$effective_source" >/dev/null ||
    fail "RX 6750 XT IOPCIMatch gate is missing"
rg -F '!policy.overridden' "$effective_source" >/dev/null ||
    fail "personality rewrite is not guarded by an explicit override"
rg -F 'identifierIndex == 0' "$effective_source" >/dev/null ||
    fail "DCC rewrite is not restricted to AMDRadeonX6000 injection"
rg -F 'GPUDCCDisplayable' "$effective_source" >/dev/null ||
    fail "GPUDCCDisplayable personality write is missing"
rg -F 'NootRX_GPUDCCDisplayable' "$effective_source" >/dev/null ||
    fail "effective DCC IORegistry property is missing"
rg -F 'NootRX_GPUDCCDisplayableOverride' "$effective_source" >/dev/null ||
    fail "DCC override IORegistry property is missing"
rg -F 'os=Tahoe' "$effective_source" >/dev/null ||
    fail "DCC startup diagnostic log is missing"

if [ -n "$binary" ]; then
    strings -a "$binary" | rg -F 'nootrx-gpu-dcc-displayable' >/dev/null ||
        fail "built kext is missing the DCC displayable boot argument"
    strings -a "$binary" | rg -F 'NootRX_GPUDCCDisplayableOverride' >/dev/null ||
        fail "built kext is missing the DCC override property"
fi

printf '%s\n' "PASS: Tahoe RX 6750 XT DCC displayable override is wired to the accelerator personality"
