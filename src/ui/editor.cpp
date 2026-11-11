#include "ui/editor.hpp"
#include "ui/diagnostics.hpp"
#include "ui/edit_feedback.hpp"
#include "app/editor_view.hpp"
#include <algorithm>
#include <cmath>
#include <vector>

namespace ern::ui {
namespace {
void change_value(const std::string &previous, const std::string &current) {
    const float left = ImGui::GetCursorPosX();
    const float text_x = left + std::max(ImGui::CalcTextSize("+").x, ImGui::CalcTextSize("-").x) +
                         ImGui::GetStyle().ItemInnerSpacing.x;
    const auto line = [&](const char *symbol, const std::string &text, ImVec4 color) {
        ImGui::BeginGroup();
        ImGui::TextColored(color, "%s", symbol);
        ImGui::SameLine();
        ImGui::SetCursorPosX(text_x);
        ImGui::TextColored(color, "%s", text.c_str());
        ImGui::EndGroup();
    };
    line("+", current, added);
    line("-", previous, removed);
}

void effect_tooltip(const Appearance &appearance, const std::string &text) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {appearance.space(2), appearance.space(2)});
    ImGui::PushFont(ImGui::GetIO().FontDefault);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() +
                           std::min(ImGui::GetFontSize() * 28, ImGui::GetIO().DisplaySize.x * .4f));
    ImGui::TextUnformatted(text.c_str());
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopFont();
    ImGui::PopStyleVar();
}

void change_tooltip(const EditorView &rt, const Appearance &appearance, const Draft &draft,
                    std::optional<EffectPosition> selected = std::nullopt) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {appearance.space(2), appearance.space(2)});
    ImGui::PushFont(ImGui::GetIO().FontDefault);
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + ImGui::GetFontSize() * 28);
    const auto describe = [&](EffectPosition position, std::uint32_t id) {
        return position.field == EffectField::Curse && id == UINT32_MAX
                   ? std::string("No curse")
                   : rt.presentation().describe(id);
    };
    if (selected) {
        change_value(describe(*selected, effect_at(draft.original.effects, *selected)),
                     describe(*selected, effect_at(draft.effects, *selected)));
    } else {
        ImGui::TextUnformatted("Relic changes");
        bool first = true;
        for (std::size_t row = 0; row < effects_per_relic; ++row) {
            const bool effect_changed =
                draft.effects.rows[row].effect != draft.original.effects.rows[row].effect;
            const bool curse_changed =
                draft.effects.rows[row].curse != draft.original.effects.rows[row].curse;
            if (!effect_changed && !curse_changed)
                continue;
            if (!first)
                ImGui::Separator();
            first = false;
            ImGui::Spacing();
            ImGui::TextUnformatted(("Effect " + std::to_string(row + 1)).c_str());
            if (effect_changed)
                change_value(
                    describe({row, EffectField::Effect}, draft.original.effects.rows[row].effect),
                    describe({row, EffectField::Effect}, draft.effects.rows[row].effect));
            else
                ImGui::TextUnformatted(
                    describe({row, EffectField::Effect}, draft.effects.rows[row].effect).c_str());
            if (curse_changed) {
                ImGui::TextColored(muted, "Curse");
                change_value(
                    describe({row, EffectField::Curse}, draft.original.effects.rows[row].curse),
                    describe({row, EffectField::Curse}, draft.effects.rows[row].curse));
            }
        }
    }
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
    ImGui::PopFont();
    ImGui::PopStyleVar();
}

void relic_tiles(const EditorView &rt, EditorState &state, const Appearance &appearance,
                 const EquippedRelics &equipped) {
    const bool grid = !rt.editing_supported();
    const float available_width = ImGui::GetContentRegionAvail().x;
    const float gap = appearance.space(2), width = std::floor((available_width - gap * 2) / 3);
    const float remainder = available_width - width * 3 - gap * 2;
    const float art = appearance.space(9), text_width = width - appearance.space(11);
    struct Tile {
        const RelicInfo *info = nullptr;
        const Effects *effects = nullptr;
        struct Row {
            std::string effect_label, curse_label;
            float effect_height = 0, curse_height = 0;
        };
        std::array<Row, effects_per_relic> rows;
    };
    std::array<Tile, relic_slot_count> tiles;
    std::array<float, 2> heights{appearance.space(32), appearance.space(32)};
    ImGui::PushFont(appearance.relic_font);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const auto &relic = equipped.slots[i];
        if (!relic)
            continue;
        auto &tile = tiles[i];
        tile.info = rt.items().find(relic->item_id);
        tile.effects = tile.info && (tile.info->kind == RelicKind::Unique ||
                                     tile.info->kind == RelicKind::Fixed)
                           ? &tile.info->fixed_effects
                       : !rt.editing_supported() ? &relic->effects
                                                 : &rt.drafts[i]->effects;
        float tile_height = appearance.space(15);
        for (std::size_t effect = 0; effect < effects_per_relic; ++effect) {
            if (tile.effects->rows[effect].effect == UINT32_MAX)
                continue;
            tile.rows[effect].effect_label =
                rt.presentation().describe(tile.effects->rows[effect].effect);
            tile.rows[effect].effect_height = appearance.space(5);
            tile_height += tile.rows[effect].effect_height;
            if (i >= effects_per_relic ||
                (state.invalid && tile.effects->rows[effect].curse != UINT32_MAX)) {
                const auto id = tile.effects->rows[effect].curse;
                tile.rows[effect].curse_label =
                    id == UINT32_MAX ? "No curse" : rt.presentation().describe(id);
                tile.rows[effect].curse_height =
                    std::max(appearance.space(4), ImGui::GetFontSize());
                tile_height += tile.rows[effect].curse_height;
            }
            tile_height += appearance.space(2);
        }
        auto &height = heights[grid ? i / effects_per_relic : 0];
        height = std::max(height, tile_height);
    }
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    const float content_height = grid ? heights[0] + gap + heights[1] : heights[0];
    ImGui::SetNextWindowContentSize(
        {grid ? available_width : available_width * 2 + gap, content_height});
    // Separate child state makes the carousel reappear and reveal the selected slot on unlock.
    ImGui::BeginChild(grid ? "relic-grid" : "relics",
                      {0, std::ceil(content_height + (grid ? 0 : ImGui::GetStyle().ScrollbarSize))},
                      ImGuiChildFlags_None,
                      grid ? ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
                           : ImGuiWindowFlags_HorizontalScrollbar |
                                 ImGuiWindowFlags_AlwaysHorizontalScrollbar);
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        if (i != 0 && (!grid || i % effects_per_relic != 0))
            ImGui::SameLine(0, gap);
        else if (grid && i != 0)
            vertical_gap(gap);
        ImGui::PushID(static_cast<int>(i));
        const float height = heights[grid ? i / effects_per_relic : 0];
        const float tile_width = width + (i % effects_per_relic == 2 ? remainder : 0.f);
        const auto position = ImGui::GetCursorScreenPos();
        ImGui::BeginGroup();
        if (selection_card("##relic", state.selected == i, {tile_width, height})) {
            state.selected = i;
            state.selected_effect = {};
            state.effect_filter.Clear();
            state.group_filter.reset();
            state.status = {};
        }
        if (!grid && state.selected == i && ImGui::IsWindowAppearing())
            ImGui::SetScrollHereX();
        auto *draw = ImGui::GetWindowDrawList();
        const auto end = ImVec2(position.x + tile_width, position.y + height);
        const auto &relic = equipped.slots[i];
        const auto &tile = tiles[i];
        const auto *info = tile.info;
        const bool locked = relic && relic_lock_reason(info);
        const auto slot_label = "Slot " + std::to_string(i + 1);
        const auto image_pos =
            ImVec2(position.x + appearance.space(2), position.y + appearance.space(2));
        const auto name = info    ? rt.presentation().name(info)
                          : relic ? std::string("Unrecognized relic")
                                  : std::string("Empty slot");
        const auto changes =
            rt.editing_supported() && rt.drafts[i] ? changed_effect_count(*rt.drafts[i]) : 0;
        const auto tag = locked ? std::string("Read only")
                         : changes
                             ? std::to_string(changes) + (changes == 1 ? " change" : " changes")
                             : std::string{};
        const float tag_width =
            tag.empty() ? 0 : appearance.badge_size(tag.c_str()).x + appearance.space(2);
        const auto displayed_name =
            elide_text(name.c_str(), tile_width - appearance.space(15) - tag_width);
        const auto slot_ink = text_ink(slot_label.c_str());
        const auto name_ink = text_ink(displayed_name.c_str());
        const float header_gap = appearance.space(1);
        const float header_height = slot_ink.y - slot_ink.x + header_gap + name_ink.y - name_ink.x;
        const float header_top = image_pos.y + std::round((art - header_height) / 2);
        if (relic)
            appearance.portrait(rt.presentation(), info, image_pos, art, locked);
        else
            appearance.empty_portrait(rt.presentation(), i >= effects_per_relic, image_pos, art);
        if (locked)
            appearance.lock_icon(
                {image_pos.x + art - appearance.space(5), image_pos.y + art - appearance.space(5)},
                appearance.space(4));
        const ImVec4 header_clip{image_pos.x + art + appearance.space(2), position.y,
                                 end.x - appearance.space(2) - tag_width, image_pos.y + art};
        if (!tag.empty()) {
            const bool tag_hovered = appearance.trailing_badge(
                tag.c_str(), {position.x, header_top, tile_width, slot_ink.y - slot_ink.x},
                locked ? muted : amber, 2);
            if (tag_hovered) {
                if (changes)
                    change_tooltip(rt, appearance, *rt.drafts[i]);
                else if (locked)
                    effect_tooltip(appearance, relic_lock_reason(info));
            }
        }
        draw->AddText(nullptr, 0, {header_clip.x, std::round(header_top - slot_ink.x)},
                      ImGui::GetColorU32(muted), slot_label.c_str(), nullptr, 0, &header_clip);
        draw->AddText(nullptr, 0,
                      {header_clip.x,
                       std::round(header_top + slot_ink.y - slot_ink.x + header_gap - name_ink.x)},
                      ImGui::GetColorU32(ivory), displayed_name.c_str(), nullptr, 0, &header_clip);
        if (displayed_name != name && ImGui::IsItemHovered() &&
            ImGui::IsMouseHoveringRect({header_clip.x, header_clip.y},
                                       {header_clip.z, header_clip.w}))
            effect_tooltip(appearance, name);
        draw->AddLine({position.x + appearance.space(2), position.y + appearance.space(13)},
                      {end.x - appearance.space(2), position.y + appearance.space(13)},
                      ImGui::GetColorU32(rgb(65, 80, 103)));
        if (relic) {
            const auto &effects = *tile.effects;
            float y = position.y + appearance.space(15);
            for (std::size_t effect = 0; effect < effects_per_relic; ++effect) {
                if (effects.rows[effect].effect == UINT32_MAX)
                    continue;
                const auto start = ImVec2(position.x + appearance.space(2), y);
                const ImVec4 clip{start.x + appearance.space(7), start.y,
                                  end.x - appearance.space(2),
                                  start.y + tile.rows[effect].effect_height};
                const bool effect_changed = rt.editing_supported() && !locked && rt.drafts[i] &&
                                            effects.rows[effect].effect !=
                                                rt.drafts[i]->original.effects.rows[effect].effect;
                const auto label = elide_text(tile.rows[effect].effect_label.c_str(), text_width);
                const auto alignment = align_row(start.y, tile.rows[effect].effect_height,
                                                 appearance.space(5), text_ink(label.c_str()));
                appearance.effect_icon(rt.presentation(), effects.rows[effect].effect,
                                       {start.x, alignment.icon_y}, appearance.space(5), locked);
                draw->AddText(nullptr, 0, {clip.x, alignment.text_y},
                              ImGui::GetColorU32(effect_changed ? amber : ivory), label.c_str(),
                              nullptr, 0, &clip);
                if (label != tile.rows[effect].effect_label && ImGui::IsItemHovered() &&
                    ImGui::IsMouseHoveringRect({clip.x, clip.y}, {clip.z, clip.w}))
                    effect_tooltip(appearance, tile.rows[effect].effect_label);
                y += tile.rows[effect].effect_height;
                if (!tile.rows[effect].curse_label.empty()) {
                    const bool curse_changed =
                        rt.editing_supported() && !locked && rt.drafts[i] &&
                        effects.rows[effect].curse !=
                            rt.drafts[i]->original.effects.rows[effect].curse;
                    const auto color = curse_changed                              ? amber
                                       : effects.rows[effect].curse == UINT32_MAX ? muted
                                                                                  : ice;
                    const ImVec4 curse_clip{clip.x, y, clip.z, y + tile.rows[effect].curse_height};
                    const auto curse_label =
                        elide_text(tile.rows[effect].curse_label.c_str(), text_width);
                    const auto curse_ink = text_ink(curse_label.c_str());
                    draw->AddText(nullptr, 0, {clip.x, std::round(y - curse_ink.x)},
                                  ImGui::GetColorU32(color), curse_label.c_str(), nullptr, 0,
                                  &curse_clip);
                    if (curse_label != tile.rows[effect].curse_label && ImGui::IsItemHovered() &&
                        ImGui::IsMouseHoveringRect({curse_clip.x, curse_clip.y},
                                                   {curse_clip.z, curse_clip.w}))
                        effect_tooltip(appearance, tile.rows[effect].curse_label);
                    y += tile.rows[effect].curse_height;
                }
                y += appearance.space(2);
            }
        }
        ImGui::SetCursorScreenPos({position.x, end.y});
        ImGui::Dummy({tile_width, 0});
        ImGui::EndGroup();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopFont();
}

void effect_field(const EditorView &rt, EditorState &state, const Appearance &appearance,
                  EffectPosition index, const Draft &draft) {
    ImGui::PushID(static_cast<int>(index.row));
    ImGui::PushID(static_cast<int>(index.field));
    const bool changed =
        effect_at(draft.effects, index) != effect_at(draft.original.effects, index);
    const auto *item = rt.items().find(draft.original.item_id);
    const auto count = item ? std::count_if(item->effect_tables.begin(), item->effect_tables.end(),
                                            [](auto table) { return table != -1; })
                            : 0;
    ImGui::BeginDisabled(!state.invalid && index.row >= static_cast<std::size_t>(count));
    const auto describe = [&](auto id) {
        return index.field == EffectField::Curse && id == UINT32_MAX
                   ? std::string("No curse")
                   : rt.presentation().describe(id);
    };
    const auto label = describe(effect_at(draft.effects, index));
    const auto position = ImGui::GetCursorScreenPos();
    const float width = ImGui::GetContentRegionAvail().x;
    const float badge_width =
        changed ? appearance.badge_size("Changed").x + appearance.space(2) : 0;
    const ImVec4 text_clip{position.x + appearance.space(9), position.y,
                           position.x + width - appearance.space(2) - badge_width,
                           position.y + appearance.space(9)};
    const float text_width = std::max(0.f, text_clip.z - text_clip.x);
    const auto displayed = elide_text(label.c_str(), text_width);
    const float height = appearance.space(9);
    const auto alignment =
        align_row(position.y, height, appearance.space(5), text_ink(displayed.c_str()));
    if (selection_card("##effect", state.selected_effect == index, {width, height})) {
        state.selected_effect = index;
        state.status = {};
    }
    auto *draw = ImGui::GetWindowDrawList();
    appearance.effect_icon(rt.presentation(), effect_at(draft.effects, index),
                           {position.x + appearance.space(2), alignment.icon_y},
                           appearance.space(5));
    if (text_width > 0)
        draw->AddText(nullptr, 0, {text_clip.x, alignment.text_y},
                      ImGui::GetColorU32(index.field == EffectField::Effect              ? ivory
                                         : effect_at(draft.effects, index) == UINT32_MAX ? muted
                                                                                         : ice),
                      displayed.c_str(), nullptr, 0, &text_clip);
    if (changed) {
        if (appearance.trailing_badge("Changed", {position.x, position.y, width, height}, amber))
            change_tooltip(rt, appearance, draft, index);
    } else if (displayed != label && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
               ImGui::IsMouseHoveringRect(
                   {text_clip.x, position.y},
                   {position.x + width - appearance.space(2), position.y + height}))
        effect_tooltip(appearance, label);
    ImGui::EndDisabled();
    ImGui::PopID();
    ImGui::PopID();
}

void effect_browser(const EditorView &rt, std::optional<EditorAction> &action, EditorState &state,
                    const Appearance &appearance, const Draft &draft) {
    const auto index = state.selected_effect;
    const auto choices = state.choice_cache.get(
        {rt.rules, rt.resources, draft.original.item_id, draft.effects, index, rt.context});
    const bool curses = choices->curses;
    const bool deep = choices->deep;
    vertical_gap(appearance.space(2));
    ImGui::BeginChild("effect-browser",
                      {0, std::max(appearance.space(52), ImGui::GetContentRegionAvail().y)},
                      ImGuiChildFlags_Borders);
    const float clear_width =
        deep || (state.invalid && curses)
            ? ImGui::CalcTextSize("No curse").x + ImGui::GetStyle().FramePadding.x * 2
            : 0.f;
    const float unavailable_width = deep ? 0.f
                                         : ImGui::GetFrameHeight() +
                                               ImGui::GetStyle().ItemInnerSpacing.x +
                                               ImGui::CalcTextSize("Show unavailable choices").x;
    const float invalid_width = ImGui::GetFrameHeight() + ImGui::GetStyle().ItemInnerSpacing.x +
                                ImGui::CalcTextSize("Allow invalid relics").x;
    ImGui::SetNextItemWidth(std::max(appearance.space(25),
                                     ImGui::GetContentRegionAvail().x - clear_width -
                                         unavailable_width - invalid_width - appearance.space(4)));
    if (ImGui::InputTextWithHint("##search", curses ? "Search curses..." : "Search effects...",
                                 state.effect_filter.InputBuf,
                                 sizeof(state.effect_filter.InputBuf)))
        state.effect_filter.Build();
    if (!deep) {
        ImGui::SameLine(0, appearance.space(2));
        checkbox("Show unavailable choices", &state.show_unavailable);
    }
    if (deep || (state.invalid && curses)) {
        ImGui::SameLine(0, appearance.space(2));
        ImGui::BeginDisabled(!choices->can_clear || effect_at(draft.effects, index) == UINT32_MAX);
        if (button("No curse", {clear_width, 0})) {
            action = SetEffect{state.selected, index, UINT32_MAX};
            state.status = {};
        }
        ImGui::EndDisabled();
    }
    ImGui::SameLine(0, appearance.space(2));
    if (checkbox("Allow invalid relics", &state.invalid))
        state.status = {};
    vertical_gap(appearance.space(2));
    const float results_height = std::floor(ImGui::GetContentRegionAvail().y);
    const float groups_width =
        std::min(appearance.space(40), ImGui::GetContentRegionAvail().x * .25f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::BeginChild("effect-groups", {groups_width, results_height}, ImGuiChildFlags_Borders);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {appearance.space(1), appearance.space(1)});
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {appearance.space(2), appearance.space(1)});
    ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign, {0, .5f});
    const auto group_button = [&](const char *label, bool selected) {
        return sidebar_item(label, selected,
                            {ImGui::GetContentRegionAvail().x, appearance.space(8)});
    };
    if (group_button(curses ? "All curses" : "All effects", !state.group_filter))
        state.group_filter.reset();
    for (const auto group : effect_groups)
        if (group_button(effect_group_name(group), state.group_filter == group))
            state.group_filter = group;
    ImGui::PopStyleVar(3);
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::SameLine(0, appearance.space(2));
    ImGui::BeginChild("effect-results", {0, results_height}, ImGuiChildFlags_Borders,
                      ImGuiWindowFlags_AlwaysVerticalScrollbar);
    bool previous_group = false;
    for (const auto group : effect_groups) {
        if (state.group_filter && group != *state.group_filter)
            continue;
        bool group_started = false;
        for (const auto &choice : choices->groups[static_cast<std::size_t>(group)]) {
            const bool available = std::holds_alternative<AvailableChoice>(choice.validation);
            if (!state.effect_filter.PassFilter(choice.description.c_str()) ||
                (!available && (deep || !state.show_unavailable)))
                continue;
            if (!group_started) {
                if (previous_group)
                    ImGui::Spacing();
                ImGui::TextColored(muted, "%s", effect_group_name(group));
                group_started = true;
                previous_group = true;
            }
            ImGui::PushID(static_cast<int>(choice.id));
            ImGui::BeginDisabled(!available);
            const auto row = ImGui::GetCursorScreenPos();
            const float row_width = ImGui::GetContentRegionAvail().x;
            const float tag_width =
                choice.tag.empty()
                    ? 0.f
                    : appearance.badge_size(choice.tag.c_str()).x + appearance.space(2);
            const float text_width = std::max(0.f, row_width - appearance.space(8) - tag_width);
            const float row_height =
                std::max(
                    appearance.space(5),
                    ImGui::CalcTextSize(choice.description.c_str(), nullptr, false, text_width).y) +
                appearance.space(2);
            if (ImGui::Selectable("##choice", effect_at(draft.effects, index) == choice.id, 0,
                                  {0, row_height})) {
                action = SetEffect{state.selected, index, choice.id};
                state.status = {};
            }
            const auto ink = text_ink(choice.description.c_str(), nullptr, 0, text_width);
            const auto alignment = align_row(row.y, row_height, appearance.space(5), ink);
            appearance.effect_icon(rt.presentation(), choice.id,
                                   {row.x + appearance.space(1), alignment.icon_y},
                                   appearance.space(5), !available);
            const ImVec4 text_clip{row.x + appearance.space(8), row.y,
                                   row.x + row_width - tag_width, row.y + row_height};
            if (text_width > 0)
                ImGui::GetWindowDrawList()->AddText(
                    nullptr, 0, {text_clip.x, alignment.text_y}, ImGui::GetColorU32(ivory),
                    choice.description.c_str(), nullptr, text_width, &text_clip);
            bool tag_hovered = false;
            if (!choice.tag.empty()) {
                tag_hovered = appearance.trailing_badge(
                    choice.tag.c_str(),
                    {row.x, row.y, ImGui::GetContentRegionAvail().x, row_height}, amber, 0);
            }
            ImGui::EndDisabled();
            if (tag_hovered)
                effect_tooltip(appearance, available ? "Choose a curse in the paired row."
                                                     : validation_reason(choice.validation));
            ImGui::PopID();
        }
    }
    if (!previous_group) {
        const bool filtered = state.group_filter || state.effect_filter.IsActive();
        empty_state(appearance, curses ? "No matching curses" : "No matching effects",
                    filtered ? "Try another search or category."
                             : "No compatible replacements are available.");
    }
    ImGui::EndChild();
    ImGui::EndChild();
}

bool detail(const EditorView &rt, std::optional<EditorAction> &action, EditorState &state,
            const Appearance &appearance, Relic relic) {
    const auto *info = rt.items().find(relic.item_id);
    const auto *reason = relic_lock_reason(info);
    const auto &draft = *rt.drafts[state.selected];
    const bool stale = draft.original != relic;
    const bool locked = reason || !rt.editing_supported();
    if (locked) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            {appearance.space(4), appearance.space(4)});
        ImGui::PushStyleColor(ImGuiCol_ChildBg, rgb(17, 25, 39, .45f));
        ImGui::BeginChild("equipped-effects", {0, 0},
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY,
                          ImGuiWindowFlags_NoScrollWithMouse);
        heading("Equipped effects", appearance.name_font);
        if (reason)
            notice({reason, muted});
        vertical_gap(appearance.space(2));
        const auto &effects =
            info && (info->kind == RelicKind::Unique || info->kind == RelicKind::Fixed)
                ? info->fixed_effects
                : relic.effects;
        for (std::size_t i = 0; i < effects_per_relic; ++i) {
            const auto &row = effects.rows[i];
            if (row.effect == UINT32_MAX && row.curse == UINT32_MAX)
                continue;
            appearance.effect_pair(rt.presentation(), row.effect, row.curse,
                                   info && info->family == RelicFamily::Deep);
            if (i + 1 < effects_per_relic)
                vertical_gap(appearance.space(2));
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    } else {
        ImGui::BeginDisabled(stale);
        const bool deep = info->family == RelicFamily::Deep;
        const bool curses = deep || state.invalid;
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {appearance.space(1), appearance.space(1)});
        if (ImGui::BeginTable("effect-fields", curses ? 2 : 1,
                              ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_NoSavedSettings |
                                  ImGuiTableFlags_NoPadOuterX)) {
            ImGui::TableSetupColumn("Effect", ImGuiTableColumnFlags_WidthStretch, 2.f);
            if (curses)
                ImGui::TableSetupColumn("Curse", ImGuiTableColumnFlags_WidthStretch, 1.f);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(muted, "Effects");
            if (curses) {
                ImGui::TableSetColumnIndex(1);
                ImGui::TextColored(muted, "Paired curses");
            }
            for (std::size_t i = 0; i < effects_per_relic; ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                effect_field(rt, state, appearance, {i, EffectField::Effect}, draft);
                if (curses) {
                    ImGui::TableSetColumnIndex(1);
                    effect_field(rt, state, appearance, {i, EffectField::Curse}, draft);
                }
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar();
        effect_browser(rt, action, state, appearance, draft);
        ImGui::EndDisabled();
    }
    return !locked;
}

struct DraftFeedback {
    std::string error;
    bool reload = false;
};

void draft_footer(const EditorView &rt, std::optional<EditorAction> &action, EditorState &state,
                  const Appearance &appearance, const EquippedRelics *equipped, UiRect editor,
                  const DraftFeedback &feedback) {
    const auto review = rt.review;
    const bool pending = review.relic_count != 0;
    const auto footer_position = ImGui::GetCursorScreenPos();
    const float footer_width = ImGui::GetContentRegionAvail().x;
    draw_diagnostics(rt, action, appearance, equipped, state.selected, editor);
    if (!rt.editing_supported()) {
        auto locked = read_only_notice(rt.editing_lock_reason());
        notification(locked, appearance, {footer_position.x, footer_position.y, footer_width, 0});
        return;
    }
    ImGui::SameLine();
    const float undo_width = appearance.space(16), discard_width = appearance.space(34),
                apply_width = appearance.space(34);
    ImGui::SetCursorPosX(std::max(
        ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - undo_width - discard_width -
                                    apply_width - ImGui::GetStyle().ItemSpacing.x * 2));
    ImGui::BeginDisabled(!rt.can_undo || !rt.editing_supported() || pending);
    if (button("Undo", {undo_width, 0})) {
        action = UndoEdit{};
        state.status = {};
    }
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("Restore every relic changed by the last Apply changes.");
    ImGui::SameLine();
    ImGui::BeginDisabled(!pending);
    if (button("Discard changes", {discard_width, 0})) {
        action = DiscardDrafts{};
        state.status = {};
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!rt.editing_supported() || !feedback.error.empty() || !pending);
    if (primary_button("Apply changes", {apply_width, 0})) {
        state.applied_focus =
            rt.drafts[state.selected]
                ? effect_at(rt.drafts[state.selected]->effects, state.selected_effect)
                : UINT32_MAX;
        action = ApplyDrafts{state.invalid};
    }
    ImGui::EndDisabled();
    Notice warning;
    Notice *message = &state.status;
    if (!feedback.error.empty()) {
        warning = ui::feedback("", feedback.error, NoticeKind::Warning);
        warning.dismissible = false;
        warning.action = feedback.reload                          ? NoticeAction::ReloadSlot
                         : state.selected != review.blocked->slot ? NoticeAction::ReviewSlot
                                                                  : NoticeAction::None;
        warning.slot = review.blocked->slot;
        message = &warning;
    }
    const auto selected_action =
        notification(*message, appearance, {footer_position.x, footer_position.y, footer_width, 0});
    if (selected_action != NoticeAction::None) {
        state.selected = message->slot;
        state.selected_effect = {};
        state.effect_filter.Clear();
        state.group_filter.reset();
        if (selected_action == NoticeAction::ReloadSlot)
            action = ReloadSlot{message->slot};
    }
}

} // namespace

void draw_editor(const EditorView &rt, std::optional<EditorAction> &action, EditorState &state,
                 const Appearance &appearance, const EquippedRelics *equipped, UiRect editor) {
    const auto review = rt.review;
    DraftFeedback feedback;
    if (review.blocked && !state.invalid) {
        const auto slot = review.blocked->slot;
        feedback.error = slot_failure_message({slot, review.blocked->rejection});
        feedback.reload = equipped && (!equipped->slots[slot] ||
                                       *equipped->slots[slot] != rt.drafts[slot]->original);
    }
    const float footer_height = ImGui::GetFrameHeight();
    const float footer_y =
        ImGui::GetCursorPosY() + ImGui::GetContentRegionAvail().y - footer_height;
    ImGui::BeginChild("editor-content",
                      {0, std::max(1.f, footer_y - ImGui::GetCursorPosY() - appearance.space(2) -
                                            notification_space(appearance))});
    if (equipped) {
        relic_tiles(rt, state, appearance, *equipped);
        if (rt.editing_supported()) {
            vertical_gap(appearance.space(2));
            if (equipped->slots[state.selected])
                detail(rt, action, state, appearance, *equipped->slots[state.selected]);
            else
                empty_state(appearance, "No relic equipped", "Equip a relic through Relic Rites.");
        }
    } else
        empty_state(appearance, "Relics unavailable",
                    std::get<InspectionMessage>(rt.inspection).text.c_str());
    ImGui::EndChild();
    ImGui::SetCursorPosY(footer_y);
    draft_footer(rt, action, state, appearance, equipped, editor, feedback);
}
} // namespace ern::ui
