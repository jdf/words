// Test entry point: Catch2 v3 main with ApprovalTests wiring.

#define APPROVALS_CATCH2_V3
#include <ApprovalTests.hpp>

#include <memory>

namespace {

// Approval mismatches write the .received file and fail the assertion —
// nothing more. The default reporter chain launches an installed diff tool
// (VS Code here), stealing window focus on every golden change; review
// received-vs-approved with jj diff or a browser instead.
auto quietReporter = ApprovalTests::Approvals::useAsDefaultReporter(
    std::make_shared<ApprovalTests::QuietReporter>());

}  // namespace
