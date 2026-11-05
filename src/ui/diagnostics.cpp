#include "ui/diagnostics.hpp"
#include "app/editor_view.hpp"
#include <algorithm>
#include <cmath>

namespace ern::ui {
void draw_diagnostics(const EditorView &rt, std::optional<EditorAction> &action,
                      const Appearance &appearance, const EquippedRelics *equipped,
                      std::size_t selected, UiRect editor) {
    const auto position = ImGui::GetCursorScreenPos();
    if (button("Diagnostics", {appearance.space(24), 0}))
        ImGui::OpenPopup("Diagnostics");
    const auto bounds = effect_popup_bounds(
        editor,
        {position.x, position.y - appearance.space(2),
         std::min(appearance.space(160), editor.width - appearance.space(9)), appearance.space(8)},
        appearance.space(160), appearance.space(3));
    ImGui::SetNextWindowPos({bounds.x, bounds.y + bounds.height}, ImGuiCond_Always, {0, 1});
    ImGui::SetNextWindowSizeConstraints({bounds.width, 0}, {bounds.width, bounds.height});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        {std::round(appearance.space(4)), std::round(appearance.space(4))});
    if (ImGui::BeginPopup("Diagnostics", ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                                             ImGuiWindowFlags_AlwaysAutoResize)) {
        heading("Diagnostics", appearance.name_font, ImGui::GetFrameHeight());
        const float action_width = appearance.space(18), gap = appearance.space(2);
        ImGui::SameLine();
        ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - action_width * 2 - gap);
        const bool refresh = button("Refresh", {action_width, 0});
        ImGui::SameLine(0, gap);
        if (button("Close", {action_width, 0}))
            ImGui::CloseCurrentPopup();
        vertical_gap(appearance.space(3));
        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, {appearance.space(1), appearance.space(1)});
        ImGui::PushStyleVar(ImGuiStyleVar_SeparatorTextBorderSize, 1.f);
        const auto row = [](const char *label, const std::string &value, ImVec4 color = ivory) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextColored(muted, "%s", label);
            ImGui::TableSetColumnIndex(1);
            ImGui::PushStyleColor(ImGuiCol_Text, color);
            ImGui::TextWrapped("%s", value.c_str());
            ImGui::PopStyleColor();
        };
        ImGui::SeparatorText("Session");
        if (ImGui::BeginTable("session-values", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed,
                                    appearance.space(24));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            row("Game", rt.version);
            row("Location", rt.location == EditLocation::RoundtableHold ? "Roundtable Hold"
                            : rt.location == EditLocation::OutsideRoundtable
                                ? "Outside Roundtable Hold"
                                : "Not loaded or unavailable");
            row("Catalogue", std::to_string(rt.catalog().size()) + " effects, " +
                                 std::to_string(rt.items().size()) + " relics");
            if (const auto *dlc = std::get_if<DlcState>(&rt.context))
                row("DLC", *dlc == DlcState::Present  ? "Installed"
                           : *dlc == DlcState::Absent ? "Not installed"
                                                      : "Unknown; both rule sets must agree");
            if (const auto *reason = rt.editing_lock_reason())
                row("Editing", reason, amber);
            if (equipped && equipped->slots[selected]) {
                const auto &relic = *equipped->slots[selected];
                row("Item ID", std::to_string(relic.item_id & 0x0FFFFFFF));
                row("Inventory index", std::to_string(relic.inventory_index()));
            }
            row("Edit journal", rt.journal_path.filename().string());
            ImGui::EndTable();
        }
        ImGui::Spacing();
        ImGui::SeparatorText("Regulation");
        const auto &diagnostic = rt.rule_diagnostics;
        const auto path_text = [](const std::filesystem::path &path) {
            const auto utf8 = path.u8string();
            return std::string(utf8.begin(), utf8.end());
        };
        if (ImGui::BeginTable("regulation-values", 2, ImGuiTableFlags_SizingFixedFit)) {
            ImGui::TableSetupColumn("Field", ImGuiTableColumnFlags_WidthFixed,
                                    appearance.space(24));
            ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch);
            if (diagnostic.requested_path.empty())
                row("Source", rt.source_label);
            else {
                const auto requested = path_text(diagnostic.requested_path),
                           resolved = path_text(diagnostic.resolved_path);
                row("Requested file", requested.empty() ? "Unavailable" : requested);
                row("Opened file", resolved.empty() ? "Unavailable" : resolved);
                row("Detected hash",
                    diagnostic.detected_hash.empty() ? "Unavailable" : diagnostic.detected_hash);
                row("Validation hash", rt.source_hash.empty() ? "Unavailable" : rt.source_hash);
                row("Rules",
                    !diagnostic.error.empty()                    ? diagnostic.error
                    : diagnostic.detected_hash == rt.source_hash ? "Match the active regulation.bin"
                                                                 : "Not checked",
                    !diagnostic.error.empty() ? amber : ivory);
                row("Local assets",
                    std::to_string(rt.assets().item_names.size()) + " item names, " +
                        std::to_string(rt.assets().effect_names.size()) + " effect names, " +
                        std::to_string(rt.assets().cells.size()) + " icons");
                if (!rt.assets().warnings.empty())
                    row("Asset fallback", rt.assets().warnings.front(), amber);
            }
            ImGui::EndTable();
        }
        ImGui::PopStyleVar(2);
        // The application worker publishes the refreshed state independently of drawing.
        if (refresh)
            action = RefreshInspection{};
        ImGui::EndPopup();
    }
    ImGui::PopStyleVar();
}
} // namespace ern::ui
