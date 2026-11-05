#pragma once

#include <algorithm>

namespace ern {
// Logical spacing unit. Display scaling is applied by Appearance::space().
inline constexpr float spacing(int steps) { return 4.f * steps; }

struct UiRect {
    float x, y, width, height;
};

inline UiRect editor_bounds(float width, float height) {
    return {width / 2.f, 0.f, width / 2.f, height};
}

inline float editor_scale(float height) { return std::clamp(height / 1200.f, .75f, 1.5f); }

// Prefer below the field, then above it; clamp both axes to the editor, not the viewport.
inline UiRect effect_popup_bounds(UiRect editor, UiRect field, float desired_height,
                                  float padding) {
    padding = std::min(padding, std::min(editor.width, editor.height) / 4.f);
    const float left = editor.x + padding, top = editor.y + padding;
    const float right = editor.x + editor.width - padding,
                bottom = editor.y + editor.height - padding;
    const float width = std::min(field.width, right - left);
    const float height = std::min(desired_height, bottom - top);
    float y = field.y + field.height;
    if (y + height > bottom && field.y - height >= top)
        y = field.y - height;
    return {std::clamp(field.x, left, right - width), std::clamp(y, top, bottom - height), width,
            height};
}
} // namespace ern
