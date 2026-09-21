#include "DCCDiagnostics.hpp"
#include <Headers/kern_api.hpp>
#include <IOKit/pci/IOPCIDevice.h>

void DCCDiagnostics::configure(IOPCIDevice *provider, bool enabled) {
    this->provider = provider;
    this->enabled = false;

    if (this->provider == nullptr) { return; }

    this->provider->setProperty("NootRX_DCCDiagEnabled", static_cast<UInt64>(0), 32);
    if (!enabled) { return; }

    this->lock = IOLockAlloc();
    if (this->lock == nullptr) {
        SYSLOG("NootRX", "DCCDIAG disabled because lock allocation failed");
        return;
    }

    this->enabled = true;
    this->provider->setProperty("NootRX_DCCDiagEnabled", static_cast<UInt64>(1), 32);
    this->provider->setProperty("NootRX_DCCDiagSequence", static_cast<UInt64>(0), 32);
    this->provider->setProperty("NootRX_DCCDiagDuplicateCount", static_cast<UInt64>(0), 32);
    this->provider->setProperty("NootRX_DCCDiagErrorCount", static_cast<UInt64>(0), 32);
    this->provider->setProperty("NootRX_DCCDiagOverflowCount", static_cast<UInt64>(0), 32);
    SYSLOG("NootRX", "DCCDIAG enabled for Tahoe RX 6750 XT");
}

bool DCCDiagnostics::isEnabled() const {
    return this->enabled && this->provider != nullptr && this->lock != nullptr;
}

void DCCDiagnostics::recordAddrLibIdentity(uint32_t returnCode, uint32_t chipEngine, uint32_t chipFamily,
    uint32_t chipRevision) {
    if (!this->isEnabled()) { return; }

    const DCCObservationKey key {DCCDiagnosticStage::AddrLibIdentity,
        {returnCode, chipEngine, chipFamily, chipRevision}, 4};
    Snapshot snapshot {};
    snapshot.stage = static_cast<uint32_t>(DCCDiagnosticStage::AddrLibIdentity);
    snapshot.returnCode = returnCode;
    snapshot.chipEngine = chipEngine;
    snapshot.chipFamily = chipFamily;
    snapshot.chipRevision = chipRevision;
    this->publishObservation("identity", key, snapshot, returnCode != 0);
}

void DCCDiagnostics::recordScanoutDecision(uint32_t width, uint32_t height, uint32_t candidateFlags,
    uint32_t pixelFormatSelector, bool result) {
    if (!this->isEnabled()) { return; }

    const DCCObservationKey key {DCCDiagnosticStage::ScanoutDecision,
        {width, height, candidateFlags, pixelFormatSelector, result ? 1U : 0U}, 5};
    Snapshot snapshot {};
    snapshot.stage = static_cast<uint32_t>(DCCDiagnosticStage::ScanoutDecision);
    snapshot.width = width;
    snapshot.height = height;
    snapshot.pixelFormat = pixelFormatSelector;
    snapshot.candidateFlags = candidateFlags;
    snapshot.decision = result ? 1U : 0U;
    this->publishObservation("scanout", key, snapshot, false);
}

void DCCDiagnostics::recordAddrLibDccInfo(uint32_t returnCode, const AppleAddr2ComputeDccInfoInputV1 *input,
    const AppleAddr2ComputeDccInfoOutputV1 *output) {
    if (!this->isEnabled()) { return; }

    Snapshot snapshot {};
    snapshot.stage = static_cast<uint32_t>(DCCDiagnosticStage::AddrLibDccInfo);
    snapshot.returnCode = returnCode;

    if (input == nullptr || output == nullptr) {
        const DCCObservationKey key {DCCDiagnosticStage::AddrLibDccInfo,
            {returnCode, input != nullptr ? 1U : 0U, output != nullptr ? 1U : 0U}, 3};
        this->publishObservation("addrlib-abi", key, snapshot, true);
        return;
    }

    if (input->size != sizeof(*input) || output->size != sizeof(*output)) {
        const DCCObservationKey key {DCCDiagnosticStage::AddrLibDccInfo,
            {returnCode, input->size, output->size}, 3};
        this->publishObservation("addrlib-abi", key, snapshot, true);
        return;
    }

    snapshot.width = input->unalignedWidth;
    snapshot.height = input->unalignedHeight;
    snapshot.swizzleMode = input->swizzleMode;
    snapshot.bpp = input->bpp;

    if (returnCode != 0) {
        const DCCObservationKey key {DCCDiagnosticStage::AddrLibDccInfo,
            {returnCode, input->swizzleMode, input->bpp, input->unalignedWidth, input->unalignedHeight,
                input->numSlices, input->numFrags, input->numMipLevels},
            8};
        this->publishObservation("addrlib", key, snapshot, true);
        return;
    }

    snapshot.metaPitch = output->pitch;
    snapshot.metaHeight = output->height;
    snapshot.metaSize = output->dccRamSize;
    snapshot.metaAlign = output->dccRamBaseAlign;
    snapshot.metaBlockWidth = output->metaBlkWidth;
    snapshot.metaBlockHeight = output->metaBlkHeight;
    snapshot.metaBlockDepth = output->metaBlkDepth;
    snapshot.metaBlockSize = output->metaBlkSize;
    const DCCObservationKey key {DCCDiagnosticStage::AddrLibDccInfo,
        {returnCode, input->swizzleMode, input->bpp, input->unalignedWidth, input->unalignedHeight,
            input->numSlices, input->numFrags, input->numMipLevels, output->dccRamBaseAlign, output->dccRamSize,
            output->pitch, output->height, output->metaBlkWidth, output->metaBlkHeight, output->metaBlkDepth,
            output->metaBlkSize},
        16};
    this->publishObservation("addrlib", key, snapshot, false);
}

void DCCDiagnostics::recordFramebufferCapability(uint32_t returnCode, const AppleDccCapsParametersV1 *input,
    const AppleDccCapabilitiesV1 *output) {
    if (!this->isEnabled()) { return; }

    Snapshot snapshot {};
    snapshot.stage = static_cast<uint32_t>(DCCDiagnosticStage::FramebufferCapability);
    snapshot.returnCode = returnCode;

    if (input == nullptr || output == nullptr) {
        const DCCObservationKey key {DCCDiagnosticStage::FramebufferCapability,
            {returnCode, input != nullptr ? 1U : 0U, output != nullptr ? 1U : 0U}, 3};
        this->publishObservation("framebuffer-abi", key, snapshot, true);
        return;
    }

    if (input->version != 1 || output->version != 1) {
        const DCCObservationKey key {DCCDiagnosticStage::FramebufferCapability,
            {returnCode, input->version, output->version}, 3};
        this->publishObservation("framebuffer-abi", key, snapshot, true);
        return;
    }

    snapshot.width = static_cast<uint32_t>(input->width);
    snapshot.height = static_cast<uint32_t>(input->height);
    snapshot.pixelFormat = input->pixelFormat;
    snapshot.candidateFlags = input->candidateFlags;

    if (returnCode != 0) {
        const DCCObservationKey key {DCCDiagnosticStage::FramebufferCapability,
            {returnCode, snapshot.width, snapshot.height, input->pixelFormat, input->candidateFlags, input->field0},
            6};
        this->publishObservation("framebuffer", key, snapshot, true);
        return;
    }

    snapshot.capabilityBits = static_cast<uint32_t>(output->capability0) |
                              (static_cast<uint32_t>(output->capability1) << 8U) |
                              (static_cast<uint32_t>(output->capability2) << 16U);
    snapshot.capabilityField0 = output->field0;
    snapshot.capabilityField1 = output->field1;
    const DCCObservationKey key {DCCDiagnosticStage::FramebufferCapability,
        {returnCode, snapshot.width, snapshot.height, input->pixelFormat, input->candidateFlags, input->field0,
            snapshot.capabilityBits, output->field0, output->field1},
        9};
    this->publishObservation("framebuffer", key, snapshot, false);
}

void DCCDiagnostics::publishObservation(const char *stageName, const DCCObservationKey &key,
    const Snapshot &snapshot, bool failure) {
    if (!this->isEnabled()) { return; }

    IOLockLock(this->lock);
    const auto decision = this->cache.observe(key, failure);
    const auto duplicateCount = this->cache.duplicateCount();
    const auto errorCount = this->cache.errorCount();
    const auto overflowCount = this->cache.overflowCount();
    IOLockUnlock(this->lock);

    this->provider->setProperty("NootRX_DCCDiagSequence", decision.sequence, 32);
    this->provider->setProperty("NootRX_DCCDiagDuplicateCount", duplicateCount, 32);
    this->provider->setProperty("NootRX_DCCDiagErrorCount", errorCount, 32);
    this->provider->setProperty("NootRX_DCCDiagOverflowCount", overflowCount, 32);
    if (!decision.shouldLog) { return; }

    this->publishSnapshot(snapshot);
    SYSLOG("NootRX",
        "DCCDIAG seq=%u stage=%s rc=0x%08X size=%ux%u fmt=0x%08X flags=0x%08X decision=%u "
        "asic=%u/%u/%u swizzle=%u bpp=%u meta=%u/%u/%u/%u block=%u/%u/%u/%u caps=0x%06X/%u/%u",
        decision.sequence, stageName, snapshot.returnCode, snapshot.width, snapshot.height, snapshot.pixelFormat,
        snapshot.candidateFlags, snapshot.decision, snapshot.chipEngine, snapshot.chipFamily, snapshot.chipRevision,
        snapshot.swizzleMode, snapshot.bpp, snapshot.metaPitch, snapshot.metaHeight, snapshot.metaSize,
        snapshot.metaAlign, snapshot.metaBlockWidth, snapshot.metaBlockHeight, snapshot.metaBlockDepth,
        snapshot.metaBlockSize, snapshot.capabilityBits, snapshot.capabilityField0, snapshot.capabilityField1);
}

void DCCDiagnostics::publishSnapshot(const Snapshot &snapshot) {
    this->provider->setProperty("NootRX_DCCDiagLastStage", snapshot.stage, 32);
    this->provider->setProperty("NootRX_DCCDiagLastReturnCode", snapshot.returnCode, 32);
    this->provider->setProperty("NootRX_DCCDiagLastWidth", snapshot.width, 32);
    this->provider->setProperty("NootRX_DCCDiagLastHeight", snapshot.height, 32);
    this->provider->setProperty("NootRX_DCCDiagLastPixelFormat", snapshot.pixelFormat, 32);
    this->provider->setProperty("NootRX_DCCDiagLastCandidateFlags", snapshot.candidateFlags, 32);
    this->provider->setProperty("NootRX_DCCDiagLastDecision", snapshot.decision, 32);
    this->provider->setProperty("NootRX_DCCDiagLastChipEngine", snapshot.chipEngine, 32);
    this->provider->setProperty("NootRX_DCCDiagLastChipFamily", snapshot.chipFamily, 32);
    this->provider->setProperty("NootRX_DCCDiagLastChipRevision", snapshot.chipRevision, 32);
    this->provider->setProperty("NootRX_DCCDiagLastSwizzleMode", snapshot.swizzleMode, 32);
    this->provider->setProperty("NootRX_DCCDiagLastBpp", snapshot.bpp, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaPitch", snapshot.metaPitch, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaHeight", snapshot.metaHeight, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaSize", snapshot.metaSize, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaAlign", snapshot.metaAlign, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaBlockWidth", snapshot.metaBlockWidth, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaBlockHeight", snapshot.metaBlockHeight, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaBlockDepth", snapshot.metaBlockDepth, 32);
    this->provider->setProperty("NootRX_DCCDiagLastMetaBlockSize", snapshot.metaBlockSize, 32);
    this->provider->setProperty("NootRX_DCCDiagLastCapabilityBits", snapshot.capabilityBits, 32);
    this->provider->setProperty("NootRX_DCCDiagLastCapabilityField0", snapshot.capabilityField0, 32);
    this->provider->setProperty("NootRX_DCCDiagLastCapabilityField1", snapshot.capabilityField1, 32);
}
