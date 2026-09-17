const auto *selectedCaps = DDICapabilityPolicy::select(donorEntry->caps, ddiCapsNavi2Universal,
    getKernelVersion() == KernelVersion::Tahoe, NootRXMain::callback->attributes.isNavi22());
NootRXMain::callback->publishDDICapabilitySelection("NootRX_DDICaps_X6000FB", donorDeviceId, donorEntry->caps);
