#include "app/editor_application.hpp"
#include "app/bootstrap.hpp"
#include "app/host_protocol.hpp"
#include <algorithm>

namespace {
using View = std::shared_ptr<const ern::EditorView>;
View request(ern::EditorAction action, View observed = {}) {
    auto &app = ern::application();
    const auto deadline = GetTickCount64() + 5000;
    while (app.pending() && GetTickCount64() < deadline)
        Sleep(1);
    if (!observed)
        observed = app.view();
    if (!observed)
        return {};
    const auto operation = app.submit(*observed, std::move(action));
    if (!operation)
        return {};
    while (GetTickCount64() < deadline) {
        auto current = app.view();
        if (!app.pending() && current && current->completion &&
            current->completion->operation == *operation)
            return current;
        Sleep(1);
    }
    return {};
}
const ern::EquippedRelics *equipped(const View &view) {
    return view ? std::get_if<ern::EquippedRelics>(&view->inspection) : nullptr;
}
View stage(std::size_t slot, const ern::Effects &effects, View state = {}) {
    if (!state)
        state = request(ern::DiscardDrafts{});
    for (const auto field : {ern::EffectField::Effect, ern::EffectField::Curse})
        for (std::size_t row = 0; state && row < effects.rows.size(); ++row) {
            state =
                request(ern::SetEffect{slot, {row, field}, ern::effect_at(effects, {row, field})});
            if (state && !std::holds_alternative<std::monostate>(state->completion->outcome))
                return {};
        }
    return state;
}
bool applied(const View &view, std::size_t count = 1) {
    if (!view || !view->completion)
        return false;
    const auto *result = std::get_if<ern::AppliedDrafts>(&view->completion->outcome);
    return result && !result->result.failure && result->result.applied == count;
}
} // namespace

extern "C" __declspec(dllexport) BOOL ern_test_fixture_edit_cycle() {
    if (!ern::test_host_attached())
        return FALSE;
    auto state = request(ern::RefreshInspection{});
    const auto *snapshot = equipped(state);
    if (!snapshot || !snapshot->slots[0])
        return FALSE;
    const auto before = *snapshot->slots[0];
    const auto captured = ern::capture_preset(*snapshot, state->validator(), state->context);
    if (!captured.slots[0] || captured.slots[1] || captured.slots[2])
        return FALSE;

    auto forbidden = before.effects;
    forbidden.rows[0].effect = 10000;
    if (const auto staged = stage(0, forbidden);
        !staged || applied(request(ern::ApplyDrafts{}, staged)))
        return FALSE;
    state = request(ern::RefreshInspection{});
    if (!equipped(state) || equipped(state)->slots[0] != before)
        return FALSE;
    for (std::size_t slot = 1; slot < 3; ++slot) {
        const auto locked = *equipped(state)->slots[slot];
        auto changed = locked.effects;
        changed.rows[0].effect = 7000400;
        if (stage(slot, changed))
            return FALSE;
        state = request(ern::RefreshInspection{});
        if (!equipped(state) || equipped(state)->slots[slot] != locked)
            return FALSE;
    }

    const auto set_location = reinterpret_cast<void (*)(ern::HostLocation)>(
        GetProcAddress(GetModuleHandleW(nullptr), "ern_host_set_location"));
    if (!set_location)
        return FALSE;
    const auto journal = state->journal_path;
    const auto journal_size = [&] {
        return std::filesystem::exists(journal) ? std::filesystem::file_size(journal) : 0;
    };
    const auto original_journal = journal_size();
    auto replacement = before.effects;
    replacement.rows[0].effect = 7000400;
    for (const auto location : {ern::HostLocation::Expedition, ern::HostLocation::Loading,
                                ern::HostLocation::Unavailable}) {
        // Submit exactly the reviewed view after the external game state changes.
        const auto reviewed = stage(0, replacement);
        if (!reviewed)
            return FALSE;
        set_location(location);
        state = request(ern::ApplyDrafts{}, reviewed);
        if (!state || applied(state) || state->editing_supported() ||
            journal_size() != original_journal)
            return FALSE;
        if (location == ern::HostLocation::Expedition
                ? !equipped(state) || equipped(state)->slots[0] != before
                : equipped(state) || state->can_undo ||
                      std::any_of(state->drafts.begin(), state->drafts.end(),
                                  [](const auto &draft) { return draft.has_value(); }))
            return FALSE;
        set_location(ern::HostLocation::Roundtable);
        state = request(ern::RefreshInspection{});
    }

    const auto reviewed = stage(0, replacement);
    if (!reviewed)
        return FALSE;
    state = request(ern::ApplyDrafts{}, reviewed);
    if (!applied(state) || !equipped(state) || !equipped(state)->slots[0] ||
        *equipped(state)->slots[0] == before || !state->can_undo)
        return FALSE;
    const auto after = *equipped(state)->slots[0];
    const auto edited_journal = journal_size();
    set_location(ern::HostLocation::Expedition);
    state = request(ern::UndoEdit{}, state);
    if (!state || !state->can_undo || journal_size() != edited_journal || !equipped(state) ||
        equipped(state)->slots[0] != after)
        return FALSE;
    const auto *undo = std::get_if<ern::UndoneEdit>(&state->completion->outcome);
    if (!undo || !undo->result.failure || undo->result.restored != 0)
        return FALSE;
    set_location(ern::HostLocation::Roundtable);
    state = request(ern::RefreshInspection{});
    if (!state)
        return FALSE;
    state = request(ern::UndoEdit{});
    if (!state || !equipped(state) || equipped(state)->slots[0] != before || state->can_undo)
        return FALSE;
    undo = std::get_if<ern::UndoneEdit>(&state->completion->outcome);
    if (!undo || undo->result.failure || undo->result.restored != 1 || !equipped(state)->slots[3])
        return FALSE;
    const auto deep_before = *equipped(state)->slots[3];
    auto deep_replacement = deep_before.effects;
    deep_replacement.rows[2].effect = 7001801;
    deep_replacement.rows[2].curse = 900002;
    state = stage(3, deep_replacement);
    if (!state || !applied(request(ern::ApplyDrafts{}, state)))
        return FALSE;
    state = request(ern::RefreshInspection{});
    if (!equipped(state) || equipped(state)->slots[3]->effects != deep_replacement ||
        !state->can_undo)
        return FALSE;
    state = request(ern::UndoEdit{}, state);
    if (!state || !equipped(state) || equipped(state)->slots[3] != deep_before || state->can_undo)
        return FALSE;

    // One Apply and one Undo must include both a normal and a Deep relic.
    state = stage(0, replacement);
    state = stage(3, deep_replacement, state);
    state = request(ern::ApplyDrafts{}, state);
    if (!applied(state, 2) || !equipped(state) ||
        equipped(state)->slots[0]->effects != replacement ||
        equipped(state)->slots[3]->effects != deep_replacement)
        return FALSE;
    state = request(ern::UndoEdit{}, state);
    if (!state || !equipped(state) || equipped(state)->slots[0] != before ||
        equipped(state)->slots[3] != deep_before || state->can_undo)
        return FALSE;
    undo = std::get_if<ern::UndoneEdit>(&state->completion->outcome);
    if (!undo || undo->result.failure || undo->result.restored != 2)
        return FALSE;

    state = stage(0, replacement);
    state = request(ern::ApplyDrafts{}, state);
    if (!applied(state) || !state->can_undo)
        return FALSE;
    state = stage(3, deep_replacement);
    if (!state || state->review.relic_count != 1)
        return FALSE;
    const auto before_unload_journal = journal_size();
    set_location(ern::HostLocation::Loading);
    state = request(ern::RefreshInspection{});
    if (!state || equipped(state) || state->can_undo || state->review.relic_count != 0)
        return FALSE;
    set_location(ern::HostLocation::Roundtable);
    state = request(ern::RefreshInspection{});
    return state && equipped(state) && equipped(state)->slots[0]->effects == replacement &&
           equipped(state)->slots[3] == deep_before && !state->can_undo &&
           state->review.relic_count == 0 && journal_size() == before_unload_journal;
}
