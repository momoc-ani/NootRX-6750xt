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
