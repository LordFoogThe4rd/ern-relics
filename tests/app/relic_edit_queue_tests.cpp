#include "app/relic_edit_queue.hpp"
#include "app/native_edit_diagnostics.hpp"
#include "support/check.hpp"
#include "support/synthetic_rules.hpp"
#include <future>
#include <string_view>
#include <thread>

using namespace std::chrono_literals;
using test::require;
namespace {
ern::RelicEditRequest request() {
    ern::RelicEditRequest edit{
        {0x10000, 0x10008, 0x10010},
        0x2F4,
        {0x10100, 0x10300, 0x11000},
        0,
        {0xC0800001,
         0x80000066,
         {{7000401, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}}},
        {{7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}},
        {},
        ern::DlcState::Unknown};
    edit.rules = std::make_shared<const ern::RelicValidator>(synthetic_rules());
    return edit;
}
void wait_pending(const ern::RelicEditQueue &queue) {
    const auto deadline = std::chrono::steady_clock::now() + 1s;
    while (!queue.pending_roots()) {
        require(std::chrono::steady_clock::now() < deadline, "Edit was never queued");
        std::this_thread::sleep_for(1ms);
    }
}
ern::EditResult success(ern::RelicEditRequest edit) {
    return ern::Relic{edit.expected.handle, edit.expected.item_id, edit.replacement};
}

void native_capacity_rejection() {
    ern::RelicEditQueue queue;
    ern::NativeEditDiagnostics diagnostics;
    const auto edit = request();
    unsigned int reads = 0;
    const auto read_context = [&](ern::RelicRoots roots) -> ern::RelicEquipReadResult {
        require(roots.game_data_global == edit.roots.game_data_global,
                "Native preflight lost the queued roots");
        ++reads;
        return ern::RelicEquipFailure{"controller.entry_count", 0x6020, 24, 48, {}, true};
    };
    const auto callback_error = [] { return std::optional<std::string>{}; };
    const auto result = queue.submit_native(edit, read_context, callback_error, diagnostics, 0ms);
    const auto &rejected = std::get<ern::EditRejected>(result);
    bool executed = false;
    queue.process([&](ern::RelicEditRequest pending) {
        executed = true;
        return success(std::move(pending));
    });
    require(
        reads == 1 && rejected.reason == ern::EditRejectReason::EquipUnavailable &&
            rejected.detail ==
                "Relic controller check failed (controller.entry_count). Found 24. Expected 48." &&
            diagnostics.summary().find("stage=preflight check=controller.entry_count") !=
                std::string::npos &&
            !queue.pending_roots() && !executed,
        "A native capacity mismatch timed out or left an edit available to the callback");
}

void availability_exception_cancels() {
    for (const unsigned int throw_on : {1u, 2u}) {
        ern::RelicEditQueue queue;
        unsigned int checks = 0;
        bool propagated = false;
        try {
            queue.submit(request(), 1s, [&]() -> std::optional<std::string> {
                if (++checks == throw_on)
                    throw std::runtime_error("Availability check failed");
                return std::nullopt;
            });
        } catch (const std::runtime_error &error) {
            propagated = std::string_view(error.what()) == "Availability check failed";
        }
        unsigned int late_writes = 0;
        queue.process([&](ern::RelicEditRequest edit) {
            ++late_writes;
            return success(std::move(edit));
        });
        require(propagated && checks == throw_on && !queue.pending_roots() && !late_writes,
                "An availability exception left a request executable after submit failed");

        auto retry = std::async(std::launch::async, [&] { return queue.submit(request()); });
        wait_pending(queue);
        queue.process(success);
        require(std::holds_alternative<ern::Relic>(retry.get()) && !queue.pending_roots(),
                "An availability exception prevented a subsequent edit from completing");
    }
}
} // namespace

int main() {
    return test::run("owned edit handoff, cancellation and executing deadlines", [] {
        native_capacity_rejection();
        availability_exception_cancels();
        ern::RelicEditQueue queue;
        auto input = request();
        auto first =
            std::async(std::launch::async, [&queue, input] { return queue.submit(input); });
        wait_pending(queue);
        input.replacement.rows[0].effect = 7000402;
        input.rules.reset();
        const auto busy = queue.submit(request(), 1ms);
        require(std::get<ern::EditRejected>(busy).reason == ern::EditRejectReason::EquipUnavailable,
                "A second edit replaced a pending request");
        const auto processor = std::this_thread::get_id();
        queue.process([&](ern::RelicEditRequest edit) {
            require(std::this_thread::get_id() == processor && edit.rules &&
                        edit.replacement.rows[0].effect == 7000400,
                    "The callback ran elsewhere or used mutable caller inputs");
            return success(std::move(edit));
        });
        const auto after = std::get<ern::Relic>(first.get());
        require(after.effects.rows[0].effect == 7000400 && after.handle == 0xC0800001 &&
                    !queue.pending_roots(),
                "The caller did not receive its executed edit");

        const auto timed_out = queue.submit(request(), 5ms);
        unsigned late_writes = 0;
        queue.process([&](ern::RelicEditRequest edit) {
            ++late_writes;
            return success(std::move(edit));
        });
        require(std::get<ern::EditRejected>(timed_out).reason ==
                        ern::EditRejectReason::EquipUnavailable &&
                    !late_writes && !queue.pending_roots(),
                "A cancelled pending request executed after timeout");

        std::atomic<bool> callback_lost = false;
        const ern::EditAvailability availability = [&]() -> std::optional<std::string> {
            return callback_lost ? std::optional<std::string>{"Equipment callback was replaced"}
                                 : std::nullopt;
        };
        auto lost = std::async(std::launch::async,
                               [&] { return queue.submit(request(), 5s, availability); });
        wait_pending(queue);
        callback_lost = true;
        const auto detected = lost.wait_for(1s) == std::future_status::ready;
        const auto rejected = lost.get();
        queue.process([&](ern::RelicEditRequest edit) {
            ++late_writes;
            return success(std::move(edit));
        });
        require(detected && !late_writes && !queue.pending_roots() &&
                    std::get<ern::EditRejected>(rejected).detail ==
                        "Equipment callback was replaced",
                "A lost callback became a generic timeout or allowed a late write");
        callback_lost = false;

        auto executing = std::async(std::launch::async,
                                    [&] { return queue.submit(request(), 100ms, availability); });
        wait_pending(queue);
        std::promise<void> entered, release;
        auto released = release.get_future();
        auto game = std::async(std::launch::async, [&] {
            queue.process([&](ern::RelicEditRequest edit) {
                entered.set_value();
                released.wait();
                return success(std::move(edit));
            });
        });
        entered.get_future().wait();
        callback_lost = true;
        const auto still_waiting = executing.wait_for(150ms) == std::future_status::timeout;
        release.set_value();
        game.get();
        require(still_waiting && std::holds_alternative<ern::Relic>(executing.get()),
                "An executing edit returned a timeout that could authorize a duplicate retry");

        auto throwing = std::async(std::launch::async, [&] { return queue.submit(request()); });
        wait_pending(queue);
        queue.process([](ern::RelicEditRequest) -> ern::EditResult {
            throw std::runtime_error("Failure after a possible native effect change");
        });
        const auto failed = std::get<ern::WriteFailure>(throwing.get());
        require(failed.stage == ern::WriteStage::Equipment &&
                    failed.restoration == ern::Restoration::Unconfirmed,
                "A callback exception became a clean rejection or left the caller waiting");
    });
}
