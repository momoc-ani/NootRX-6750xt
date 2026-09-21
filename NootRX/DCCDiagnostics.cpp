#include "DCCDiagnostics.hpp"
#include <Headers/kern_api.hpp>
#include <IOKit/pci/IOPCIDevice.h>
#include <libkern/c++/OSDictionary.h>
#include <libkern/c++/OSNumber.h>
#include <string.h>

void DCCDiagnostics::configure(IOPCIDevice *provider, bool enabled) {
    this->provider = provider;
    this->enabled = false;
    this->routeMask = 0;
    this->failureCode = DCCDiagnosticFailureCode::None;

    if (this->provider == nullptr) { return; }

    this->provider->setProperty("NootRX_DCCDiagEnabled", static_cast<UInt64>(0), 32);
    this->provider->setProperty("NootRX_DCCDiagRouteMask", static_cast<UInt64>(0), 32);
    this->provider->setProperty("NootRX_DCCDiagFailureCode", static_cast<UInt64>(0), 32);
    if (!enabled) { return; }

    this->lock = IOLockAlloc();
    if (this->lock == nullptr) {
        this->failureCode = DCCDiagnosticFailureCode::LockAllocation;
        this->provider->setProperty("NootRX_DCCDiagFailureCode",
            static_cast<UInt64>(this->failureCode), 32);
        SYSLOG("NootRX", "DCCDIAG disabled because lock allocation failed");
        return;
    }

    this->enabled = true;
    this->provider->setProperty("NootRX_DCCDiagEnabled", static_cast<UInt64>(1), 32);
    SYSLOG("NootRX", "DCCDIAG enabled for Tahoe 25E253 RX 6750 XT");
}

bool DCCDiagnostics::isEnabled() const {
    return this->enabled && this->provider != nullptr && this->lock != nullptr;
}

void DCCDiagnostics::disable(DCCDiagnosticFailureCode code, const char *reason) {
    if (this->provider == nullptr) { return; }

    if (this->lock != nullptr) { IOLockLock(this->lock); }
    this->enabled = false;
    this->failureCode = code;
    this->provider->setProperty("NootRX_DCCDiagEnabled", static_cast<UInt64>(0), 32);
    this->provider->setProperty("NootRX_DCCDiagFailureCode", static_cast<UInt64>(code), 32);
    if (this->lock != nullptr) { IOLockUnlock(this->lock); }

    SYSLOG("NootRX", "DCCDIAG disabled code=%u reason=%s", static_cast<uint32_t>(code), safeString(reason));
}

void DCCDiagnostics::markRouteReady(uint32_t route) {
    if (!this->isEnabled()) { return; }

    IOLockLock(this->lock);
    if (this->enabled) {
        this->routeMask |= route;
        this->provider->setProperty("NootRX_DCCDiagRouteMask", this->routeMask, 32);
    }
    IOLockUnlock(this->lock);
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

    snapshot.inputSize = input->size;
    snapshot.outputSize = output->size;
    if (input->size != sizeof(*input) || output->size != sizeof(*output)) {
        const DCCObservationKey key {DCCDiagnosticStage::AddrLibDccInfo,
            {returnCode, input->size, output->size}, 3};
        this->publishObservation("addrlib-abi", key, snapshot, true);
        return;
    }

    snapshot.width = input->unalignedWidth;
    snapshot.height = input->unalignedHeight;
    snapshot.dccKeyFlags = input->dccKeyFlags;
    snapshot.colorFlags = input->colorFlags;
    snapshot.resourceType = input->resourceType;
    snapshot.swizzleMode = input->swizzleMode;
    snapshot.bpp = input->bpp;
    snapshot.numSlices = input->numSlices;
    snapshot.numFrags = input->numFrags;
    snapshot.numMipLevels = input->numMipLevels;
    snapshot.dataSurfaceSize = input->dataSurfaceSize;
    snapshot.firstMipIdInTail = input->firstMipIdInTail;

    if (returnCode != 0) {
        const DCCObservationKey key {DCCDiagnosticStage::AddrLibDccInfo,
            {returnCode, input->size, output->size, input->dccKeyFlags, input->colorFlags, input->resourceType,
                input->swizzleMode, input->bpp, input->unalignedWidth, input->unalignedHeight, input->numSlices,
                input->numFrags, input->numMipLevels, input->dataSurfaceSize, input->firstMipIdInTail},
            15};
        this->publishObservation("addrlib", key, snapshot, true);
        return;
    }

    snapshot.metaPitch = output->pitch;
    snapshot.metaHeight = output->height;
    snapshot.metaDepth = output->depth;
    snapshot.metaSize = output->dccRamSize;
    snapshot.metaAlign = output->dccRamBaseAlign;
    snapshot.compressBlockWidth = output->compressBlkWidth;
    snapshot.compressBlockHeight = output->compressBlkHeight;
    snapshot.compressBlockDepth = output->compressBlkDepth;
    snapshot.metaBlockWidth = output->metaBlkWidth;
    snapshot.metaBlockHeight = output->metaBlkHeight;
    snapshot.metaBlockDepth = output->metaBlkDepth;
    snapshot.metaBlockSize = output->metaBlkSize;
    snapshot.metaBlockNumPerSlice = output->metaBlkNumPerSlice;
    snapshot.dccRamSliceSize = output->dccRamSliceSize;
    snapshot.alignmentPadding = output->alignmentPadding;
    snapshot.mipInfoPresent = output->pMipInfo != nullptr ? 1U : 0U;
    const DCCObservationKey key {DCCDiagnosticStage::AddrLibDccInfo,
        {returnCode, input->size, output->size, input->dccKeyFlags, input->colorFlags, input->resourceType,
            input->swizzleMode, input->bpp, input->unalignedWidth, input->unalignedHeight, input->numSlices,
            input->numFrags, input->numMipLevels, input->dataSurfaceSize, input->firstMipIdInTail,
            output->dccRamBaseAlign, output->dccRamSize, output->pitch, output->height, output->depth,
            output->compressBlkWidth, output->compressBlkHeight, output->compressBlkDepth, output->metaBlkWidth,
            output->metaBlkHeight, output->metaBlkDepth, output->metaBlkSize, output->metaBlkNumPerSlice,
            output->dccRamSliceSize, output->alignmentPadding, snapshot.mipInfoPresent},
        31};
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

    snapshot.inputVersion = input->version;
    snapshot.outputVersion = output->version;
    if (input->version != 1 || output->version != 1) {
        const DCCObservationKey key {DCCDiagnosticStage::FramebufferCapability,
            {returnCode, input->version, output->version}, 3};
        this->publishObservation("framebuffer-abi", key, snapshot, true);
        return;
    }

    memcpy(&snapshot.widthBits, &input->width, sizeof(snapshot.widthBits));
    memcpy(&snapshot.heightBits, &input->height, sizeof(snapshot.heightBits));
    snapshot.pixelFormat = input->pixelFormat;
    snapshot.candidateFlags = input->candidateFlags;

    if (returnCode != 0) {
        const DCCObservationKey key {DCCDiagnosticStage::FramebufferCapability,
            {returnCode, input->version, output->version, snapshot.widthBits, snapshot.heightBits,
                input->pixelFormat, input->candidateFlags, input->field0},
            8};
        this->publishObservation("framebuffer", key, snapshot, true);
        return;
    }

    snapshot.capabilityBits = static_cast<uint32_t>(output->capability0) |
                              (static_cast<uint32_t>(output->capability1) << 8U) |
                              (static_cast<uint32_t>(output->capability2) << 16U);
    snapshot.capabilityField0 = output->field0;
    snapshot.capabilityField1 = output->field1;
    const DCCObservationKey key {DCCDiagnosticStage::FramebufferCapability,
        {returnCode, input->version, output->version, snapshot.widthBits, snapshot.heightBits, input->pixelFormat,
            input->candidateFlags, input->field0, snapshot.capabilityBits, output->field0, output->field1},
        11};
    this->publishObservation("framebuffer", key, snapshot, false);
}

void DCCDiagnostics::publishObservation(const char *stageName, const DCCObservationKey &key,
    const Snapshot &snapshot, bool failure) {
    if (!this->isEnabled()) { return; }

    IOLockLock(this->lock);
    if (!this->enabled) {
        IOLockUnlock(this->lock);
        return;
    }

    const auto decision = this->cache.observe(key, failure);
    const auto duplicateCount = this->cache.duplicateCount();
    const auto errorCount = this->cache.errorCount();
    const auto overflowCount = this->cache.overflowCount();
    if (!decision.shouldPublish) {
        IOLockUnlock(this->lock);
        return;
    }

    if (!this->publishSnapshotLocked(snapshot, decision, duplicateCount, errorCount, overflowCount)) {
        SYSLOG("NootRX", "DCCDIAG snapshot allocation failed seq=%u stage=%s", decision.sequence,
            safeString(stageName));
    }
    if (decision.shouldLog) {
        SYSLOG("NootRX",
            "DCCDIAG seq=%u stage=%s repeat=%u overflow=%u rc=0x%08X size=%ux%u bits=0x%08X/0x%08X "
            "fmt=0x%08X flags=0x%08X decision=%u asic=%u/%u/%u input=0x%X/0x%X/%u swizzle=%u bpp=%u "
            "meta=%u/%u/%u/%u/%u compress=%u/%u/%u block=%u/%u/%u/%u/%u slice=%u pad=%u mip=%u "
            "caps=0x%06X/%u/%u totals=%u/%u/%u",
            decision.sequence, stageName, decision.repeatCount, decision.overflow ? 1U : 0U, snapshot.returnCode,
            snapshot.width, snapshot.height, snapshot.widthBits, snapshot.heightBits, snapshot.pixelFormat,
            snapshot.candidateFlags, snapshot.decision, snapshot.chipEngine, snapshot.chipFamily,
            snapshot.chipRevision, snapshot.dccKeyFlags, snapshot.colorFlags, snapshot.resourceType,
            snapshot.swizzleMode, snapshot.bpp, snapshot.metaPitch, snapshot.metaHeight, snapshot.metaDepth,
            snapshot.metaSize, snapshot.metaAlign, snapshot.compressBlockWidth, snapshot.compressBlockHeight,
            snapshot.compressBlockDepth, snapshot.metaBlockWidth, snapshot.metaBlockHeight,
            snapshot.metaBlockDepth, snapshot.metaBlockSize, snapshot.metaBlockNumPerSlice,
            snapshot.dccRamSliceSize, snapshot.alignmentPadding, snapshot.mipInfoPresent, snapshot.capabilityBits,
            snapshot.capabilityField0, snapshot.capabilityField1, duplicateCount, errorCount, overflowCount);
    }
    IOLockUnlock(this->lock);
}

bool DCCDiagnostics::publishSnapshotLocked(const Snapshot &snapshot, const DCCObservationDecision &decision,
    uint32_t duplicateCount, uint32_t errorCount, uint32_t overflowCount) {
    auto *dictionary = OSDictionary::withCapacity(64);
    if (dictionary == nullptr) { return false; }

    struct SnapshotField {
        const char *name;
        uint32_t value;
    };
    const SnapshotField fields[] = {
        {"Sequence", decision.sequence},
        {"RepeatCount", decision.repeatCount},
        {"Overflow", decision.overflow ? 1U : 0U},
        {"DuplicateCount", duplicateCount},
        {"ErrorCount", errorCount},
        {"OverflowCount", overflowCount},
        {"RouteMask", this->routeMask},
        {"FailureCode", static_cast<uint32_t>(this->failureCode)},
        {"Stage", snapshot.stage},
        {"ReturnCode", snapshot.returnCode},
        {"Width", snapshot.width},
        {"Height", snapshot.height},
        {"WidthBits", snapshot.widthBits},
        {"HeightBits", snapshot.heightBits},
        {"PixelFormat", snapshot.pixelFormat},
        {"CandidateFlags", snapshot.candidateFlags},
        {"Decision", snapshot.decision},
        {"ChipEngine", snapshot.chipEngine},
        {"ChipFamily", snapshot.chipFamily},
        {"ChipRevision", snapshot.chipRevision},
        {"InputSize", snapshot.inputSize},
        {"OutputSize", snapshot.outputSize},
        {"InputVersion", snapshot.inputVersion},
        {"OutputVersion", snapshot.outputVersion},
        {"DccKeyFlags", snapshot.dccKeyFlags},
        {"ColorFlags", snapshot.colorFlags},
        {"ResourceType", snapshot.resourceType},
        {"SwizzleMode", snapshot.swizzleMode},
        {"Bpp", snapshot.bpp},
        {"NumSlices", snapshot.numSlices},
        {"NumFrags", snapshot.numFrags},
        {"NumMipLevels", snapshot.numMipLevels},
        {"DataSurfaceSize", snapshot.dataSurfaceSize},
        {"FirstMipIdInTail", snapshot.firstMipIdInTail},
        {"MetaPitch", snapshot.metaPitch},
        {"MetaHeight", snapshot.metaHeight},
        {"MetaDepth", snapshot.metaDepth},
        {"MetaSize", snapshot.metaSize},
        {"MetaAlign", snapshot.metaAlign},
        {"CompressBlockWidth", snapshot.compressBlockWidth},
        {"CompressBlockHeight", snapshot.compressBlockHeight},
        {"CompressBlockDepth", snapshot.compressBlockDepth},
        {"MetaBlockWidth", snapshot.metaBlockWidth},
        {"MetaBlockHeight", snapshot.metaBlockHeight},
        {"MetaBlockDepth", snapshot.metaBlockDepth},
        {"MetaBlockSize", snapshot.metaBlockSize},
        {"MetaBlockNumPerSlice", snapshot.metaBlockNumPerSlice},
        {"DccRamSliceSize", snapshot.dccRamSliceSize},
        {"AlignmentPadding", snapshot.alignmentPadding},
        {"MipInfoPresent", snapshot.mipInfoPresent},
        {"CapabilityBits", snapshot.capabilityBits},
        {"CapabilityField0", snapshot.capabilityField0},
        {"CapabilityField1", snapshot.capabilityField1},
    };

    for (const auto &field : fields) {
        if (!setDictionaryNumber(dictionary, field.name, field.value)) {
            dictionary->release();
            return false;
        }
    }

    const auto published = this->provider->setProperty("NootRX_DCCDiagSnapshot", dictionary);
    dictionary->release();
    return published;
}

bool DCCDiagnostics::setDictionaryNumber(OSDictionary *dictionary, const char *name, uint32_t value) {
    if (dictionary == nullptr || name == nullptr) { return false; }

    auto *number = OSNumber::withNumber(value, 32);
    if (number == nullptr) { return false; }
    const auto inserted = dictionary->setObject(name, number);
    number->release();
    return inserted;
}
