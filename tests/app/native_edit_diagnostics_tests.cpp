#include "app/native_edit_diagnostics.hpp"
#include "support/check.hpp"

using test::require;
namespace {
bool contains(const std::string &text, std::string_view value) {
    return text.find(value) != std::string::npos;
}
} // namespace

int main() {
    return test::run("native callback and controller diagnostics", [] {
        ern::NativeEditDiagnostics absent;
        require(
            contains(absent.summary(), "callback_entries=0 callback_returns=0 context_checks=0"),
            "A missing callback was not distinguishable from a controller rejection");

        ern::NativeEditDiagnostics trace;
        trace.entered();
        require(contains(trace.summary(), "callback_entries=1 callback_returns=0"),
                "An original callback that has not returned cannot be identified");
        trace.returned();
        const ern::RelicEquipContext context{{}, 0x1000, 0x6000, 0x7000, {}, {}, 0};
        trace.observe(0x2000, context);
        trace.observe(0x1000, context);
        const auto healthy = trace.summary();
        require(contains(healthy, "context_checks=2 context_rejections=0") &&
                    contains(healthy, "other_character_callbacks=1 matching_player_callbacks=1") &&
                    !contains(healthy, "first_rejection"),
                "Normal callbacks lost their counts or were reported as failures");
        trace.observe(0x1000, ern::RelicEquipFailure{"controller.entry_count", 0x6020, 6, 24});
        for (unsigned count = 0; count < 10000; ++count)
            trace.observe(0x1000, ern::RelicEquipFailure{"controller.pointer", 0x1650, {}, {}});
        const auto report = trace.summary();
        require(
            contains(report, "context_checks=10003 context_rejections=10001") &&
                contains(report, "other_character_callbacks=1 matching_player_callbacks=1") &&
                contains(
                    report,
                    "first_rejection={stage=callback check=controller.entry_count address=0x6020 actual=0x6 expected=0x18}") &&
                contains(
                    report,
                    "last_rejection={stage=callback check=controller.pointer address=0x1650 actual=unreadable_or_unavailable}") &&
                report.size() < 1024,
            "Repeated failures lost their reasons or produced an unbounded report");
        trace.reject("unequip", {"cached_handles", 0, 0xC0800002, 0xC0800001, 1});
        require(
            contains(
                trace.summary(),
                "stage=unequip check=cached_handles address=0x0 actual=0xc0800002 expected=0xc0800001 slot=2"),
            "Equip-cycle failure lost its stage, handles, or slot");
        require(contains(absent.summary(), "context_rejections=0") &&
                    !contains(absent.summary(), "first_rejection"),
                "One attempt's diagnostics leaked into another attempt");
    });
}
