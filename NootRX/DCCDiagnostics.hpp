#pragma once

#include "DCCDiagnosticsPolicy.hpp"
#include <IOKit/IOLocks.h>

class IOPCIDevice;
class OSDictionary;

enum class DCCDiagnosticFailureCode : uint32_t {
    None = 0,
    LockAllocation = 1,
    AcceleratorSymbol = 2,
    AcceleratorSignature = 3,
    AcceleratorRoute = 4,
    FramebufferSymbol = 5,
    FramebufferSignature = 6,
    FramebufferRoute = 7,
};

class DCCDiagnostics {
    public:
    static constexpr uint32_t AcceleratorRoute = 1U << 0;
    static constexpr uint32_t FramebufferRoute = 1U << 1;

    // Configures the target gate and publishes whether DCC diagnostics are active.
    void configure(IOPCIDevice *provider, bool enabled);

    // Returns whether any DCC diagnostic route may be installed or emit data.
    bool isEnabled() const;

    // Disables only the diagnostic branch after a symbol, signature, or route failure.
    void disable(DCCDiagnosticFailureCode code, const char *reason);

    // Records that one validated driver component completed all diagnostic routes.
    void markRouteReady(uint32_t route);

    // Records the exact engine, family, and revision used to create the AddrLib context.
    void recordAddrLibIdentity(uint32_t returnCode, uint32_t chipEngine, uint32_t chipFamily,
        uint32_t chipRevision);

    // Records the unmodified inputs and result of shouldAllocScanoutDcc.
    void recordScanoutDecision(uint32_t width, uint32_t height, uint32_t candidateFlags,
        uint32_t pixelFormatSelector, bool result);

    // Records the unmodified AddrLib2 DCC request and result after size validation.
    void recordAddrLibDccInfo(uint32_t returnCode, const AppleAddr2ComputeDccInfoInputV1 *input,
        const AppleAddr2ComputeDccInfoOutputV1 *output);

    // Records request 0x1A before and after the original Framebuffer method.
    void recordFramebufferCapability(uint32_t returnCode, const AppleDccCapsParametersV1 *input,
        const AppleDccCapabilitiesV1 *output);

    private:
    struct Snapshot {
        uint32_t stage {0};
        uint32_t returnCode {0};
        uint32_t width {0};
        uint32_t height {0};
        uint32_t widthBits {0};
        uint32_t heightBits {0};
        uint32_t pixelFormat {0};
        uint32_t candidateFlags {0};
        uint32_t decision {0};
        uint32_t chipEngine {0};
        uint32_t chipFamily {0};
        uint32_t chipRevision {0};
        uint32_t inputSize {0};
        uint32_t outputSize {0};
        uint32_t inputVersion {0};
        uint32_t outputVersion {0};
        uint32_t dccKeyFlags {0};
        uint32_t colorFlags {0};
        uint32_t resourceType {0};
        uint32_t swizzleMode {0};
        uint32_t bpp {0};
        uint32_t numSlices {0};
        uint32_t numFrags {0};
        uint32_t numMipLevels {0};
        uint32_t dataSurfaceSize {0};
        uint32_t firstMipIdInTail {0};
        uint32_t metaPitch {0};
        uint32_t metaHeight {0};
        uint32_t metaDepth {0};
        uint32_t metaSize {0};
        uint32_t metaAlign {0};
        uint32_t compressBlockWidth {0};
        uint32_t compressBlockHeight {0};
        uint32_t compressBlockDepth {0};
        uint32_t metaBlockWidth {0};
        uint32_t metaBlockHeight {0};
        uint32_t metaBlockDepth {0};
        uint32_t metaBlockSize {0};
        uint32_t metaBlockNumPerSlice {0};
        uint32_t dccRamSliceSize {0};
        uint32_t alignmentPadding {0};
        uint32_t mipInfoPresent {0};
        uint32_t capabilityBits {0};
        uint32_t capabilityField0 {0};
        uint32_t capabilityField1 {0};
    };

    // Applies the bounded observation policy, publishes counters, and emits one stable log record.
    void publishObservation(const char *stageName, const DCCObservationKey &key, const Snapshot &snapshot,
        bool failure);

    // Publishes one complete immutable dictionary while the observation lock preserves sequence order.
    bool publishSnapshotLocked(const Snapshot &snapshot, const DCCObservationDecision &decision,
        uint32_t duplicateCount, uint32_t errorCount, uint32_t overflowCount);

    // Adds one 32-bit scalar to a snapshot dictionary and releases its temporary number object.
    static bool setDictionaryNumber(OSDictionary *dictionary, const char *name, uint32_t value);

    IOPCIDevice *provider {nullptr};
    IOLock *lock {nullptr};
    DCCObservationCache cache {};
    uint32_t routeMask {0};
    DCCDiagnosticFailureCode failureCode {DCCDiagnosticFailureCode::None};
    bool enabled {false};
};
