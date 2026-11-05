#include "app/native_edit_diagnostics.hpp"
#include <sstream>

namespace ern {
void NativeEditDiagnostics::remember(Rejection rejection) {
    if (!first_)
        first_ = rejection;
    last_ = rejection;
}

void NativeEditDiagnostics::observe(Address character, const RelicEquipReadResult &observed) {
    std::lock_guard lock(mutex_);
    ++checks_;
    if (const auto *failure = std::get_if<RelicEquipFailure>(&observed)) {
        ++invalid_;
        remember({"callback", *failure});
        return;
    }
    const auto &context = std::get<RelicEquipContext>(observed);
    if (context.character != character) {
        ++other_;
        return;
    }
    ++matched_;
}

void NativeEditDiagnostics::reject(std::string_view stage, RelicEquipFailure failure) {
    std::lock_guard lock(mutex_);
    remember({stage, failure});
}

std::string NativeEditDiagnostics::summary() const {
    std::lock_guard lock(mutex_);
    std::ostringstream text;
    text << "callback_entries=" << entered_.load() << " callback_returns=" << returned_.load()
         << " context_checks=" << checks_ << " context_rejections=" << invalid_
         << " other_character_callbacks=" << other_ << " matching_player_callbacks=" << matched_;
    const auto append = [&](std::string_view label, const Rejection &rejection) {
        const auto &failure = rejection.failure;
        text << ' ' << label << "={stage=" << rejection.stage << " check=" << failure.check
             << " address=0x" << std::hex << failure.address << " actual=";
        if (failure.actual)
            text << "0x" << *failure.actual;
        else
            text << "unreadable_or_unavailable";
        if (failure.expected)
            text << " expected=0x" << *failure.expected;
        text << std::dec;
        if (failure.slot)
            text << " slot=" << *failure.slot + 1;
        text << '}';
    };
    if (first_)
        append("first_rejection", *first_);
    if (last_)
        append("last_rejection", *last_);
    return text.str();
}
} // namespace ern
