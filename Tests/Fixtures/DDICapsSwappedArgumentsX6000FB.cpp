const auto *selectedCaps = DDICapabilityPolicy::select(ddiCapsNavi2Universal, donorEntry->caps,
    getKernelVersion() == KernelVersion::Tahoe, NootRXMain::callback->attributes.isNavi22());
donorEntry->caps = selectedCaps;
NootRXMain::callback->publishDDICapabilitySelection("NootRX_DDICaps_X6000FB", donorDeviceId, donorEntry->caps);
