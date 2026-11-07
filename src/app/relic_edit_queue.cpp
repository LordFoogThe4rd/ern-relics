#include "app/relic_edit_queue.hpp"
#include "app/native_edit_diagnostics.hpp"
#include <algorithm>

namespace ern {
EditResult RelicEditQueue::submit_native(RelicEditRequest request,
                                         const ReadEquipContext &read_context,
                                         const EditAvailability &callback_error,
                                         NativeEditDiagnostics &diagnostics,
                                         std::chrono::milliseconds timeout) {
    const auto availability = [&, roots = request.roots] {
        if (auto error = callback_error())
            return error;
        const auto observed = read_context(roots);
        const auto *failure = std::get_if<RelicEquipFailure>(&observed);
        if (!failure)
            return std::optional<std::string>{};
        auto error = relic_capacity_error(*failure);
        if (error)
            diagnostics.reject("preflight", *failure);
        return error;
    };
    return submit(std::move(request), timeout, availability);
}

EditResult RelicEditQueue::submit(RelicEditRequest request, std::chrono::milliseconds timeout,
                                  const EditAvailability &availability) {
    const auto message = std::make_shared<Message>(Message{std::move(request)});
    std::unique_lock lock(mutex_);
    if (current_)
        return EditRejected{EditRejectReason::EquipUnavailable,
                            "Another relic update is still finishing."};
    current_ = message;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::holds_alternative<Waiting>(message->state)) {
        if (availability) {
            try {
                if (const auto error = availability()) {
                    current_.reset();
                    return EditRejected{EditRejectReason::EquipUnavailable, *error};
                }
            } catch (...) {
                // The mutex keeps this request Waiting until the check returns.
                // A failed check must not leave an edit available for a late callback.
                current_.reset();
                throw;
            }
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            current_.reset();
            return EditRejected{
                EditRejectReason::EquipUnavailable,
                "The relic update timed out before it started. No effects were changed. See ern-relics.log for details."};
        }
        changed_.wait_until(lock, std::min(deadline, now + std::chrono::milliseconds(25)),
                            [&] { return !std::holds_alternative<Waiting>(message->state); });
    }
    // An executing transaction must finish before its caller can retry or publish Undo.
    changed_.wait(lock, [&] { return std::holds_alternative<EditResult>(message->state); });
    auto result = std::move(std::get<EditResult>(message->state));
    current_.reset();
    return result;
}

std::optional<RelicRoots> RelicEditQueue::pending_roots() const {
    std::lock_guard lock(mutex_);
    if (current_ && std::holds_alternative<Waiting>(current_->state))
        return current_->request.roots;
    return std::nullopt;
}

void RelicEditQueue::process(const RelicCommit &commit) {
    std::shared_ptr<Message> message;
    {
        std::lock_guard lock(mutex_);
        if (!current_ || !std::holds_alternative<Waiting>(current_->state))
            return;
        message = current_;
        message->state = Executing{};
    }
    EditResult result;
    try {
        result = commit(std::move(message->request));
    } catch (...) {
        // A callback may throw after changing memory or active effects. Do not report rejection.
        result = WriteFailure{WriteStage::Equipment, Restoration::Unconfirmed};
    }
    {
        std::lock_guard lock(mutex_);
        message->state = std::move(result);
    }
    changed_.notify_all();
}
} // namespace ern
