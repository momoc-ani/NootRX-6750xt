#include "../NootRX/DCCRouteValidation.hpp"

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstring>

// Copies one independently recorded instruction fixture into its expected function offset.
template<size_t N, size_t M>
static void installFixture(uint8_t (&buffer)[N], size_t offset, const uint8_t (&fixture)[M]) {
    assert(offset + M <= N);
    memcpy(buffer + offset, fixture, M);
}

// Verifies the 25E253 align-manager entry and all three AddrLib identity getter offsets.
static void testAlignManagerValidationRejectsGetterDrift() {
    uint8_t function[0x120] {};
    const uint8_t prologue[] = {
        0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x54, 0x53, 0x48, 0x81, 0xEC, 0x90, 0x00,
        0x00, 0x00,
    };
    const uint8_t getters[] = {
        0x48, 0x8B, 0x7B, 0x10, 0x48, 0x8B, 0x07, 0xFF, 0x90, 0x40, 0x01, 0x00, 0x00, 0x41, 0x89, 0x46,
        0x04, 0x48, 0x8B, 0x7B, 0x10, 0x48, 0x8B, 0x07, 0xFF, 0x90, 0xB8, 0x01, 0x00, 0x00, 0x41, 0x89,
        0x46, 0x08, 0x48, 0x8B, 0x7B, 0x10, 0x48, 0x8B, 0x07, 0xFF, 0x90, 0xA8, 0x01, 0x00, 0x00, 0x41,
        0x89, 0x46, 0x0C,
    };
    installFixture(function, 0, prologue);
    installFixture(function, 0xC6, getters);

    assert(DCCRouteValidation::validateAlignManagerInit(function, sizeof(function)));
    function[0xC6 + 9] ^= 0x01;
    assert(!DCCRouteValidation::validateAlignManagerInit(function, sizeof(function)));
}

// Verifies the 25E253 scanout decision entry including the GPUDCCDisplayable bit test.
static void testScanoutValidationRejectsCapabilityDrift() {
    uint8_t function[0x40] {};
    const uint8_t fixture[] = {
        0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48,
        0x83, 0xEC, 0x38, 0x48, 0x8B, 0x47, 0x18, 0xF6, 0x80, 0x24, 0x1F, 0x00, 0x00, 0x01,
    };
    installFixture(function, 0, fixture);

    assert(DCCRouteValidation::validateShouldAllocScanoutDcc(function, sizeof(function)));
    function[27] = 0;
    assert(!DCCRouteValidation::validateShouldAllocScanoutDcc(function, sizeof(function)));
}

// Verifies the 25E253 AddrLib DCC entry and its null-argument error contract.
static void testGetDccInfoValidationRejectsReturnCodeDrift() {
    uint8_t function[0x40] {};
    const uint8_t fixture[] = {
        0x48, 0x85, 0xF6, 0x0F, 0x94, 0xC1, 0x48, 0x85, 0xD2,
        0x41, 0x0F, 0x94, 0xC0, 0xB8, 0xC2, 0x02, 0x00, 0xE0,
    };
    installFixture(function, 0, fixture);

    assert(DCCRouteValidation::validateGetDccInfo2(function, sizeof(function)));
    function[14] ^= 0x01;
    assert(!DCCRouteValidation::validateGetDccInfo2(function, sizeof(function)));
}

// Verifies the Framebuffer entry, request 0x1A ABI checks, and jump-table mapping to that branch.
static void testFramebufferValidationRejectsRequestMappingDrift() {
    uint8_t function[0x600] {};
    const uint8_t prologue[] = {
        0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41,
        0x55, 0x41, 0x54, 0x53, 0x48, 0x83, 0xEC, 0x28,
    };
    const uint8_t requestBranch[] = {
        0x49, 0x83, 0xBE, 0x38, 0x79, 0x00, 0x00, 0x00, 0x0F, 0x84, 0x50, 0x04, 0x00, 0x00,
        0x49, 0x8B, 0xBE, 0xE8, 0x78, 0x00, 0x00, 0x48, 0x8B, 0x07, 0xBE, 0x10, 0x00, 0x00,
        0x00, 0xFF, 0x90, 0x18, 0x01, 0x00, 0x00, 0x84, 0xC0, 0x0F, 0x84, 0x33, 0x04, 0x00,
        0x00, 0x4D, 0x85, 0xFF, 0x0F, 0x84, 0x95, 0x02, 0x00, 0x00, 0x41, 0x83, 0x3F, 0x01,
        0x0F, 0x85, 0xB1, 0x02, 0x00, 0x00, 0x4D, 0x85, 0xE4, 0x0F, 0x84, 0xF9, 0x02, 0x00,
        0x00, 0x41, 0x83, 0x3C, 0x24, 0x01,
    };
    const uint8_t requestJumpTableEntry[] = {0x90, 0xFB, 0xFF, 0xFF};
    installFixture(function, 0, prologue);
    installFixture(function, 0x13E, requestBranch);
    installFixture(function, 0x5EA, requestJumpTableEntry);

    assert(DCCRouteValidation::validateFramebufferRequest(function, sizeof(function)));
    function[0x5EA] ^= 0x01;
    assert(!DCCRouteValidation::validateFramebufferRequest(function, sizeof(function)));
}

// Verifies every validator rejects a buffer that cannot contain its furthest required instruction.
static void testValidationRejectsTruncatedFunctions() {
    uint8_t function[8] {};
    assert(!DCCRouteValidation::validateAlignManagerInit(function, sizeof(function)));
    assert(!DCCRouteValidation::validateShouldAllocScanoutDcc(function, sizeof(function)));
    assert(!DCCRouteValidation::validateGetDccInfo2(function, sizeof(function)));
    assert(!DCCRouteValidation::validateFramebufferRequest(function, sizeof(function)));
}

// Runs all dependency-free 25E253 DCC route validation tests.
int main() {
    testAlignManagerValidationRejectsGetterDrift();
    testScanoutValidationRejectsCapabilityDrift();
    testGetDccInfoValidationRejectsReturnCodeDrift();
    testFramebufferValidationRejectsRequestMappingDrift();
    testValidationRejectsTruncatedFunctions();
    return 0;
}
