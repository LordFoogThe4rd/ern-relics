#pragma once

#include "ui/effect_choices.hpp"
#include "relics/types.hpp"
#include "relics/relic_validator.hpp"
#include "relics/relic_drafts.hpp"
#include "ui/widgets.hpp"
#include "ui/ui_layout.hpp"
#include "app/editor_view.hpp"

namespace ern::ui {
struct EditorState {
    std::size_t selected = 0;
    EffectPosition selected_effect;
    Notice status;
    ImGuiTextFilter effect_filter;
    bool show_unavailable = false;
    bool invalid = false;
    std::optional<EffectGroup> group_filter;
    std::optional<std::uint32_t> applied_focus;
    EffectChoiceCache choice_cache;
};

void draw_editor(const EditorView &rt, std::optional<EditorAction> &action, EditorState &state,
                 const Appearance &appearance, const EquippedRelics *equipped, UiRect editor);
} // namespace ern::ui
