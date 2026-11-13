#include "ui/ui.hpp"
#include "app/editor_view.hpp"
#include "ui/edit_feedback.hpp"
#include <algorithm>
#include "ui/editor.hpp"
#include "ui/presets.hpp"
#include <cmath>

namespace ern {
namespace {
template <typename... Cases> struct Match : Cases... {
    using Cases::operator()...;
};
} // namespace

void EditorUi::consume(const EditorView &view) {
    if (session_ != view.session) {
        session_ = view.session;
        state_.status = {};
        state_.applied_focus.reset();
        browser_.feedback = {};
    }
    if (!view.completion || completed_ == view.completion->operation.value)
        return;
    completed_ = view.completion->operation.value;
    const auto &action = view.completion->action;
    const bool preset_action = std::holds_alternative<SavePreset>(action) ||
                               std::holds_alternative<DeletePreset>(action) ||
                               std::holds_alternative<ReloadPresets>(action) ||
                               std::holds_alternative<LoadPreset>(action);
    const auto reject = [&](std::string text) {
        auto message =
            ui::feedback(preset_action ? "Preset operation failed" : "Changes weren't applied",
                         std::move(text), ui::NoticeKind::Error);
        if (!preset_action && view.review.blocked) {
            message.slot = view.review.blocked->slot;
            message.action = view.review.blocked->rejection.reason == EditRejectReason::Stale
                                 ? ui::NoticeAction::ReloadSlot
                                 : ui::NoticeAction::ReviewSlot;
        }
        (preset_action ? browser_.feedback : state_.status) = std::move(message);
    };
    std::visit(
        Match{[](const std::monostate &) {},
              [&](const EditRejected &result) { reject(ui::rejection_message(result)); },
              [&](const OperationFailed &result) { reject("Operation failed: " + result.detail); },
              [&](PresetStoreError result) { reject(ui::preset_store_message(result)); },
              [&](const AppliedDrafts &applied) {
                  const auto &result = applied.result;
                  if (state_.applied_focus && *state_.applied_focus != UINT32_MAX &&
                      view.drafts[state_.selected]) {
                      const auto &effects = view.drafts[state_.selected]->effects;
                      state_.selected_effect = {};
                      for (std::size_t row = 0; row < effects.rows.size(); ++row) {
                          if (effects.rows[row].effect == *state_.applied_focus) {
                              state_.selected_effect = {row, EffectField::Effect};
                              break;
                          }
                          if (effects.rows[row].curse == *state_.applied_focus) {
                              state_.selected_effect = {row, EffectField::Curse};
                              break;
                          }
                      }
                  }
                  state_.applied_focus.reset();
                  if (!result.failure)
                      state_.status = ui::feedback(
                          "Changes applied",
                          "Applied changes to " + std::to_string(result.applied) +
                              (result.applied == 1 ? " relic. Save through the game."
                                                   : " relics. Save through the game."),
                          ui::NoticeKind::Success);
                  else {
                      state_.status = ui::feedback(
                          "Some changes weren't applied",
                          (result.applied ? "Applied " + std::to_string(result.applied) +
                                                (result.applied == 1 ? " relic. " : " relics. ")
                                          : "") +
                              ui::slot_failure_message(*result.failure),
                          ui::NoticeKind::Warning);
                      state_.status.slot = result.failure->slot;
                      state_.status.action = ui::NoticeAction::ReviewSlot;
                  }
              },
              [&](const UndoneEdit &result) {
                  const auto count = result.result.restored;
                  const auto restored = "Undid changes to " + std::to_string(count) +
                                        (count == 1 ? " relic. " : " relics. ");
                  if (result.result.failure)
                      reject((count ? restored : "") +
                             ui::slot_failure_message(*result.result.failure));
                  else if (count)
                      state_.status =
                          ui::feedback("Changes undone", restored + "Save through the game.",
                                       ui::NoticeKind::Success);
                  else
                      state_.status =
                          ui::feedback("Nothing to undo", "No changes to undo in this session.",
                                       ui::NoticeKind::Info);
              },
              [&](const SavedPresetResult &result) {
                  browser_.selected = result.name;
                  browser_.feedback =
                      ui::feedback("Preset saved",
                                   "Saved " + std::to_string(result.count) +
                                       (result.count == 1 ? " equipped relic template."
                                                          : " equipped relic templates."),
                                   ui::NoticeKind::Success);
              },
              [&](const DeletedPresetResult &) {
                  browser_.feedback = ui::feedback("Preset deleted", "The preset was removed.",
                                                   ui::NoticeKind::Success);
              },
              [&](const LoadedPresetResult &result) {
                  const auto count = std::count_if(
                      result.staged.drafts.slots.begin(), result.staged.drafts.slots.end(),
                      [](const auto &slot) { return slot.has_value(); });
                  for (std::size_t i = 0; i < result.staged.drafts.slots.size(); ++i)
                      if (result.staged.drafts.slots[i]) {
                          state_.selected = i;
                          break;
                      }
                  state_.selected_effect = {};
                  state_.effect_filter.Clear();
                  state_.group_filter.reset();
                  const bool skipped = static_cast<std::size_t>(count) < result.requested;
                  state_.status =
                      ui::feedback("Preset loaded into drafts",
                                   "Loaded " + std::to_string(count) +
                                       (count == 1 ? " relic draft." : " relic drafts."),
                                   skipped ? ui::NoticeKind::Warning : ui::NoticeKind::Success);
                  if (skipped)
                      state_.status.text += " Incompatible slots were skipped.";
                  page_ = Page::Editor;
              }},
        view.completion->outcome);
}
void EditorUi::initialize(const EditorView &view, float display_height) {
    appearance_.initialize(view.assets(), *view.fonts, display_height);
}

void EditorUi::draw(const EditorView &rt, bool pending, std::optional<EditorAction> &action,
                    bool &open) {
    consume(rt);
    if (std::none_of(rt.presets->begin(), rt.presets->end(),
                     [this](const auto &entry) { return entry.name == browser_.selected; }))
        browser_.selected = rt.presets->empty() ? "" : rt.presets->front().name;
    const auto display = ImGui::GetIO().DisplaySize;
    appearance_.scale = editor_scale(display.y);
    const auto bounds = editor_bounds(display.x, display.y);
    ImGui::SetNextWindowPos({bounds.x, bounds.y}, ImGuiCond_Always);
    ImGui::SetNextWindowSize({bounds.width, bounds.height}, ImGuiCond_Always);
    // Auto-sized panels truncate their size. Whole-pixel padding avoids spurious scrollbars.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {appearance_.space(3), appearance_.space(3)});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {appearance_.space(2), appearance_.space(1)});
    ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
                        {appearance_.space(2), appearance_.space(1)});
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, appearance_.space(4));
    if (!ImGui::Begin("Relic editor", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
        ImGui::End();
        ImGui::PopStyleVar(4);
        return;
    }
    const float control_height = appearance_.space(9);
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                        {appearance_.space(2), (control_height - ImGui::GetFontSize()) / 2});
    const auto position = ImGui::GetWindowPos(), window_size = ImGui::GetWindowSize();
    const UiRect editor{position.x, position.y, window_size.x, window_size.y};
    const auto *equipped = std::get_if<EquippedRelics>(&rt.inspection);
    ui::heading("Relic editor", appearance_.title_font, control_height);
    ImGui::SameLine();
    const float nav_width = appearance_.space(26), close_width = appearance_.space(26);
    ImGui::SetCursorPosX(ImGui::GetWindowContentRegionMax().x - nav_width - close_width -
                         ImGui::GetStyle().ItemSpacing.x);
    ImGui::BeginDisabled(pending || (page_ == Page::Editor && !rt.editing_supported()));
    const char *navigation = pending                  ? "Updating...###page-navigation"
                             : page_ == Page::Presets ? "Editor###page-navigation"
                                                      : "Presets###page-navigation";
    if (ui::button(navigation, {nav_width, 0})) {
        page_ = page_ == Page::Presets ? Page::Editor : Page::Presets;
        if (page_ == Page::Presets) {
            browser_.feedback = {};
            action = ReloadPresets{};
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ui::button(("Close (" + rt.hotkey_name + ")").c_str(), {close_width, 0}))
        open = false;
    ui::vertical_gap(appearance_.space(2));
    ImGui::BeginChild("page-content", {0, 0});
    ImGui::BeginDisabled(pending);
    if (page_ == Page::Presets)
        ui::draw_presets(rt, action, browser_, appearance_, equipped, editor, state_.invalid);
    else
        ui::draw_editor(rt, action, state_, appearance_, equipped, editor);
    ImGui::EndDisabled();
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::End();
    ImGui::PopStyleVar(4);
}
} // namespace ern
