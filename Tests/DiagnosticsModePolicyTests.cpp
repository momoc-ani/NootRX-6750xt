#include "../NootRX/DiagnosticsModePolicy.hpp"

#include <cassert>

// Verifies that neither diagnostics path is enabled without an explicit request.
static void testNoDiagnosticsRequestKeepsBothPathsDisabled() {
    const auto mode = DiagnosticsModePolicy::select(false, false);
    assert(!mode.powerDiagnostics);
    assert(!mode.dccDiagnostics);
}

// Verifies that the DCC-only request never enables full PowerPlay or DAL logging.
static void testDccRequestDoesNotEnablePowerDiagnostics() {
    const auto mode = DiagnosticsModePolicy::select(false, true);
    assert(!mode.powerDiagnostics);
    assert(mode.dccDiagnostics);
}

// Verifies that the Power-only request never installs DCC recorder routes implicitly.
static void testPowerRequestDoesNotEnableDccDiagnostics() {
    const auto mode = DiagnosticsModePolicy::select(true, false);
    assert(mode.powerDiagnostics);
    assert(!mode.dccDiagnostics);
}

// Verifies that short targeted captures may explicitly enable both independent paths.
static void testBothRequestsEnableBothIndependentPaths() {
    const auto mode = DiagnosticsModePolicy::select(true, true);
    assert(mode.powerDiagnostics);
    assert(mode.dccDiagnostics);
}

// Runs every dependency-free diagnostics mode isolation test.
int main() {
    testNoDiagnosticsRequestKeepsBothPathsDisabled();
    testDccRequestDoesNotEnablePowerDiagnostics();
    testPowerRequestDoesNotEnableDccDiagnostics();
    testBothRequestsEnableBothIndependentPaths();
    return 0;
}
