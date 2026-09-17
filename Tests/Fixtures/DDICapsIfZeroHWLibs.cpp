#if 0
selectedCaps = DDICapabilityPolicy::select(orgCapsTable->caps, ddiCapsNavi2Universal,
    getKernelVersion() == KernelVersion::Tahoe, NootRXMain::callback->attributes.isNavi22());
orgCapsTable->caps = selectedCaps;
.caps = orgCapsTable->caps,
NootRXMain::callback->publishDDICapabilitySelection("NootRX_DDICaps_HWLibs", targetDeviceId,
    orgCapsTable->caps);
#endif
