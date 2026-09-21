#pragma once

#include <stddef.h>
#include <stdint.h>

// Mirrors Tahoe 25E253's AddrLib2 DCC input. The size field must equal 0x34 before use.
struct AppleAddr2ComputeDccInfoInputV1 {
    uint32_t size;
    uint32_t dccKeyFlags;
    uint32_t colorFlags;
    uint32_t resourceType;
    uint32_t swizzleMode;
    uint32_t bpp;
    uint32_t unalignedWidth;
    uint32_t unalignedHeight;
    uint32_t numSlices;
    uint32_t numFrags;
    uint32_t numMipLevels;
    uint32_t dataSurfaceSize;
    uint32_t firstMipIdInTail;
};

// Mirrors Tahoe 25E253's AddrLib2 DCC output. The trailing pointer is observed but never dereferenced.
struct AppleAddr2ComputeDccInfoOutputV1 {
    uint32_t size;
    uint32_t dccRamBaseAlign;
    uint32_t dccRamSize;
    uint32_t pitch;
    uint32_t height;
    uint32_t depth;
    uint32_t compressBlkWidth;
    uint32_t compressBlkHeight;
    uint32_t compressBlkDepth;
    uint32_t metaBlkWidth;
    uint32_t metaBlkHeight;
    uint32_t metaBlkDepth;
    uint32_t metaBlkSize;
    uint32_t metaBlkNumPerSlice;
    uint32_t dccRamSliceSize;
    uint32_t alignmentPadding;
    void *pMipInfo;
};

// Mirrors the version-1 request sent by shouldAllocScanoutDcc to request type 0x1A.
struct AppleDccCapsParametersV1 {
    uint32_t version;
    uint32_t pixelFormat;
    uint64_t reserved;
    float width;
    float height;
    uint32_t candidateFlags;
    uint32_t field0;
};

// Mirrors the version-1 response returned by Framebuffer request type 0x1A.
struct AppleDccCapabilitiesV1 {
    uint32_t version;
    uint8_t capability0;
    uint8_t capability1;
    uint8_t capability2;
    uint8_t reserved;
    uint32_t field0;
    uint32_t field1;
};

static_assert(sizeof(AppleAddr2ComputeDccInfoInputV1) == 0x34);
static_assert(sizeof(AppleAddr2ComputeDccInfoOutputV1) == 0x48);
static_assert(sizeof(AppleDccCapsParametersV1) == 0x20);
static_assert(sizeof(AppleDccCapabilitiesV1) == 0x10);

enum class DCCDiagnosticStage : uint32_t {
    AddrLibIdentity = 1,
    ScanoutDecision = 2,
    AddrLibDccInfo = 3,
    FramebufferCapability = 4,
};

struct DCCObservationKey {
    static constexpr size_t MaxFields = 16;

    DCCDiagnosticStage stage;
    uint32_t fields[MaxFields];
    size_t fieldCount;

    // Compares the complete observation without a hash or digest.
    bool equals(const DCCObservationKey &other) const {
        if (this->stage != other.stage || this->fieldCount != other.fieldCount ||
            this->fieldCount > MaxFields) {
            return false;
        }

        for (size_t index = 0; index < this->fieldCount; index += 1) {
            if (this->fields[index] != other.fields[index]) { return false; }
        }

        return true;
    }
};

struct DCCObservationDecision {
    bool shouldLog;
    uint32_t sequence;
};

class DCCObservationCache {
    public:
    static constexpr size_t Capacity = 32;

    // Logs new values and failures while suppressing exact successful duplicates.
    DCCObservationDecision observe(const DCCObservationKey &key, bool failure) {
        if (failure) {
            this->errorValue += 1;
            this->sequenceValue += 1;
            return {true, this->sequenceValue};
        }

        for (size_t index = 0; index < this->entryCount; index += 1) {
            if (this->entries[index].equals(key)) {
                this->duplicateValue += 1;
                return {false, this->sequenceValue};
            }
        }

        if (this->entryCount < Capacity) {
            this->entries[this->entryCount] = key;
            this->entryCount += 1;
            this->sequenceValue += 1;
            return {true, this->sequenceValue};
        }

        this->overflowValue += 1;
        if (this->overflowValue == 1) {
            this->sequenceValue += 1;
            return {true, this->sequenceValue};
        }

        return {false, this->sequenceValue};
    }

    // Returns the number of exact successful duplicates suppressed so far.
    uint32_t duplicateCount() const { return this->duplicateValue; }

    // Returns the number of failure observations recorded so far.
    uint32_t errorCount() const { return this->errorValue; }

    // Returns observations omitted after the fixed cache became full.
    uint32_t overflowCount() const { return this->overflowValue; }

    private:
    DCCObservationKey entries[Capacity] {};
    size_t entryCount {0};
    uint32_t sequenceValue {0};
    uint32_t duplicateValue {0};
    uint32_t errorValue {0};
    uint32_t overflowValue {0};
};

namespace DCCDiagnosticsPolicy {

// Restricts diagnostics to the approved Tahoe RX 6750 XT target and explicit diagnostic mode.
constexpr bool isTarget(bool isTahoe, uint32_t deviceId, uint32_t pciRevision, bool diagnosticsRequested) {
    return isTahoe && deviceId == 0x73DF && pciRevision == 0xC0 && diagnosticsRequested;
}

}    // namespace DCCDiagnosticsPolicy
