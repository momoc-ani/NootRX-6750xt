#!/bin/sh

set -eu

integration_test="${1:-Tests/DDICapabilityPolicyTests.sh}"

# Verifies that one invalid source fixture is rejected by the integration policy test.
expect_rejected() {
    fixture_name="$1"
    framebuffer_source="$2"
    hwlibs_source="$3"
    expected_reason="$4"

    if rejection_output="$(sh "$integration_test" "$framebuffer_source" "$hwlibs_source" 2>&1)"; then
        printf '%s\n' "FAIL: integration guard accepted $fixture_name"
        exit 1
    fi

    if ! printf '%s\n' "$rejection_output" | rg -F "$expected_reason" >/dev/null; then
        printf '%s\n' "FAIL: integration guard rejected $fixture_name for an unexpected reason"
        printf '%s\n' "$rejection_output"
        exit 1
    fi
}

[ -f "$integration_test" ] || {
    printf '%s\n' "FAIL: integration test script not found: $integration_test"
    exit 2
}

expect_rejected "comment-only wiring" \
    Tests/Fixtures/DDICapsCommentOnlyX6000FB.cpp Tests/Fixtures/DDICapsCommentOnlyHWLibs.cpp \
    "FAIL: X6000Framebuffer must select donor DDI caps before the universal table"
expect_rejected "block-comment wiring" \
    Tests/Fixtures/DDICapsBlockCommentX6000FB.cpp Tests/Fixtures/DDICapsBlockCommentHWLibs.cpp \
    "FAIL: X6000Framebuffer must select donor DDI caps before the universal table"
expect_rejected "disabled preprocessor wiring" \
    Tests/Fixtures/DDICapsIfZeroX6000FB.cpp Tests/Fixtures/DDICapsIfZeroHWLibs.cpp \
    "FAIL: X6000Framebuffer must select donor DDI caps before the universal table"
expect_rejected "swapped donor and universal arguments" \
    Tests/Fixtures/DDICapsSwappedArgumentsX6000FB.cpp NootRX/HWLibs.cpp \
    "FAIL: X6000Framebuffer must select donor DDI caps before the universal table"
expect_rejected "missing framebuffer writeback" \
    Tests/Fixtures/DDICapsMissingWritebackX6000FB.cpp NootRX/HWLibs.cpp \
    "FAIL: X6000Framebuffer must write selected DDI caps back to its donor entry"

printf '%s\n' "PASS: DDI capability integration guard rejects invalid wiring"
