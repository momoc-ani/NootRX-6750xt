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

rg -F 'checkKernelArgument("-NRXDCCDiag")' "$main_effective" >/dev/null ||
    fail "independent -NRXDCCDiag boot argument is missing"
rg -F 'DiagnosticsModePolicy::select(' "$main_effective" >/dev/null ||
    fail "independent diagnostics mode selection is missing"
rg -F 'this->dccDiagnosticsRequested' "$main_effective" >/dev/null ||
    fail "DCC diagnostics request state is not stored independently"
rg -F 'DCCDiagnosticsPolicy::isTarget(' "$main_effective" >/dev/null ||
    fail "DCC diagnostic target gate is missing"
rg -F 'DCCDiagnosticsPolicy::isTarget(getKernelVersion() == KernelVersion::Tahoe, osversion,' \
    "$main_effective" >/dev/null || fail "DCC diagnostics are not gated to the verified OS build"
rg -U -P -q 'DCCDiagnosticsPolicy::isTarget\((?s:.*?)this->pciRevision, this->dccDiagnosticsRequested\)' \
    "$main_effective" || fail "DCC diagnostics still depend on the Power diagnostics request"

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
rg -F 'diagnostics.isEnabled()' "$x6000_effective" >/dev/null ||
    fail "accelerator DCC routes are not guarded by diagnostic mode"
rg -F 'KernelPatcher::SolveRequest diagnosticSymbols[]' "$x6000_effective" >/dev/null ||
    fail "accelerator DCC symbols are not pre-resolved as one set"
rg -F 'patcher.solveMultiple(id, diagnosticSymbols, slide, size)' "$x6000_effective" >/dev/null ||
    fail "accelerator DCC symbol pre-resolution is missing"
rg -F 'DCCRouteValidation::validateAlignManagerInit' "$x6000_effective" >/dev/null ||
    fail "align-manager route signature is not validated"
rg -F 'DCCRouteValidation::validateShouldAllocScanoutDcc' "$x6000_effective" >/dev/null ||
    fail "scanout-DCC route signature is not validated"
rg -F 'DCCRouteValidation::validateGetDccInfo2' "$x6000_effective" >/dev/null ||
    fail "AddrLib DCC route signature is not validated"
rg -F 'KernelPatcher::RouteRequest diagnosticRoutes[]' "$x6000_effective" >/dev/null ||
    fail "accelerator DCC wrappers are not installed as one route set"
rg -F 'patcher.routeMultiple(id, diagnosticRoutes, slide, size)' "$x6000_effective" >/dev/null ||
    fail "accelerator DCC wrappers do not use one routeMultiple call"
rg -F 'disable(DCCDiagnosticFailureCode::AcceleratorSymbol' "$x6000_effective" >/dev/null ||
    fail "accelerator symbol failure does not disable diagnostics"
rg -F 'disable(DCCDiagnosticFailureCode::AcceleratorSignature' "$x6000_effective" >/dev/null ||
    fail "accelerator signature failure does not disable diagnostics"
rg -F 'disable(DCCDiagnosticFailureCode::AcceleratorRoute' "$x6000_effective" >/dev/null ||
    fail "accelerator route failure does not disable diagnostics"
rg -F 'markRouteReady(DCCDiagnostics::AcceleratorRoute)' "$x6000_effective" >/dev/null ||
    fail "accelerator route readiness is not published"
if rg -F 'Failed to route DCC diagnostic symbols' "$x6000_effective" >/dev/null; then
    fail "accelerator diagnostic route failure still panics"
fi
if rg -F 'FunctionCast(wrapAlignManagerInit' "$x6000_effective" >/dev/null ||
    rg -F 'FunctionCast(wrapShouldAllocScanoutDcc' "$x6000_effective" >/dev/null ||
    rg -F 'FunctionCast(wrapGetDccInfo2' "$x6000_effective" >/dev/null; then
    fail "accelerator diagnostic wrappers still use untyped original function casts"
fi

if ! rg -U -P -q 'wrapAlignManagerInit\((?s:.*?)callback->orgAlignManagerInit\((?s:.*?);(?s:.*?)recordAddrLibIdentity(?s:.*?)return ret;' \
    "$x6000_effective"; then
    fail "align-manager wrapper does not preserve call-record-return order"
fi
if ! rg -U -q 'wrapShouldAllocScanoutDcc\([^}]+callback->orgShouldAllocScanoutDcc\([^;]+;[^}]+recordScanoutDecision[^}]+return ret;' \
    "$x6000_effective"; then
    fail "scanout wrapper does not preserve call-record-return order"
fi
if ! rg -U -q 'wrapGetDccInfo2\([^}]+callback->orgGetDccInfo2\([^;]+;[^}]+recordAddrLibDccInfo[^}]+return ret;' \
    "$x6000_effective"; then
    fail "AddrLib wrapper does not preserve call-record-return order"
fi

rg -F '__ZN34AMDRadeonX6000_AmdRadeonController28callPlatformFunctionFromDrvrEjPvS0_S0_' \
    "$framebuffer_effective" >/dev/null || fail "Framebuffer DCC capability route is missing"
rg -F 'requestType == 0x1A' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer wrapper is not restricted to request 0x1A"
rg -F 'recordFramebufferCapability' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer capability recorder call is missing"
rg -F 'KernelPatcher::SolveRequest diagnosticSymbol' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer DCC symbol is not pre-resolved"
rg -F 'patcher.solveMultiple(id, &diagnosticSymbol, 1, slide, size)' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer DCC symbol pre-resolution is missing"
rg -F 'DCCRouteValidation::validateFramebufferRequest' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer DCC route signature is not validated"
rg -F 'KernelPatcher::RouteRequest diagnosticRoute' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer diagnostic wrapper does not use a native route request"
rg -F 'patcher.routeMultiple(id, &diagnosticRoute, 1, slide, size)' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer diagnostic wrapper does not use routeMultiple"
rg -F 'disable(DCCDiagnosticFailureCode::FramebufferSymbol' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer symbol failure does not disable diagnostics"
rg -F 'disable(DCCDiagnosticFailureCode::FramebufferSignature' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer signature failure does not disable diagnostics"
rg -F 'disable(DCCDiagnosticFailureCode::FramebufferRoute' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer route failure does not disable diagnostics"
rg -F 'markRouteReady(DCCDiagnostics::FramebufferRoute)' "$framebuffer_effective" >/dev/null ||
    fail "Framebuffer route readiness is not published"
if rg -F 'Failed to route Framebuffer DCC capability diagnostics' "$framebuffer_effective" >/dev/null; then
    fail "Framebuffer diagnostic route failure still panics"
fi
if rg -F 'FunctionCast(wrapCallPlatformFunctionFromDrvr' "$framebuffer_effective" >/dev/null; then
    fail "Framebuffer diagnostic wrapper still uses an untyped original function cast"
fi
if ! rg -U -P -q 'wrapCallPlatformFunctionFromDrvr\((?s:.*?)callback->orgCallPlatformFunctionFromDrvr\((?s:.*?);(?s:.*?)recordFramebufferCapability(?s:.*?)return ret;' \
    "$framebuffer_effective"; then
    fail "Framebuffer wrapper does not preserve call-record-return order"
fi

if [ -n "$binary" ]; then
    strings -a "$binary" | rg -F -- '-NRXDCCDiag' >/dev/null ||
        fail "built kext is missing the independent DCC diagnostics boot argument"
    strings -a "$binary" | rg -F -- '-NRXPowerDiag' >/dev/null ||
        fail "built kext is missing the independent Power diagnostics boot argument"
    strings -a "$binary" | rg -F 'NootRX_DCCDiagEnabled' >/dev/null ||
        fail "built kext is missing the DCC diagnostic enabled property"
    strings -a "$binary" | rg -F 'DCCDIAG' >/dev/null ||
        fail "built kext is missing the DCC diagnostic log marker"
fi

printf '%s\n' "PASS: Tahoe RX 6750 XT DCC diagnostics are gated, read-only, and present in the build"
