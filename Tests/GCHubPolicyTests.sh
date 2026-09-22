#!/bin/sh

set -eu

x6000_source="${1:-NootRX/X6000.cpp}"
x6000_header="${2:-NootRX/X6000.hpp}"
nootrx_source="${3:-NootRX/NootRX.cpp}"

if ! rg -n 'AMDRadeonX6000_AMDNavi21Hardware8newGCHubEv' "$x6000_header" "$x6000_source"; then
    printf '%s\n' 'FAIL: Navi21 GCHub donor symbol is missing'
    exit 1
fi
if ! rg -n 'isNavi22\(\) && getKernelVersion\(\) == KernelVersion::Tahoe' "$x6000_source"; then
    printf '%s\n' 'FAIL: Tahoe + Navi22 GCHub gate is missing'
    exit 1
fi
if ! rg -U -q 'newGCHubEv",\s*wrapNewGCHub.*orgNewGCHub' "$x6000_source"; then
    printf '%s\n' 'FAIL: Navi23 newGCHub route is missing'
    exit 1
fi
if ! rg -n 'GCHub_10_3_0|NootRX_GCHubDonor|NootRX_GCHubClass' "$x6000_source" "$nootrx_source"; then
    printf '%s\n' 'FAIL: GCHub donor diagnostics are missing'
    exit 1
fi
if rg -n 'newMMHub|AMDMMHub_2_1_0|AMDNavi21Hardware.*newMMHub' "$x6000_source"; then
    printf '%s\n' 'FAIL: GCHub patch must not change MMHub selection'
    exit 1
fi

printf '%s\n' 'PASS: Tahoe Navi22 selects the Navi21 GCHub donor without changing MMHub'
