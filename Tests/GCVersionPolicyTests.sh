#!/bin/sh

set -eu

hwlibs_source="${1:-NootRX/HWLibs.cpp}"
hwlibs_header="${2:-NootRX/HWLibs.hpp}"
nootrx_source="${3:-NootRX/NootRX.cpp}"
binary="${4:-}"

# Tahoe must retain the hardware-reported GC runtime and publish the separate
# firmware descriptor compatibility version. Earlier supported kernels retain
# the existing GC 10.3.4 runtime compatibility patch.
if ! rg -U -q 'if \(getKernelVersion\(\) == KernelVersion::Tahoe\) \{(?s:.*?)publishGCVersionSelection\(0x0A0302, 0x0A0304\);' "$hwlibs_source"; then
    printf '%s\n' "FAIL: Tahoe Navi22 must publish GC 10.3.2 runtime and 10.3.4 descriptor versions"
    exit 1
fi
if ! rg -U -q 'if \(getKernelVersion\(\) == KernelVersion::Tahoe\) \{(?s:.*?)\} else \{(?s:.*?)kGcSwInitOriginal(?s:.*?)Failed to apply Navi 22 gc_sw_init patch' "$hwlibs_source"; then
    printf '%s\n' "FAIL: non-Tahoe Navi22 must retain the gc_sw_init compatibility patch"
    exit 1
fi
if [ "$(rg -c 'Failed to apply Navi 22 gc_sw_init patch' "$hwlibs_source")" -ne 1 ]; then
    printf '%s\n' "FAIL: Navi22 GC runtime override is installed more than once"
    exit 1
fi
rg -n "kGcSwInitOriginal" "$hwlibs_header" >/dev/null

# Both GC version properties and the split log are required for post-boot diagnosis.
if ! rg -n 'NootRX_GCRuntimeVersion|NootRX_GCDescriptorVersion' "$nootrx_source" >/dev/null; then
    printf '%s\n' "FAIL: selected GC versions are not published to IORegistry"
    exit 1
fi
if ! rg -n 'Tahoe Navi22 GC runtime split' "$hwlibs_source" "$nootrx_source" >/dev/null; then
    printf '%s\n' "FAIL: Tahoe Navi22 GC split log is missing"
    exit 1
fi

# Both descriptor layouts must still be applied after their matching entry patch.
if ! rg -U -q 'kGcSetFwEntryInfoOriginal14_4, kGcSetFwEntryInfoOriginalMask14_4,(?s:.*?)PANIC_COND\(!LookupPatchPlus::applyAll\(patcher, patches, slide, size\), "HWLibs",\n\s+"Failed to apply Navi 22 patches \(>=14\.4\)"\);' "$hwlibs_source"; then
    printf '%s\n' "FAIL: >=14.4 GC firmware descriptor patch is not applied"
    exit 1
fi
if ! rg -U -q 'kGcSetFwEntryInfoOriginal, kGcSetFwEntryInfoOriginalMask,(?s:.*?)PANIC_COND\(!LookupPatchPlus::applyAll\(patcher, patches, slide, size\), "HWLibs",\n\s+"Failed to apply Navi 22 patches \(<14\.4\)"\);' "$hwlibs_source"; then
    printf '%s\n' "FAIL: <14.4 GC firmware descriptor patch is not applied"
    exit 1
fi

# Navi22 must continue loading its native GC 10.3.2 command processor and RLC firmware.
rg -n 'return "gc_10_3_2_";' "$nootrx_source" >/dev/null

if [ -n "$binary" ]; then
    if ! strings -a "$binary" | rg -F "Tahoe Navi22 GC runtime split" >/dev/null; then
        printf '%s\n' "FAIL: built kext does not contain the Tahoe Navi22 GC split log"
        exit 1
    fi

    if ! strings -a "$binary" | rg -F "NootRX_GCRuntimeVersion" >/dev/null; then
        printf '%s\n' "FAIL: built kext does not contain the GC runtime IORegistry property"
        exit 1
    fi

    if ! strings -a "$binary" | rg -F "NootRX_GCDescriptorVersion" >/dev/null; then
        printf '%s\n' "FAIL: built kext does not contain the GC descriptor IORegistry property"
        exit 1
    fi

    if ! strings -a "$binary" | rg -F "gc_10_3_2_rlc_ucode.bin" >/dev/null; then
        printf '%s\n' "FAIL: built kext does not contain Navi22 GC 10.3.2 RLC firmware"
        exit 1
    fi
fi

printf '%s\n' "PASS: Tahoe Navi22 keeps GC 10.3.2 runtime and GC 10.3.4 descriptor compatibility"
