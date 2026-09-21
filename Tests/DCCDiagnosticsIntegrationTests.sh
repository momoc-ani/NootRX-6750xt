#!/bin/sh

set -eu

main_source="${1:-NootRX/NootRX.cpp}"
x6000_source="${2:-NootRX/X6000.cpp}"
framebuffer_source="${3:-NootRX/X6000FB.cpp}"
diagnostics_source="${4:-NootRX/DCCDiagnostics.cpp}"
binary="${5:-}"
test_tmp_dir="$(mktemp -d "${TMPDIR:-/private/tmp}/nootrx-dcc-diagnostics.XXXXXX")"
main_effective="$test_tmp_dir/NootRX.effective.cpp"
x6000_effective="$test_tmp_dir/X6000.effective.cpp"
framebuffer_effective="$test_tmp_dir/X6000FB.effective.cpp"
diagnostics_effective="$test_tmp_dir/DCCDiagnostics.effective.cpp"

# Reports one DCC diagnostic integration failure and stops the test immediately.
fail() {
    printf '%s\n' "FAIL: $1"
    exit 1
}

# Removes comments and inactive preprocessor branches before structural checks.
preprocess_source() {
    input_source="$1"
    output_source="$2"
    sdk_path="$3"

    [ -f "$input_source" ] || fail "source file not found: $input_source"
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
        "${TMPDIR:-/private/tmp}"/nootrx-dcc-diagnostics.*) rm -rf -- "$test_tmp_dir" ;;
        *) printf '%s\n' "FAIL: refusing to remove unexpected temporary path $test_tmp_dir" >&2 ;;
    esac
}

trap cleanup EXIT HUP INT TERM

sdk_path="$(xcrun --sdk macosx --show-sdk-path)" || fail "failed to locate the macOS SDK"
preprocess_source "$main_source" "$main_effective" "$sdk_path"

rg -F 'DCCDiagnosticsPolicy::isTarget(' "$main_effective" >/dev/null ||
    fail "DCC diagnostic target gate is missing"
rg -F 'DCCDiagnosticsPolicy::isTarget(getKernelVersion() == KernelVersion::Tahoe, osversion,' \
    "$main_effective" >/dev/null || fail "DCC diagnostics are not gated to the verified OS build"

preprocess_source "$diagnostics_source" "$diagnostics_effective" "$sdk_path"
preprocess_source "$x6000_source" "$x6000_effective" "$sdk_path"
preprocess_source "$framebuffer_source" "$framebuffer_effective" "$sdk_path"

rg -F 'NootRX_DCCDiagEnabled' "$diagnostics_effective" >/dev/null ||
    fail "DCC diagnostic enabled property is missing"
rg -F 'NootRX_DCCDiagRouteMask' "$diagnostics_effective" >/dev/null ||
    fail "DCC diagnostic route mask is missing"
rg -F 'NootRX_DCCDiagFailureCode' "$diagnostics_effective" >/dev/null ||
    fail "DCC diagnostic failure code is missing"
rg -F 'NootRX_DCCDiagSnapshot' "$diagnostics_effective" >/dev/null ||
    fail "atomic DCC diagnostic snapshot property is missing"
rg -F 'DCCDiagnostics::disable(' "$diagnostics_effective" >/dev/null ||
    fail "DCC diagnostic safe-disable entry is missing"
rg -F 'DCCDiagnostics::markRouteReady(' "$diagnostics_effective" >/dev/null ||
    fail "DCC diagnostic route-ready entry is missing"
rg -F 'widthBits' "$diagnostics_effective" >/dev/null ||
    fail "Framebuffer width is not recorded as raw IEEE-754 bits"
rg -F 'heightBits' "$diagnostics_effective" >/dev/null ||
    fail "Framebuffer height is not recorded as raw IEEE-754 bits"
rg -F 'memcpy' "$diagnostics_effective" >/dev/null ||
    fail "Framebuffer float bit preservation is missing"
if rg -F 'NootRX_DCCDiagLast' "$diagnostics_effective" >/dev/null; then
    fail "legacy per-field DCC snapshot publication is still present"
fi
rg -F 'DCCDIAG' "$diagnostics_effective" >/dev/null ||
    fail "DCC diagnostic log marker is missing"

rg -F '__ZN33AMDRadeonX6000_AMDHWAlignManager24initEP30AMDRadeonX6000_IAMDHWInterface' \
    "$x6000_effective" >/dev/null || fail "AddrLib identity route is missing"
rg -F '__ZNK36AMDRadeonX6000_AMDAccelResourceAddr221shouldAllocScanoutDccEjjjj' \
    "$x6000_effective" >/dev/null || fail "scanout DCC route is missing"
rg -F '__ZN33AMDRadeonX6000_AMDHWAlignManager211getDccInfo2EP28_ADDR2_COMPUTE_DCCINFO_INPUTP29_ADDR2_COMPUTE_DCCINFO_OUTPUT' \
    "$x6000_effective" >/dev/null || fail "AddrLib DCC route is missing"
rg -F 'getDCCDiagnostics().isEnabled()' "$x6000_effective" >/dev/null ||
    fail "accelerator DCC routes are not guarded by diagnostic mode"

if ! rg -U -q 'wrapShouldAllocScanoutDcc\([^}]+FunctionCast\(wrapShouldAllocScanoutDcc,[^;]+;[^}]+recordScanoutDecision[^}]+return ret;' \
    "$x6000_effective"; then
    fail "scanout wrapper does not preserve call-record-return order"
fi
if ! rg -U -q 'wrapGetDccInfo2\([^}]+FunctionCast\(wrapGetDccInfo2,[^;]+;[^}]+recordAddrLibDccInfo[^}]+return ret;' \
    "$x6000_effective"; then
    fail "AddrLib wrapper does not preserve call-record-return order"
fi

rg -F '__ZN34AMDRadeonX6000_AmdRadeonController28callPlatformFunctionFromDrvrEjPvS0_S0_' \
    "$framebuffer_effective" >/dev/null || fail "Framebuffer DCC capability route is missing"
rg -F 'requestType == 0x1A' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer wrapper is not restricted to request 0x1A"
rg -F 'recordFramebufferCapability' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer capability recorder call is missing"

if [ -n "$binary" ]; then
    strings -a "$binary" | rg -F 'NootRX_DCCDiagEnabled' >/dev/null ||
        fail "built kext is missing the DCC diagnostic enabled property"
    strings -a "$binary" | rg -F 'DCCDIAG' >/dev/null ||
        fail "built kext is missing the DCC diagnostic log marker"
fi

printf '%s\n' "PASS: Tahoe RX 6750 XT DCC diagnostics are gated, read-only, and present in the build"
