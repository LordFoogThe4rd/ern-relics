#pragma once

#include "ui/widgets.hpp"
#include "ui/ui_layout.hpp"
#include "app/editor_view.hpp"
#include <optional>

namespace ern {}

namespace ern::ui {
void draw_diagnostics(const EditorView &rt, std::optional<EditorAction> &action,
                      const Appearance &appearance, const EquippedRelics *equipped,
                      std::optional<std::size_t> selected, UiRect editor);
}
