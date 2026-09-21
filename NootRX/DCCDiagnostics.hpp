#pragma once

#include "DCCDiagnosticsPolicy.hpp"
#include <IOKit/IOLocks.h>

class IOPCIDevice;

class DCCDiagnostics {
    public:
    // Configures the target gate and publishes whether DCC diagnostics are active.
    void configure(IOPCIDevice *provider, bool enabled);

    // Returns whether any DCC diagnostic route may be installed or emit data.
    bool isEnabled() const;

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
        uint32_t pixelFormat {0};
        uint32_t candidateFlags {0};
        uint32_t decision {0};
        uint32_t chipEngine {0};
        uint32_t chipFamily {0};
        uint32_t chipRevision {0};
        uint32_t swizzleMode {0};
        uint32_t bpp {0};
        uint32_t metaPitch {0};
        uint32_t metaHeight {0};
        uint32_t metaSize {0};
        uint32_t metaAlign {0};
        uint32_t metaBlockWidth {0};
        uint32_t metaBlockHeight {0};
        uint32_t metaBlockDepth {0};
        uint32_t metaBlockSize {0};
        uint32_t capabilityBits {0};
        uint32_t capabilityField0 {0};
        uint32_t capabilityField1 {0};
    };

    // Applies the bounded observation policy, publishes counters, and emits one stable log record.
    void publishObservation(const char *stageName, const DCCObservationKey &key, const Snapshot &snapshot,
        bool failure);

    // Publishes the most recent complete snapshot without exposing pointers or GPU addresses.
    void publishSnapshot(const Snapshot &snapshot);

    IOPCIDevice *provider {nullptr};
    IOLock *lock {nullptr};
    DCCObservationCache cache {};
    bool enabled {false};
};
