#pragma once

#include <imgui.h>
#include "ui/ui_layout.hpp"
#include <cstdint>
#include <cmath>
#include <string>
#include <vector>

namespace ern {
struct RelicInfo;
struct GameAssets;
struct SystemFonts;
class CatalogPresentation;
} // namespace ern

namespace ern::ui {
class Appearance;
inline ImVec4 rgb(int r, int g, int b, float a = 1.f) {
    return {r / 255.f, g / 255.f, b / 255.f, a};
}
inline const ImVec4 ivory = rgb(225, 225, 218), muted = rgb(161, 171, 189),
                    ice = rgb(168, 199, 242), amber = rgb(219, 189, 132),
                    removed = rgb(226, 157, 152), added = rgb(163, 216, 186);

enum class NoticeKind { Info, Success, Warning, Error };
enum class NoticeAction { None, ReviewSlot, ReloadSlot };
inline const ImVec4 colors[]{rgb(185, 83, 75), rgb(103, 150, 207), rgb(211, 180, 102),
                             rgb(91, 173, 145)};

struct Notice {
    std::string text;
    ImVec4 color = muted;
    std::string title;
    NoticeKind kind = NoticeKind::Info;
    NoticeAction action = NoticeAction::None;
    std::size_t slot = 0;
    bool dismissible = true;
};

Notice feedback(std::string title, std::string body, NoticeKind kind);
Notice read_only_notice(const char *reason);
float notification_space(const Appearance &appearance);
NoticeAction notification(Notice &message, const Appearance &appearance, UiRect bounds);

class Appearance {
  public:
    void initialize(const GameAssets &assets, const SystemFonts &fonts, float display_height);
    // Keep scaled layout edges on whole pixels, including at fractional display scales.
    float px(float value) const { return std::round(value * scale); }
    float space(int steps) const { return px(spacing(steps)); }
    void portrait(const CatalogPresentation &catalog, const RelicInfo *info, ImVec2 position,
                  float size, bool faded = false) const;
    void empty_portrait(const CatalogPresentation &catalog, bool deep, ImVec2 position,
                        float size) const;
    void effect_icon(const CatalogPresentation &catalog, std::uint32_t id, ImVec2 position,
                     float size, bool faded = false) const;
    void effect_line(const CatalogPresentation &catalog, std::uint32_t id, bool wrap = true) const;
    void effect_pair(const CatalogPresentation &catalog, std::uint32_t effect, std::uint32_t curse,
                     bool deep, bool wrap = true) const;
    void lock_icon(ImVec2 position, float size) const;
    ImVec2 badge_size(const char *text) const;
    // Draw a tag and report hover over the tag itself, without changing the row's hit target.
    bool badge(const char *text, ImVec2 position, ImVec4 color) const;
    bool trailing_badge(const char *text, UiRect row, ImVec4 color, int inset = 2) const;

    ImFont *title_font = nullptr;
    ImFont *name_font = nullptr;
    ImFont *relic_font = nullptr;
    ImFont *tag_font = nullptr;
    float scale = 1.f;

  private:
    void load_icons(ImFontAtlas &atlas, const GameAssets &assets);
    bool atlas_image(unsigned int cell, ImVec2 position, float size, bool faded) const;
    int icon_rectangle = -1;
    unsigned int icon_width = 0, icon_height = 0;
    std::vector<float> effect_icon_y_;
};

void heading(const char *text, ImFont *font, float row_height = 0);
void centered_text(const char *text, UiRect bounds, ImFont *font = nullptr, float size = 0,
                   ImVec4 color = ivory);
// Visible vertical glyph bounds, relative to the text's drawing origin.
ImVec2 text_ink(const char *text, ImFont *font = nullptr, float size = 0, float wrap_width = 0);
struct RowAlignment {
    float icon_y, text_y;
};
RowAlignment align_row(float top, float height, float icon_size, ImVec2 ink);
std::string elide_text(const char *text, float width);
void notice(const Notice &message);
void vertical_gap(float height);
bool button(const char *label, ImVec2 size = {});
bool checkbox(const char *label, bool *value);
bool primary_button(const char *label, ImVec2 size);
bool selection_card(const char *label, bool selected, ImVec2 size);
bool sidebar_item(const char *label, bool selected, ImVec2 size);
void empty_state(const Appearance &appearance, const char *title, const char *description);
bool danger_button(const char *label, ImVec2 size);
} // namespace ern::ui
