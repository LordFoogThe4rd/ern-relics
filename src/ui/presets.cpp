#include "ui/presets.hpp"
#include "app/editor_view.hpp"
#include "ui/edit_feedback.hpp"
#include <algorithm>
#include <cmath>
namespace ern::ui {
namespace {
void preset_slot(const EditorView &rt, const Appearance &appearance, std::size_t index,
                 const Preset &preset, const PresetStage *staged, const EquippedRelics *equipped) {
    const auto *relic = equipped && equipped->slots[index] ? &*equipped->slots[index] : nullptr;
    const auto *info = relic ? rt.items().find(relic->item_id) : nullptr;
    const bool ready = staged && staged->drafts.slots[index] && rt.editing_supported();
    const bool deep = index >= effects_per_relic;
    const float art = appearance.space(9);
    const float curse_height = std::max(appearance.space(4), ImGui::GetFontSize());
    // Sum rounded parts so fractional display scaling cannot add an inner scrollbar.
    const float height = appearance.space(2) * 2 + art + appearance.space(4) +
                         effects_per_relic * (appearance.space(5) + curse_height) +
                         (effects_per_relic - 1) * appearance.space(2);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {appearance.space(2), appearance.space(2)});
    ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(9, 15, 26, .5f));
    ImGui::BeginChild("preset-slot", {0, height}, ImGuiChildFlags_Borders);
    ImGui::PushFont(appearance.relic_font);
    const auto position = ImGui::GetCursorScreenPos();
    if (preset.slots[index] && relic)
        appearance.portrait(rt.presentation(), info, position, art, !ready);
    else
        appearance.empty_portrait(rt.presentation(), deep, position, art);
    const auto label = std::string(deep ? "Deep slot " : "Normal slot ") +
                       std::to_string(index % effects_per_relic + 1);
    const char *status = ready ? "Ready" : preset.slots[index] ? "Skipped" : "Omitted";
    const auto size = appearance.badge_size(status);
    const float right = position.x + ImGui::GetContentRegionAvail().x;
    const bool tag_hovered =
        appearance.trailing_badge(status, {position.x, position.y, right - position.x, size.y},
                                  ready                 ? ice
                                  : preset.slots[index] ? amber
                                                        : muted,
                                  0);
    const auto name = rt.presentation().name(info);
    const float header_width =
        std::max(0.f, right - size.x - position.x - art - appearance.space(4));
    const auto title = elide_text(name.c_str(), header_width);
    const auto caption = elide_text(label.c_str(), header_width);
    const auto caption_ink = text_ink(caption.c_str()), title_ink = text_ink(title.c_str());
    const float gap = appearance.space(1);
    const float caption_height = caption_ink.y - caption_ink.x;
    const float top =
        position.y + std::round((art - caption_height - gap - title_ink.y + title_ink.x) / 2);
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 text_start{position.x + art + appearance.space(2), position.y};
    draw->PushClipRect(text_start, {text_start.x + header_width, position.y + art}, true);
    draw->AddText({position.x + art + appearance.space(2), top - caption_ink.x},
                  ImGui::GetColorU32(muted), caption.c_str());
    draw->AddText(
        {position.x + art + appearance.space(2), top + caption_height + gap - title_ink.x},
        ImGui::GetColorU32(ivory), title.c_str());
    draw->PopClipRect();
    ImGui::Dummy({ImGui::GetContentRegionAvail().x, art});
    if (tag_hovered) {
        const auto reason = ready ? std::string("This template fits the equipped relic.")
                            : !preset.slots[index]
                                ? std::string("This slot was omitted from the preset.")
                            : staged && !staged->skipped[index].empty()
                                ? staged->skipped[index]
                                : "No editable relic is equipped in this slot.";
        ImGui::SetTooltip("%s", reason.c_str());
    }
    ImGui::SetCursorScreenPos({position.x, position.y + art + appearance.space(2)});
    ImGui::Separator();
    ImGui::SetCursorScreenPos({position.x, position.y + art + appearance.space(4)});
    if (preset.slots[index]) {
        const auto &effects = *preset.slots[index];
        for (std::size_t effect = 0; effect < effects_per_relic; ++effect) {
            if (effects.rows[effect].effect == UINT32_MAX)
                continue;
            appearance.effect_pair(rt.presentation(), effects.rows[effect].effect,
                                   effects.rows[effect].curse, deep, false);
            if (effect + 1 < effects_per_relic)
                vertical_gap(appearance.space(2));
        }
    } else
        ImGui::TextColored(muted, "Slot omitted");
    ImGui::PopFont();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();
}
void delete_dialog(std::optional<EditorAction> &action, PresetBrowser &browser,
                   const Appearance &appearance, UiRect editor) {
    const float width = std::min(appearance.space(105), editor.width - appearance.space(10));
    ImGui::SetNextWindowPos({editor.x + editor.width / 2, editor.y + editor.height / 2},
                            ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, editor.height - appearance.space(10)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {std::round(appearance.space(4)), std::round(appearance.space(4))});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, appearance.space(1));
    if (ImGui::BeginPopupModal("Delete preset?", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        heading("Delete preset?", appearance.name_font);
        ImGui::Dummy({0, appearance.space(1)});
        ImGui::TextWrapped("%s", browser.pending_delete.c_str());
        notice({"This cannot be undone."});
        ImGui::Dummy({0, appearance.space(2)});
        const float button_width = (ImGui::GetContentRegionAvail().x - appearance.space(2)) / 2;
        if (button("Cancel", {button_width, appearance.space(9)}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            browser.pending_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine(0, appearance.space(2));
        if (danger_button("Delete preset", {button_width, appearance.space(9)})) {
            action = DeletePreset{browser.pending_delete};
            browser.pending_delete.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

void replace_dialog(std::optional<EditorAction> &action, PresetBrowser &browser,
                    const Appearance &appearance, UiRect editor, bool can_save, bool invalid) {
    const float width = std::min(appearance.space(105), editor.width - appearance.space(10));
    ImGui::SetNextWindowPos({editor.x + editor.width / 2, editor.y + editor.height / 2},
                            ImGuiCond_Always, {.5f, .5f});
    ImGui::SetNextWindowSizeConstraints({width, 0}, {width, editor.height - appearance.space(10)});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {std::round(appearance.space(4)), std::round(appearance.space(4))});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, appearance.space(1));
    if (ImGui::BeginPopupModal("Replace preset?", nullptr,
                               ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                                   ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize)) {
        heading("Replace preset?", appearance.name_font);
        ImGui::Dummy({0, appearance.space(1)});
        ImGui::TextWrapped("%s", browser.pending_replace.c_str());
        notice(
            {"Replace its saved effects with the currently equipped effects. This cannot be undone."});
        ImGui::Dummy({0, appearance.space(2)});
        const float button_width = (ImGui::GetContentRegionAvail().x - appearance.space(2)) / 2;
        if (button("Cancel", {button_width, appearance.space(9)}) ||
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            browser.pending_replace.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine(0, appearance.space(2));
        ImGui::BeginDisabled(!can_save);
        if (danger_button("Replace preset", {button_width, appearance.space(9)})) {
            action = SavePreset{browser.pending_replace, PresetSaveMode::Replace, invalid};
            browser.pending_replace.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar(2);
}

} // namespace

void draw_presets(const EditorView &rt, std::optional<EditorAction> &action, PresetBrowser &browser,
                  const Appearance &appearance, const EquippedRelics *equipped, UiRect editor,
                  bool invalid) {
    const auto &style = ImGui::GetStyle();
    const float footer_y =
        ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - ImGui::GetFrameHeight();
    ImGui::BeginChild("preset-browser",
                      {0, std::max(1.f, footer_y - ImGui::GetCursorPosY() - appearance.space(2) -
                                            notification_space(appearance))});
    if (!rt.presets->empty()) {
        const float list_width =
            std::min(appearance.space(40), ImGui::GetContentRegionAvail().x * .27f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
        ImGui::BeginChild("saved-presets", {list_width, 0}, ImGuiChildFlags_Borders);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            {appearance.space(2), appearance.space(2)});
        ImGui::BeginChild("preset-search", {0, 0},
                          ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x);
        if (ImGui::InputTextWithHint("##search", "Search presets...", browser.filter.InputBuf,
                                     sizeof(browser.filter.InputBuf)))
            browser.filter.Build();
        ImGui::TextColored(muted, "Saved presets");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        vertical_gap(0);
        ImGui::BeginChild("saved-preset-list", {0, 0});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {0, 0});
        bool visible = false;
        for (const auto &entry : (*rt.presets)) {
            if (!browser.filter.PassFilter(entry.name.c_str()))
                continue;
            visible = true;
            ImGui::PushID(entry.name.c_str());
            const auto position = ImGui::GetCursorScreenPos();
            const auto width = ImGui::GetContentRegionAvail().x;
            const bool active = browser.selected == entry.name;
            const auto count =
                entry.contents
                    ? std::count_if(entry.contents->slots.begin(), entry.contents->slots.end(),
                                    [](const auto &slot) { return slot.has_value(); })
                    : 0;
            const auto subtitle =
                entry.contents
                    ? std::to_string(count) + (count == 1 ? " relic template" : " relic templates")
                    : "Cannot read preset";
            const auto name = elide_text(
                entry.name.c_str(), std::max(appearance.space(5), width - appearance.space(5)));
            const auto name_ink = text_ink(name.c_str());
            const auto subtitle_ink = text_ink(subtitle.c_str());
            const float content_height =
                name_ink.y - name_ink.x + appearance.space(1) + subtitle_ink.y - subtitle_ink.x;
            const float height =
                std::ceil(std::max(appearance.space(10), content_height + appearance.space(4)));
            if (sidebar_item("##preset", active, {width, height})) {
                browser.selected = entry.name;
                browser.feedback = {};
            }
            if (name != entry.name && ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", entry.name.c_str());
            auto *draw = ImGui::GetWindowDrawList();
            draw->PushClipRect({position.x + appearance.space(1), position.y},
                               {position.x + width - appearance.space(1), position.y + height},
                               true);
            const float top = std::round(position.y + (height - content_height) / 2);
            draw->AddText({position.x + appearance.space(2), top - name_ink.x},
                          ImGui::GetColorU32(ivory), name.c_str());
            draw->AddText({position.x + appearance.space(2),
                           top + name_ink.y - name_ink.x + appearance.space(1) - subtitle_ink.x},
                          ImGui::GetColorU32(entry.contents ? muted : amber), subtitle.c_str());
            draw->PopClipRect();
            ImGui::PopID();
        }
        ImGui::PopStyleVar();
        if (!visible) {
            const auto at = ImGui::GetCursorScreenPos(), available = ImGui::GetContentRegionAvail();
            centered_text("No matching presets", {at.x, at.y, available.x, available.y}, nullptr, 0,
                          muted);
        }
        ImGui::EndChild();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::SameLine(0, appearance.space(2));
    }
    ImGui::BeginChild("preset-preview", {0, 0}, ImGuiChildFlags_Borders);
    const auto entry = std::find_if(rt.presets->begin(), rt.presets->end(), [&](const auto &value) {
        return value.name == browser.selected;
    });
    if (entry != rt.presets->end()) {
        const auto preset = entry->contents;
        std::optional<PresetStage> staged;
        if (preset && equipped)
            staged = stage_preset(*preset, *equipped, rt.validator(), rt.context);
        const auto count =
            staged ? std::count_if(staged->drafts.slots.begin(), staged->drafts.slots.end(),
                                   [](const auto &value) { return value.has_value(); })
                   : 0;
        const float buttons_width = appearance.space(61);
        const auto title = elide_text(
            entry->name.c_str(),
            std::max(appearance.space(10), ImGui::GetContentRegionAvail().x - buttons_width));
        heading(title.c_str(), appearance.name_font, ImGui::GetFrameHeight());
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - buttons_width);
        if (button("Delete preset", {appearance.space(26), 0})) {
            browser.pending_delete = entry->name;
            ImGui::OpenPopup("Delete preset?");
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!rt.editing_supported() || !count);
        if (primary_button("Load into drafts", {appearance.space(33), 0}))
            action = LoadPreset{*preset};
        ImGui::EndDisabled();
        ImGui::TextColored(muted, "%zu slots ready to load", static_cast<std::size_t>(count));
        ImGui::BeginChild("preset-slot-previews", {0, 0});
        if (preset) {
            const int columns = ImGui::GetContentRegionAvail().x >= appearance.space(195) ? 3 : 2;
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding,
                                {appearance.space(1), appearance.space(1)});
            if (ImGui::BeginTable("preset-slots", columns,
                                  ImGuiTableFlags_SizingStretchSame |
                                      ImGuiTableFlags_NoSavedSettings |
                                      ImGuiTableFlags_NoPadOuterX)) {
                for (std::size_t i = 0; i < preset->slots.size(); ++i) {
                    ImGui::TableNextColumn();
                    ImGui::PushID(static_cast<int>(i));
                    preset_slot(rt, appearance, i, *preset, staged ? &*staged : nullptr, equipped);
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            ImGui::PopStyleVar();
        } else
            notice({"This file is not a valid relic preset.", amber});
        ImGui::EndChild();
    } else if (rt.preset_error)
        empty_state(appearance, "Presets unavailable",
                    "Check folder permissions and reopen Presets to retry.");
    else
        empty_state(appearance, "Save your equipped relics",
                    "Give this setup a name below to create your first preset.");
    delete_dialog(action, browser, appearance, editor);
    ImGui::EndChild();
    ImGui::EndChild();
    ImGui::SetCursorPosY(footer_y);
    const auto footer = ImGui::GetCursorScreenPos();
    const float footer_width = ImGui::GetContentRegionAvail().x;
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(muted, "Save equipped effects");
    ImGui::SameLine();
    const float save_width =
        std::max(appearance.space(34),
                 ImGui::CalcTextSize("Replace preset").x + ImGui::GetStyle().FramePadding.x * 2);
    ImGui::SetNextItemWidth(std::max(appearance.space(10), ImGui::GetContentRegionAvail().x -
                                                               save_width - style.ItemSpacing.x));
    ImGui::InputTextWithHint("##preset-name", "Preset name", browser.name, sizeof(browser.name));
    ImGui::SameLine();
    const std::string_view name = browser.name;
    const auto existing =
        std::find_if(rt.presets->begin(), rt.presets->end(), [&](const auto &saved) {
            return std::equal(saved.name.begin(), saved.name.end(), name.begin(), name.end(),
                              [](char left, char right) {
                                  const auto lower = [](char ch) {
                                      return ch >= 'A' && ch <= 'Z' ? ch + ('a' - 'A') : ch;
                                  };
                                  return lower(left) == lower(right);
                              });
        });
    const bool replacing = existing != rt.presets->end();
    const bool can_save = equipped && rt.editing_supported();
    ImGui::BeginDisabled(!can_save || name.empty());
    if (button(replacing ? "Replace preset" : "Save preset", {save_width, 0})) {
        if (replacing) {
            browser.pending_replace = existing->name;
            ImGui::OpenPopup("Replace preset?");
        } else
            action = SavePreset{browser.name, PresetSaveMode::Create, invalid};
    }
    ImGui::EndDisabled();
    replace_dialog(action, browser, appearance, editor, can_save, invalid);
    if (const auto *reason = rt.editing_lock_reason()) {
        auto locked = read_only_notice(reason);
        notification(locked, appearance, {footer.x, footer.y, footer_width, 0});
    } else
        notification(browser.feedback, appearance, {footer.x, footer.y, footer_width, 0});
}
} // namespace ern::ui
