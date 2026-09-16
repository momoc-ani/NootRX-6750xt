#!/bin/sh

set -eu

hwlibs_source="${1:-NootRX/HWLibs.cpp}"
hwlibs_header="${2:-NootRX/HWLibs.hpp}"
nootrx_source="${3:-NootRX/NootRX.cpp}"
binary="${4:-}"

# Tahoe must use the hardware-reported GC version instead of the 10.3.4
# software-init override, while earlier supported systems retain upstream behavior.
if ! rg -n "getKernelVersion\(\) == KernelVersion::Tahoe" "$hwlibs_source" >/dev/null; then
    printf '%s\n' "FAIL: Tahoe Navi22 GC runtime policy is not version-scoped"
    exit 1
fi

# The runtime version override must be in the non-Tahoe branch only.
if ! rg -U -q 'if \(getKernelVersion\(\) == KernelVersion::Tahoe\) \{\n\s+SYSLOG\(.+\);\n\s+\} else \{\n\s+const LookupPatchPlus patch \{&kextRadeonX6810HWLibs, kGcSwInitOriginal, kGcSwInitOriginalMask,\n\s+kGcSwInitPatched, kGcSwInitPatchedMask, 1\};\n\s+PANIC_COND\(!patch\.apply\(patcher, slide, size\), "HWLibs", "Failed to apply Navi 22 gc_sw_init patch"\);\n\s+\}' "$hwlibs_source"; then
    printf '%s\n' "FAIL: Tahoe does not bypass the Navi22 GC runtime override"
    exit 1
fi
if [ "$(rg -c 'Failed to apply Navi 22 gc_sw_init patch' "$hwlibs_source")" -ne 1 ]; then
    printf '%s\n' "FAIL: Navi22 GC runtime override is installed more than once"
    exit 1
fi
rg -n "kGcSwInitOriginal" "$hwlibs_header" >/dev/null

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
    if ! strings -a "$binary" | rg -F "Tahoe Navi22 GC runtime uses hardware version; retaining firmware descriptor compatibility" >/dev/null; then
        printf '%s\n' "FAIL: built kext does not contain the Tahoe Navi22 GC runtime policy"
        exit 1
    fi

    if ! strings -a "$binary" | rg -F "gc_10_3_2_rlc_ucode.bin" >/dev/null; then
        printf '%s\n' "FAIL: built kext does not contain Navi22 GC 10.3.2 RLC firmware"
        exit 1
    fi
fi

printf '%s\n' "PASS: Navi22 runtime version policy keeps hardware GC init and descriptor compatibility"
