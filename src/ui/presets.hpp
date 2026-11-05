#pragma once

#include "relics/preset.hpp"
#include "ui/editor.hpp"
#include "data/preset_types.hpp"
#include <vector>

namespace ern::ui {
struct PresetBrowser {
    char name[33]{};
    std::string pending_delete;
    std::string pending_replace;
    Notice feedback;
    std::string selected;
    ImGuiTextFilter filter;
};

void draw_presets(const EditorView &rt, std::optional<EditorAction> &action, PresetBrowser &browser,
                  const Appearance &appearance, const EquippedRelics *equipped, UiRect editor);
} // namespace ern::ui
