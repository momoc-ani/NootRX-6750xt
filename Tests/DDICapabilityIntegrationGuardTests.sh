#!/bin/sh

set -eu

integration_test="${1:-Tests/DDICapabilityPolicyTests.sh}"

# Verifies that one invalid source fixture is rejected by the integration policy test.
expect_rejected() {
    fixture_name="$1"
    framebuffer_source="$2"
    hwlibs_source="$3"

    if sh "$integration_test" "$framebuffer_source" "$hwlibs_source" >/dev/null 2>&1; then
        printf '%s\n' "FAIL: integration guard accepted $fixture_name"
        exit 1
    fi
}

expect_rejected "comment-only wiring" \
    Tests/Fixtures/DDICapsCommentOnlyX6000FB.cpp Tests/Fixtures/DDICapsCommentOnlyHWLibs.cpp
expect_rejected "swapped donor and universal arguments" \
    Tests/Fixtures/DDICapsSwappedArgumentsX6000FB.cpp NootRX/HWLibs.cpp
expect_rejected "missing framebuffer writeback" \
    Tests/Fixtures/DDICapsMissingWritebackX6000FB.cpp NootRX/HWLibs.cpp

printf '%s\n' "PASS: DDI capability integration guard rejects invalid wiring"
