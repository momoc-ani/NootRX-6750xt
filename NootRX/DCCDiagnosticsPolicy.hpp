#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

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
    bool shouldPublish;
    bool overflow;
    uint32_t sequence;
    uint32_t repeatCount;
};

class DCCObservationCache {
    public:
    static constexpr size_t Capacity = 32;

    // Records one observation using independent bounded storage for its diagnostic stage.
    DCCObservationDecision observe(const DCCObservationKey &key, bool failure) {
        auto &stageCache = this->stageCaches[stageIndex(key.stage)];
        if (failure) { this->errorValue += 1; }

        for (size_t index = 0; index < stageCache.entryCount; index += 1) {
            auto &entry = stageCache.entries[index];
            if (entry.failure == failure && entry.key.equals(key)) {
                this->duplicateValue += 1;
                entry.repeatCount += 1;
                const auto checkpoint = isLogarithmicCheckpoint(entry.repeatCount);
                if (checkpoint) { this->sequenceValue += 1; }
                return {failure && checkpoint, checkpoint, false, this->sequenceValue, entry.repeatCount};
            }
        }

        if (stageCache.entryCount < Capacity) {
            auto &entry = stageCache.entries[stageCache.entryCount];
            entry.key = key;
            entry.failure = failure;
            entry.repeatCount = 0;
            stageCache.entryCount += 1;
            this->sequenceValue += 1;
            return {true, true, false, this->sequenceValue, 0};
        }

        stageCache.overflowCount += 1;
        this->overflowValue += 1;
        const auto checkpoint = isLogarithmicCheckpoint(stageCache.overflowCount);
        if (checkpoint) { this->sequenceValue += 1; }
        return {checkpoint, checkpoint, true, this->sequenceValue, stageCache.overflowCount};
    }

    // Returns the number of exact success or failure repetitions observed so far.
    uint32_t duplicateCount() const { return this->duplicateValue; }

    // Returns the number of failure observations recorded so far.
    uint32_t errorCount() const { return this->errorValue; }

    // Returns observations omitted after the fixed cache became full.
    uint32_t overflowCount() const { return this->overflowValue; }

    private:
    static constexpr size_t StageCount = 4;

    struct Entry {
        DCCObservationKey key {};
        bool failure {false};
        uint32_t repeatCount {0};
    };

    struct StageCache {
        Entry entries[Capacity] {};
        size_t entryCount {0};
        uint32_t overflowCount {0};
    };

    // Converts the one-based public stage identifier into fixed-array storage.
    static constexpr size_t stageIndex(DCCDiagnosticStage stage) {
        const auto value = static_cast<size_t>(stage);
        return value >= 1 && value <= StageCount ? value - 1 : 0;
    }

    // Returns true only for the bounded repeat checkpoints 1, 2, 4, 8, and so on.
    static constexpr bool isLogarithmicCheckpoint(uint32_t value) {
        return value != 0 && (value & (value - 1)) == 0;
    }

    StageCache stageCaches[StageCount] {};
    uint32_t sequenceValue {0};
    uint32_t duplicateValue {0};
    uint32_t errorValue {0};
    uint32_t overflowValue {0};
};

namespace DCCDiagnosticsPolicy {

// Restricts build-specific diagnostics to the verified Tahoe 25E253 RX 6750 XT target.
inline bool isTarget(bool isTahoe, const char *osBuild, uint32_t deviceId, uint32_t pciRevision,
    bool diagnosticsRequested) {
    return isTahoe && osBuild != nullptr && strcmp(osBuild, "25E253") == 0 && deviceId == 0x73DF &&
           pciRevision == 0xC0 && diagnosticsRequested;
}

}    // namespace DCCDiagnosticsPolicy
