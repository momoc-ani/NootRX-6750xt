#pragma once

struct NootRXDiagnosticsMode {
    bool powerDiagnostics;
    bool dccDiagnostics;
};

namespace DiagnosticsModePolicy {

// Preserves the two explicit diagnostics requests without enabling either path implicitly.
inline NootRXDiagnosticsMode select(bool powerDiagnosticsRequested, bool dccDiagnosticsRequested) {
    return {powerDiagnosticsRequested, dccDiagnosticsRequested};
}

}    // namespace DiagnosticsModePolicy
