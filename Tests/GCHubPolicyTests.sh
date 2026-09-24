#!/bin/sh

set -eu

x6000_source="${1:-NootRX/X6000.cpp}"
x6000_header="${2:-NootRX/X6000.hpp}"
nootrx_source="${3:-NootRX/NootRX.cpp}"

if rg -n 'AMDRadeonX6000_AMDNavi21Hardware8newGCHubEv|wrapNewGCHub|donorNewGCHub|GCHub_10_3_0|NootRX_GCHubDonor|NootRX_GCHubClass' \
    "$x6000_header" "$x6000_source" "$nootrx_source"; then
    printf '%s\n' 'FAIL: Tahoe Navi22 must keep the native Navi23 GCHub path'
    exit 1
fi
if rg -n 'newMMHub|AMDMMHub_2_1_0|AMDNavi21Hardware.*newMMHub' "$x6000_source"; then
    printf '%s\n' 'FAIL: GCHub rollback must not change MMHub selection'
    exit 1
fi

printf '%s\n' 'PASS: Tahoe Navi22 keeps the native Navi23 GCHub without MMHub changes'
