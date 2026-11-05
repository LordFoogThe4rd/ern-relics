#include "ui/widgets.hpp"
#include "data/catalog_presentation.hpp"
#include <imgui_internal.h>
#include <cstring>
#include <algorithm>
#include <array>
#include <limits>
#include <cfloat>

namespace ern::ui {
namespace {
constexpr int notification_height = 14, notification_gap = 2;
}
// Share the existing font texture, preserving its DX12 descriptor/fence lifetime.
void Appearance::load_icons(ImFontAtlas &atlas, const GameAssets &assets) {
    const auto &icons = assets.atlas;
    icon_width = icons.width;
    icon_height = icons.height;
    if (icons.rgba.empty())
        return;
    effect_icon_y_.assign(IconAtlas::columns * (icon_height / IconAtlas::cell_size), 0.f);
    for (std::size_t cell = 0; cell < effect_icon_y_.size(); ++cell) {
        std::array<unsigned int, IconAtlas::icon_size> coverage{};
        for (unsigned int y = 0; y < IconAtlas::icon_size; ++y)
            for (unsigned int x = 0; x < IconAtlas::icon_size; ++x) {
                const auto at =
                    ((cell / IconAtlas::columns * IconAtlas::cell_size + IconAtlas::gutter + y) *
                         icon_width +
                     cell % IconAtlas::columns * IconAtlas::cell_size + IconAtlas::gutter + x) *
                    4;
                if (icons.rgba[at + 3] >= 32)
                    ++coverage[y];
            }
        // Center the main artwork, excluding faint shadows and narrow upgrade markers.
        const auto peak = *std::max_element(coverage.begin(), coverage.end());
        unsigned int top = IconAtlas::icon_size, bottom = 0;
        for (unsigned int y = 0; y < coverage.size(); ++y)
            if (coverage[y] && coverage[y] * 4 >= peak) {
                top = std::min(top, y);
                bottom = y + 1;
            }
        if (top < bottom)
            effect_icon_y_[cell] = float(top + bottom) / (2 * IconAtlas::icon_size) - .5f;
    }
    atlas.TexDesiredWidth = 4096;
    icon_rectangle =
        atlas.AddCustomRectRegular(static_cast<int>(icon_width), static_cast<int>(icon_height));
    unsigned char *pixels = nullptr;
    int width = 0, height = 0;
    atlas.GetTexDataAsRGBA32(&pixels, &width, &height);
    const auto *rect = atlas.GetCustomRectByIndex(icon_rectangle);
    for (unsigned int y = 0; y < icon_height; ++y)
        std::copy_n(icons.rgba.data() + std::size_t(y) * icon_width * 4, icon_width * 4,
                    pixels + (std::size_t(rect->Y + y) * width + rect->X) * 4);
    atlas.TexPixelsUseColors = true;
}

bool Appearance::atlas_image(unsigned int cell, ImVec2 position, float size, bool faded) const {
    if (icon_rectangle >= 0 && cell < IconAtlas::columns * (icon_height / IconAtlas::cell_size)) {
        auto &atlas = *ImGui::GetIO().Fonts;
        const auto *rect = atlas.GetCustomRectByIndex(icon_rectangle);
        const float x = static_cast<float>(
            rect->X + (cell % IconAtlas::columns) * IconAtlas::cell_size + IconAtlas::gutter);
        const float y = static_cast<float>(
            rect->Y + (cell / IconAtlas::columns) * IconAtlas::cell_size + IconAtlas::gutter);
        const float extent = static_cast<float>(IconAtlas::icon_size);
        ImGui::GetWindowDrawList()->AddImage(
            atlas.TexID, position, {position.x + size, position.y + size},
            {x / atlas.TexWidth, y / atlas.TexHeight},
            {(x + extent) / atlas.TexWidth, (y + extent) / atlas.TexHeight},
            IM_COL32(255, 255, 255, faded ? 178 : 255));
        return true;
    }
    return false;
}

void Appearance::portrait(const CatalogPresentation &catalog, const RelicInfo *info,
                          ImVec2 position, float size, bool faded) const {
    auto *draw = ImGui::GetWindowDrawList();
    const auto end = ImVec2(position.x + size, position.y + size);
    draw->AddRectFilled(position, end, ImGui::GetColorU32(rgb(7, 12, 23, .65f)));
    const auto cell = catalog.item_cell(info);
    if (!cell || !atlas_image(*cell, position, size, faded))
        draw->AddText({position.x + space(1), position.y + size / 2 - space(2)},
                      ImGui::GetColorU32(muted), "?");
    draw->AddRect(position, end, ImGui::GetColorU32(info ? colors[info->color] : muted));
}

void Appearance::empty_portrait(const CatalogPresentation &catalog, bool deep, ImVec2 position,
                                float size) const {
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 end{position.x + size, position.y + size};
    draw->AddRectFilled(position, end, ImGui::GetColorU32(rgb(7, 12, 23, .65f)));
    if (const auto cell = catalog.empty_relic_cell(deep))
        atlas_image(*cell, position, size, false);
    draw->AddRect(position, end, ImGui::GetColorU32(rgb(65, 80, 103)));
}

void Appearance::effect_icon(const CatalogPresentation &catalog, std::uint32_t id, ImVec2 position,
                             float size, bool faded) const {
    const auto cell = catalog.effect_cell(id);
    if (cell && *cell < effect_icon_y_.size())
        position.y -= std::round(effect_icon_y_[*cell] * size);
    if (cell && atlas_image(*cell, position, size, faded))
        return;
    auto *draw = ImGui::GetWindowDrawList();
    if (id != UINT32_MAX)
        draw->AddRect(position, {position.x + size, position.y + size}, ImGui::GetColorU32(muted));
    draw->AddLine({position.x + size * .3f, position.y + size * .5f},
                  {position.x + size * .7f, position.y + size * .5f}, ImGui::GetColorU32(muted));
}

void Appearance::effect_line(const CatalogPresentation &catalog, std::uint32_t id,
                             bool wrap) const {
    const auto position = ImGui::GetCursorScreenPos();
    const float size = space(5);
    const auto full_label = catalog.describe(id);
    const float text_width = std::max(0.f, ImGui::GetContentRegionAvail().x - space(7));
    const auto label = wrap ? full_label : elide_text(full_label.c_str(), text_width);
    const auto ink = text_ink(label.c_str(), nullptr, 0, wrap ? text_width : 0);
    const float text_height = ink.y - ink.x;
    const float row_height = std::max(size, text_height);
    const auto alignment = align_row(position.y, row_height, size, ink);
    effect_icon(catalog, id, {position.x, alignment.icon_y}, size);
    ImGui::GetWindowDrawList()->AddText(nullptr, 0, {position.x + space(7), alignment.text_y},
                                        ImGui::GetColorU32(ivory), label.c_str(), nullptr,
                                        wrap ? text_width : 0);
    ImGui::Dummy({ImGui::GetContentRegionAvail().x, std::ceil(row_height)});
    if (label != full_label && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", full_label.c_str());
}

void Appearance::effect_pair(const CatalogPresentation &catalog, std::uint32_t effect,
                             std::uint32_t curse, bool deep, bool wrap) const {
    ImGui::BeginGroup();
    effect_line(catalog, effect, wrap);
    if (deep || curse != UINT32_MAX) {
        vertical_gap(0);
        const auto position = ImGui::GetCursorScreenPos();
        const float width = std::max(0.f, ImGui::GetContentRegionAvail().x - space(7));
        const auto text = curse == UINT32_MAX ? std::string("No curse") : catalog.describe(curse);
        const auto label = wrap ? text : elide_text(text.c_str(), width);
        const auto ink = text_ink(label.c_str(), nullptr, 0, wrap ? width : 0);
        const float height = std::ceil(std::max({space(4), ImGui::GetFontSize(), ink.y - ink.x}));
        const ImVec4 clip{position.x + space(7), position.y, position.x + space(7) + width,
                          position.y + height};
        if (width > 0)
            ImGui::GetWindowDrawList()->AddText(
                nullptr, 0, {clip.x, std::round(position.y - ink.x)},
                ImGui::GetColorU32(curse == UINT32_MAX ? muted : ice), label.c_str(), nullptr,
                wrap ? width : 0, &clip);
        ImGui::Dummy({ImGui::GetContentRegionAvail().x, height});
        if (label != text && ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", text.c_str());
    }
    ImGui::EndGroup();
}

void Appearance::lock_icon(ImVec2 p, float size) const {
    auto *draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(ivory);
    draw->PathArcTo({p.x + size * .5f, p.y + size * .4f}, size * .24f, 3.14159f, 6.28318f, 12);
    draw->PathStroke(color, 0, px(2));
    draw->AddRectFilled({p.x + size * .17f, p.y + size * .38f},
                        {p.x + size * .83f, p.y + size * .9f}, color, px(1));
    draw->AddCircleFilled({p.x + size * .5f, p.y + size * .61f}, size * .065f,
                          IM_COL32(14, 20, 32, 255));
}

ImVec2 text_ink(const char *text, ImFont *font, float size, float wrap_width) {
    if (!font)
        font = ImGui::GetFont();
    if (size <= 0)
        size = ImGui::GetFontSize();
    const float scale = size / font->FontSize;
    ImVec2 bounds{std::numeric_limits<float>::max(), 0};
    const char *end = text + std::strlen(text);
    const char *wrap_end = nullptr;
    float y = 0;
    while (*text) {
        if (wrap_width > 0) {
            if (!wrap_end)
                wrap_end = font->CalcWordWrapPositionA(scale, text, end, wrap_width);
            if (text >= wrap_end) {
                y += size;
                while (ImCharIsBlankA(*text))
                    ++text;
                if (*text == '\n')
                    ++text;
                wrap_end = nullptr;
                continue;
            }
        }
        unsigned int character = 0;
        text += ImTextCharFromUtf8(&character, text, nullptr);
        if (character == '\n') {
            y += size;
            wrap_end = nullptr;
            continue;
        }
        if (character == '\r')
            continue;
        const auto *glyph = font->FindGlyph(static_cast<ImWchar>(character));
        if (glyph && glyph->Visible) {
            bounds.x = std::min(bounds.x, y + glyph->Y0 * scale);
            bounds.y = std::max(bounds.y, y + glyph->Y1 * scale);
        }
    }
    return bounds.y > bounds.x ? bounds : ImVec2{0, size};
}

RowAlignment align_row(float top, float height, float icon_size, ImVec2 ink) {
    const float center = top + height / 2;
    const float text_y = std::round(center - (ink.x + ink.y) / 2);
    return {std::round(text_y + (ink.x + ink.y) / 2 - icon_size / 2), text_y};
}

std::string elide_text(const char *text, float width) {
    auto *font = ImGui::GetFont();
    const float size = ImGui::GetFontSize();
    if (font->CalcTextSizeA(size, FLT_MAX, 0, text).x <= width)
        return text;
    const float dots = font->CalcTextSizeA(size, FLT_MAX, 0, "...").x;
    const char *end = text;
    font->CalcTextSizeA(size, std::max(0.f, width - dots), 0, text, nullptr, &end);
    return std::string(text, end) + "...";
}

void heading(const char *text, ImFont *font, float row_height) {
    ImGui::PushFont(font);
    const auto position = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const auto size = ImGui::CalcTextSize(text, nullptr, false, width);
    const auto ink = text_ink(text, nullptr, 0, width);
    const float height = std::ceil(std::max(row_height, ink.y - ink.x));
    ImGui::GetWindowDrawList()->AddText(
        nullptr, 0, {position.x, position.y + std::round((height - (ink.y - ink.x)) / 2 - ink.x)},
        ImGui::GetColorU32(ImGuiCol_Text), text, nullptr, width);
    ImGui::Dummy({size.x, height});
    ImGui::PopFont();
}

void centered_text(const char *text, UiRect bounds, ImFont *font, float size, ImVec4 color) {
    if (!font)
        font = ImGui::GetFont();
    if (size <= 0)
        size = ImGui::GetFontSize();
    const auto ink = text_ink(text, font, size, bounds.width);
    float y = bounds.y + (bounds.height - ink.y + ink.x) / 2 - ink.x;
    const char *end = text + std::strlen(text);
    for (const char *line = text; line < end;) {
        const auto *paragraph = std::find(line, end, '\n');
        const char *next =
            font->CalcWordWrapPositionA(size / font->FontSize, line, paragraph, bounds.width);
        if (next == line && line != paragraph) {
            unsigned int character = 0;
            next += ImTextCharFromUtf8(&character, line, paragraph);
        }
        auto *visible_end = next;
        while (visible_end > line &&
               (visible_end[-1] == ' ' || visible_end[-1] == '\t' || visible_end[-1] == '\r'))
            --visible_end;
        const auto width = font->CalcTextSizeA(size, FLT_MAX, 0, line, visible_end).x;
        ImGui::GetWindowDrawList()->AddText(
            font, size, {std::round(bounds.x + (bounds.width - width) / 2), std::round(y)},
            ImGui::GetColorU32(color), line, visible_end);
        y += size;
        line = next == paragraph && paragraph < end ? paragraph + 1 : next;
        while (line < end && (*line == ' ' || *line == '\t'))
            ++line;
    }
    if (ImGui::GetCurrentContext()->LogEnabled) {
        const ImVec2 position{bounds.x, bounds.y};
        ImGui::LogRenderedText(&position, text);
    }
}

ImVec2 Appearance::badge_size(const char *text) const {
    auto *font = tag_font ? tag_font : ImGui::GetFont();
    const float size = font->FontSize;
    const auto width = font->CalcTextSizeA(size, FLT_MAX, 0, text).x;
    const int columns = static_cast<int>(std::ceil(width / (4 * scale)));
    return {space(columns + 4), space(6)};
}

bool Appearance::badge(const char *text, ImVec2 position, ImVec4 color) const {
    auto *draw = ImGui::GetWindowDrawList();
    const auto size = badge_size(text);
    const ImVec2 end{position.x + size.x, position.y + size.y};
    draw->AddRectFilled(position, end, ImGui::GetColorU32({color.x, color.y, color.z, .13f}),
                        space(1));
    draw->AddRect(position, end, ImGui::GetColorU32({color.x, color.y, color.z, .35f}), space(1));
    auto *font = tag_font ? tag_font : ImGui::GetFont();
    // Optical adjustment is independent of the spacing grid.
    centered_text(text, {position.x, position.y + px(1), size.x, size.y}, font, font->FontSize,
                  color);
    return ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(position, end);
}

bool Appearance::trailing_badge(const char *text, UiRect row, ImVec4 color, int inset) const {
    const auto size = badge_size(text);
    return badge(
        text,
        {row.x + row.width - space(inset) - size.x, std::round(row.y + (row.height - size.y) / 2)},
        color);
}

Notice read_only_notice(const char *reason) {
    auto result = feedback("Editing unavailable", reason, NoticeKind::Warning);
    result.dismissible = false;
    return result;
}

float notification_space(const Appearance &appearance) {
    return appearance.space(notification_height) + appearance.space(notification_gap);
}

void notice(const Notice &message) {
    if (message.text.empty())
        return;
    ImGui::PushStyleColor(ImGuiCol_Text, message.color);
    ImGui::TextWrapped("%s", message.text.c_str());
    ImGui::PopStyleColor();
}

Notice feedback(std::string title, std::string body, NoticeKind kind) {
    return {std::move(body),
            kind == NoticeKind::Error     ? removed
            : kind == NoticeKind::Warning ? amber
                                          : ice,
            std::move(title), kind};
}

NoticeAction notification(Notice &message, const Appearance &appearance, UiRect bounds) {
    if (message.text.empty())
        return NoticeAction::None;
    const float height = appearance.space(notification_height);
    ImGui::SetNextWindowPos({bounds.x, bounds.y - notification_space(appearance)});
    ImGui::SetNextWindowSize({bounds.width, height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {appearance.space(4), appearance.space(2)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, rgb(14, 23, 39, 1));
    ImGui::Begin("##notification", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollWithMouse |
                     ImGuiWindowFlags_NoFocusOnAppearing);
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    // Banners sit above the editor, but below open popups regardless of submission order.
    for (const auto &popup : ImGui::GetCurrentContext()->OpenPopupStack)
        if (popup.Window) {
            ImGui::BringWindowToDisplayBehind(ImGui::GetCurrentWindow(), popup.Window);
            break;
        }
    const auto p = ImGui::GetWindowPos();
    auto *draw = ImGui::GetWindowDrawList();
    const auto color = ImGui::GetColorU32(message.color);
    draw->AddRect(p, {p.x + bounds.width, p.y + height},
                  ImGui::GetColorU32({message.color.x, message.color.y, message.color.z, .3f}));
    draw->AddRectFilled(p, {p.x + appearance.space(1), p.y + height}, color);
    const float padding = appearance.space(4), gap = appearance.space(2);
    const float action_width = message.action == NoticeAction::None ? 0 : appearance.space(33);
    const float dismiss_width = message.dismissible ? appearance.space(18) : 0;
    const float text_width = bounds.width - padding * 2 - (action_width ? action_width + gap : 0) -
                             (dismiss_width ? dismiss_width + gap : 0);
    auto *body_font =
        !message.title.empty() && appearance.tag_font ? appearance.tag_font : ImGui::GetFont();
    const float body_size = body_font->FontSize;
    const auto body_ink = text_ink(message.text.c_str(), body_font, body_size, text_width);
    const auto title = elide_text(message.title.c_str(), text_width);
    const auto title_ink = text_ink(title.c_str());
    const float title_height = title.empty() ? 0 : title_ink.y - title_ink.x + appearance.space(1);
    const float text_height = title_height + body_ink.y - body_ink.x;
    const float top =
        p.y + std::round((height - std::min(text_height, height - appearance.space(3))) / 2);
    const ImVec2 text{p.x + padding, top};
    const ImVec4 clip{text.x, p.y + appearance.space(1), text.x + text_width,
                      p.y + height - appearance.space(1)};
    if (!title.empty())
        draw->AddText({text.x, std::round(top - title_ink.x)}, color, title.c_str());
    draw->AddText(body_font, body_size, {text.x, std::round(top + title_height - body_ink.x)},
                  ImGui::GetColorU32(title.empty() ? message.color : ivory), message.text.c_str(),
                  nullptr, text_width, &clip);
    if (ImGui::GetCurrentContext()->LogEnabled)
        ImGui::LogRenderedText(&text, message.text.c_str());
    NoticeAction result = NoticeAction::None;
    const float button_height = appearance.space(8);
    ImGui::SetCursorPos({bounds.width - padding - action_width - dismiss_width -
                             (action_width && dismiss_width ? gap : 0),
                         std::round((height - button_height) / 2)});
    if (action_width) {
        const auto label =
            std::string(message.action == NoticeAction::ReloadSlot ? "Reload slot "
                                                                   : "Review slot ") +
            std::to_string(message.slot + 1);
        if (button(label.c_str(), {action_width, button_height}))
            result = message.action;
        if (dismiss_width)
            ImGui::SameLine(0, gap);
    }
    if (dismiss_width && button("Dismiss", {dismiss_width, button_height}))
        message.text.clear();
    if (text_height > height - appearance.space(3) && ImGui::IsWindowHovered() &&
        ImGui::IsMouseHoveringRect({text.x, clip.y}, {clip.z, clip.w}))
        ImGui::SetTooltip("%s", message.text.c_str());
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(2);
    return result;
}

namespace {
void control_label(const char *label, const ImRect &bounds, float padding, float align) {
    const std::string text(label, ImGui::FindRenderedTextEnd(label));
    if (text.empty() || !ImGui::IsItemVisible())
        return;
    // Center the font's cap height so every label shares a baseline, including descenders.
    const auto ink = text_ink("H");
    const float y = std::round((bounds.Min.y + bounds.Max.y - ink.x - ink.y) / 2);
    ImGui::RenderTextClippedEx(ImGui::GetWindowDrawList(), {bounds.Min.x + padding, y},
                               {bounds.Max.x - padding, bounds.Max.y}, text.c_str(), nullptr,
                               nullptr, {align, 0}, &bounds);
}
} // namespace

bool button(const char *label, ImVec2 size) {
    if (ImGui::GetCurrentWindow()->SkipItems)
        return false;
    // Keep native sizing, interaction, navigation and logging; draw the visible ink ourselves.
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0, 0, 0, 0});
    const bool clicked = ImGui::Button(label, size);
    ImGui::PopStyleColor();
    const auto &style = ImGui::GetStyle();
    control_label(label, {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()}, style.FramePadding.x,
                  style.ButtonTextAlign.x);
    return clicked;
}

bool checkbox(const char *label, bool *value) {
    if (ImGui::GetCurrentWindow()->SkipItems)
        return false;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{0, 0, 0, 0});
    const bool changed = ImGui::Checkbox(label, value);
    ImGui::PopStyleColor();
    auto min = ImGui::GetItemRectMin();
    min.x += ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x;
    control_label(label, {min, ImGui::GetItemRectMax()}, 0, 0);
    return changed;
}

bool primary_button(const char *label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(49, 82, 131));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(65, 104, 160));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(42, 70, 113));
    const bool clicked = button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

void vertical_gap(float height) {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + height - ImGui::GetStyle().ItemSpacing.y);
}

bool danger_button(const char *label, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, rgb(111, 48, 46));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(141, 61, 57));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(91, 39, 38));
    const bool clicked = button(label, size);
    ImGui::PopStyleColor(3);
    return clicked;
}

void empty_state(const Appearance &appearance, const char *title, const char *description) {
    const auto available = ImGui::GetContentRegionAvail();
    const auto position = ImGui::GetCursorScreenPos();
    const float width = std::max(appearance.space(15),
                                 std::min(appearance.space(85), available.x - appearance.space(8)));
    ImGui::PushFont(appearance.name_font);
    const float title_size = ImGui::GetFontSize();
    const auto title_ink = text_ink(title, nullptr, title_size, width);
    ImGui::PopFont();
    const auto description_ink = text_ink(description, nullptr, 0, width);
    const float title_height = title_ink.y - title_ink.x;
    const float description_height = description_ink.y - description_ink.x;
    const float height = title_height + appearance.space(2) + description_height;
    const float left = std::round(position.x + (available.x - width) / 2);
    const float top = std::round(position.y + std::max(0.f, (available.y - height) / 2));
    centered_text(title, {left, top, width, title_height}, appearance.name_font, title_size, ivory);
    centered_text(description,
                  {left, top + title_height + appearance.space(2), width, description_height},
                  nullptr, 0, muted);
    ImGui::Dummy(available);
}

bool sidebar_item(const char *label, bool selected, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? rgb(32, 47, 72) : rgb(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0);
    const bool clicked = button(label, size);
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();
    return clicked;
}

bool selection_card(const char *label, bool selected, ImVec2 size) {
    ImGui::PushStyleColor(ImGuiCol_Button, selected ? rgb(32, 47, 72) : rgb(9, 15, 26, .5f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, rgb(39, 56, 83));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, rgb(44, 63, 94));
    ImGui::PushStyleColor(ImGuiCol_Border, selected ? ice : rgb(65, 80, 103));
    const bool clicked = button(label, size);
    ImGui::PopStyleColor(4);
    return clicked;
}

void Appearance::initialize(const GameAssets &assets, const SystemFonts &fonts,
                            float display_height) {
    scale = editor_scale(display_height);
    ImGui::StyleColorsDark();
    auto &style = ImGui::GetStyle();
    style.WindowPadding = {spacing(3), spacing(3)};
    style.FramePadding = {spacing(2), spacing(2)};
    style.ItemSpacing = {spacing(2), spacing(1)};
    style.ItemInnerSpacing = {spacing(2), spacing(1)};
    style.CellPadding = {spacing(1), spacing(1)};
    style.IndentSpacing = spacing(4);
    style.ScrollbarSize = space(4);
    style.GrabMinSize = space(6);
    style.ButtonTextAlign = {.5f, .5f};
    style.WindowRounding = style.FrameRounding = style.PopupRounding = 0;
    style.WindowBorderSize = style.FrameBorderSize = 1;
    style.PopupBorderSize = 1;
    auto &c = style.Colors;
    c[ImGuiCol_WindowBg] = rgb(10, 16, 29, .97f);
    c[ImGuiCol_ChildBg] = rgb(10, 16, 29, 0);
    c[ImGuiCol_PopupBg] = rgb(13, 21, 36, 1);
    c[ImGuiCol_Border] = rgb(65, 80, 103, .8f);
    c[ImGuiCol_Separator] = rgb(79, 95, 121, .65f);
    c[ImGuiCol_Text] = ivory;
    c[ImGuiCol_TextDisabled] = muted;
    c[ImGuiCol_FrameBg] = c[ImGuiCol_Button] = rgb(17, 26, 42, .85f);
    c[ImGuiCol_FrameBgHovered] = c[ImGuiCol_ButtonHovered] = rgb(34, 51, 78);
    c[ImGuiCol_ButtonActive] = rgb(49, 71, 104);
    c[ImGuiCol_CheckMark] = c[ImGuiCol_NavCursor] = ice;
    c[ImGuiCol_Header] = rgb(32, 47, 72);
    c[ImGuiCol_HeaderHovered] = rgb(44, 63, 94);
    c[ImGuiCol_ModalWindowDimBg] = rgb(5, 10, 19, .55f);
    c[ImGuiCol_ScrollbarGrab] = rgb(92, 110, 138);
    c[ImGuiCol_ScrollbarGrabHovered] = rgb(129, 155, 190);
    c[ImGuiCol_ScrollbarGrabActive] = ice;
    auto &io = ImGui::GetIO();
    // Rebuilt only while the renderer is stopped, including after a resolution change.
    io.Fonts->Clear();
    io.IniFilename = io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    const auto add_font = [&](const auto &bytes, float size) -> ImFont * {
        if (bytes.empty())
            return nullptr;
        void *owned = IM_ALLOC(bytes.size());
        if (!owned)
            return nullptr;
        std::memcpy(owned, bytes.data(), bytes.size());
        ImFontConfig config;
        config.PixelSnapH = true;
        config.OversampleH = config.OversampleV = 1;
        return io.Fonts->AddFontFromMemoryTTF(owned, static_cast<int>(bytes.size()), px(size),
                                              &config);
    };
    io.FontDefault = add_font(fonts.controls, 18.f);
    relic_font = io.FontDefault;
    tag_font = add_font(fonts.controls, 16.f);
    title_font = add_font(fonts.headings, 24.f);
    name_font = add_font(fonts.headings, 20.f);
    load_icons(*io.Fonts, assets);
}

} // namespace ern::ui
