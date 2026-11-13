#include "support/check.hpp"
#include "support/synthetic_rules.hpp"
#include "support/synthetic_assets.hpp"
#include "data/system_fonts.hpp"
#include "ui/editor.hpp"
#include "ui/presets.hpp"
#include "ui/diagnostics.hpp"
#include "ui/ui.hpp"
#include <imgui_internal.h>
#include <cmath>
#include <cstring>

namespace {
using test::require;

void add_icons(ern::GameAssets &assets, const ern::RelicValidator &rules) {
    // Procedural framed icons exercise the texture path without distributing game artwork.
    auto &atlas = assets.atlas;
    atlas.width = ern::IconAtlas::columns * ern::IconAtlas::cell_size;
    atlas.height = ern::IconAtlas::cell_size;
    atlas.rgba.resize(std::size_t(atlas.width) * atlas.height * 4);
    constexpr std::array<std::array<unsigned char, 3>, 5> colors{
        {{{166, 54, 46}}, {{175, 144, 67}}, {{54, 126, 81}}, {{65, 107, 151}}, {{111, 70, 129}}}};
    for (unsigned int cell = 0; cell < colors.size(); ++cell)
        for (unsigned int y = 0; y < ern::IconAtlas::icon_size; ++y)
            for (unsigned int x = 0; x < ern::IconAtlas::icon_size; ++x) {
                const auto offset =
                    (std::size_t(y + ern::IconAtlas::gutter) * atlas.width +
                     cell * ern::IconAtlas::cell_size + x + ern::IconAtlas::gutter) *
                    4;
                const auto plate_top = cell == 0 ? 24u : 12u;
                const auto plate_bottom = cell == 0 ? 156u : 148u;
                const bool plate = x >= 12 && x < 148 && y >= plate_top && y < plate_bottom;
                const bool border =
                    plate && (x < 18 || x >= 142 || y < plate_top + 6 || y >= plate_bottom - 6);
                const bool glyph = plate && x >= 68 && x < 92 && y >= 32 && y < 128;
                const bool marker = cell == 0 && x >= 12 && x < 18 && y >= 4 && y < 24;
                for (unsigned int channel = 0; channel < 3; ++channel)
                    atlas.rgba[offset + channel] = border || glyph ? 207 : colors[cell][channel];
                atlas.rgba[offset + 3] = plate || marker ? 255 : 0;
            }
    for (const auto &[id, effect] : rules.effects().entries())
        assets.cells[{ern::IconKind::Status, static_cast<std::uint32_t>(effect.icon)}] = id % 4;
    assets.cells[{ern::IconKind::Status, 7000400}] = 0;
    assets.cells[{ern::IconKind::Status, 7000402}] = 0;
    assets.cells[{ern::IconKind::Status, 7000601}] = 1;
    assets.cells[{ern::IconKind::Status, 7001801}] = 2;
    assets.cells[{ern::IconKind::Item, 0}] = 4;
}

ern::EditorView fixture(bool deep, bool wrapped) {
    ern::EditorView view;
    auto rules = synthetic_rules();
    for (auto &[id, effect] : rules.effects)
        effect.icon = static_cast<std::int32_t>(id);
    view.rules = std::make_shared<const ern::RelicValidator>(std::move(rules));
    auto assets = synthetic_assets();
    assets.effect_names[7000400] = "Strength +1";
    assets.effect_names[7000401] = "Strength +2";
    assets.effect_names[7000402] =
        wrapped
            ? "[Fixture] Standard attacks gain a follow-up after using the character skill with a compatible armament equipped"
            : "Strength +3";
    add_icons(assets, *view.rules);
    view.resources = std::make_shared<const ern::GameAssets>(std::move(assets));
    view.fonts = std::make_shared<const ern::SystemFonts>(ern::read_system_fonts());
    view.presets = std::make_shared<const std::vector<ern::SavedPreset>>();
    view.context = ern::DlcState::Present;
    ern::EquippedRelics equipped;
    const std::size_t slot = deep ? 3 : 0;
    auto effects = ern::empty_effects;
    effects.rows[0].effect = 7000400;
    effects.rows[1].effect = 7000601;
    effects.rows[2].effect = 7001801;
    if (deep)
        effects.rows[2].curse = 900001;
    equipped.slots[slot] = ern::Relic{1, 0x80000000u | (deep ? 2000000u : 102u), effects};
    view.drafts[slot] = ern::Draft{*equipped.slots[slot], effects};
    view.inspection = equipped;
    view.review = ern::review_drafts(view.drafts, &equipped, view.validator(), view.context);
    return view;
}

struct Frame {
    explicit Frame(float height, const ern::EditorView &view) {
        ImGui::CreateContext();
        appearance.initialize(view.assets(), *view.fonts, height);
        auto &io = ImGui::GetIO();
        io.DisplaySize = {height * 16 / 9, height};
        io.DeltaTime = 1.f / 60;
        unsigned char *pixels = nullptr;
        int width = 0, atlas_height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &atlas_height);
    }
    ~Frame() { ImGui::DestroyContext(); }
    Frame(const Frame &) = delete;
    Frame &operator=(const Frame &) = delete;

    void begin() const {
        ImGui::NewFrame();
        const auto bounds =
            ern::editor_bounds(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y);
        ImGui::SetNextWindowPos({bounds.x, bounds.y});
        ImGui::SetNextWindowSize({bounds.width, bounds.height});
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                            {appearance.space(3), appearance.space(3)});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, {appearance.space(2), appearance.space(1)});
        ImGui::PushStyleVar(ImGuiStyleVar_ItemInnerSpacing,
                            {appearance.space(2), appearance.space(1)});
        ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, appearance.space(4));
        ImGui::Begin("render-check", nullptr,
                     ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoScrollbar);
        const float control_height = appearance.space(9);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            {appearance.space(2), (control_height - ImGui::GetFontSize()) / 2});
    }
    static void end() {
        ImGui::PopStyleVar();
        ImGui::End();
        ImGui::PopStyleVar(4);
        ImGui::Render();
    }
    ern::ui::Appearance appearance;
};

const ImGuiWindow &child(const char *name) {
    for (const auto *window : ImGui::GetCurrentContext()->Windows)
        if (std::strstr(window->Name, name))
            return *window;
    for (const auto *window : ImGui::GetCurrentContext()->Windows)
        std::cerr << window->Name << '\n';
    throw std::runtime_error("Expected rendered child was not found");
}

void hover_tag(const ImGuiWindow &window) {
    const auto amber = ImGui::ColorConvertFloat4ToU32(ern::ui::amber);
    for (const auto &vertex : window.DrawList->VtxBuffer)
        if (vertex.col == amber) {
            ImGui::GetIO().AddMousePosEvent(vertex.pos.x + 2, vertex.pos.y + 2);
            return;
        }
    throw std::runtime_error("The rendered field has no change tag to hover");
}

bool tooltip_visible() {
    for (const auto *window : ImGui::GetCurrentContext()->Windows)
        if (window->Active && window->Flags & ImGuiWindowFlags_Tooltip)
            return true;
    return false;
}

struct EditorGeometry {
    float browser_y;
    float carousel_height;
};

EditorGeometry draw_editor(Frame &frame, const ern::EditorView &view, ern::ui::EditorState &state) {
    std::optional<ern::EditorAction> action;
    const auto &equipped = std::get<ern::EquippedRelics>(view.inspection);
    for (int i = 0; i < 3; ++i) {
        frame.begin();
        ern::ui::draw_editor(
            view, action, state, frame.appearance, &equipped,
            ern::editor_bounds(ImGui::GetIO().DisplaySize.x, ImGui::GetIO().DisplaySize.y));
        Frame::end();
    }
    return {child("/effect-browser_").Pos.y,
            child(view.editing_supported() ? "/relics_" : "/relic-grid_").Size.y};
}

void comparison_space(float height, bool deep, bool wrapped) {
    auto view = fixture(deep, wrapped);
    Frame frame(height, view);
    ern::ui::EditorState state;
    state.selected = deep ? 3 : 0;
    const auto before = draw_editor(frame, view, state);
    require(child("/effect-browser_").Size.y >= height * .4f,
            "Effect fields leave too little space for the effect browser");
    auto &draft = *view.drafts[state.selected];
    draft.effects.rows[0].effect = 7000402;
    if (deep)
        draft.effects.rows[0].curse = 900002;
    view.review = ern::review_drafts(view.drafts, &std::get<ern::EquippedRelics>(view.inspection),
                                     view.validator(), view.context);
    const auto changed = draw_editor(frame, view, state);
    std::cout << "height=" << height << " deep=" << deep << " wrapped=" << wrapped
              << " browser=" << before.browser_y << "/" << changed.browser_y
              << " carousel=" << before.carousel_height << "/" << changed.carousel_height << '\n';
    require(std::abs(before.browser_y - changed.browser_y) <= 1.f,
            "Changing an effect moved the effect browser");
    require(std::abs(before.carousel_height - changed.carousel_height) <= 1.f,
            "Changing an effect resized the relic carousel");
    draft.effects = draft.original.effects;
    view.review = ern::review_drafts(view.drafts, &std::get<ern::EquippedRelics>(view.inspection),
                                     view.validator(), view.context);
    const auto restored = draw_editor(frame, view, state);
    require(std::abs(before.browser_y - restored.browser_y) <= 1.f,
            "Restoring the original effect moved the effect browser");
    if (deep) {
        const auto groups = child("/effect-groups_").Pos;
        const auto results = child("/effect-results_").Pos;
        state.selected_effect = {0, ern::EffectField::Curse};
        const auto curses = draw_editor(frame, view, state);
        require(before.browser_y == curses.browser_y &&
                    groups.y == child("/effect-groups_").Pos.y &&
                    results.y == child("/effect-results_").Pos.y &&
                    results.x == child("/effect-results_").Pos.x,
                "Switching between effects and curses moved the browser by a pixel");
    }
}

void button_alignment(float height) {
    const auto view = fixture(false, false);
    Frame frame(height, view);
    for (const bool disabled : {false, true})
        for (const float button_height :
             {0.f, frame.appearance.space(8), frame.appearance.space(12)}) {
            float baseline = FLT_MAX;
            for (const auto *label : {"Undo", "Discard changes", "Apply changes", "Dismiss",
                                      "Cancel##confirmation", "Updating..."}) {
                frame.begin();
                ImGui::BeginDisabled(disabled);
                const auto *draw = ImGui::GetWindowDrawList();
                const auto first = draw->VtxBuffer.Size;
                const auto color = ImGui::GetColorU32(ImGuiCol_Text);
                const ImVec2 size{frame.appearance.space(40), button_height};
                if (std::strcmp(label, "Apply changes") == 0)
                    ern::ui::primary_button(label, size);
                else
                    ern::ui::button(label, size);
                const auto min = ImGui::GetItemRectMin(), max = ImGui::GetItemRectMax();
                float top = FLT_MAX, bottom = -FLT_MAX, left = FLT_MAX, right = -FLT_MAX;
                float origin = FLT_MAX;
                for (int i = first; i < draw->VtxBuffer.Size; ++i) {
                    const auto &v = draw->VtxBuffer[i];
                    if (v.col == color) {
                        if (origin == FLT_MAX)
                            origin = v.pos.y - ImGui::GetFont()->FindGlyph(*label)->Y0;
                        top = std::min(top, v.pos.y);
                        bottom = std::max(bottom, v.pos.y);
                        left = std::min(left, v.pos.x);
                        right = std::max(right, v.pos.x);
                    }
                }
                const float rendered_baseline = origin + ImGui::GetFont()->Ascent;
                if (baseline == FLT_MAX)
                    baseline = rendered_baseline;
                std::cout << "height=" << height << " button=" << label
                          << " baseline=" << rendered_baseline << '\n';
                require(bottom > top, "The button label was not rendered");
                require(rendered_baseline == baseline,
                        "Button labels with different ascenders and descenders changed baseline");
                if (std::strcmp(label, "Dismiss") == 0)
                    require(std::abs(top + bottom - min.y - max.y) <= 2.f,
                            "The shared button baseline does not center the capital-height label");
                require(std::abs(left + right - min.x - max.x) <= 4.f,
                        "The button label is not horizontally centered");
                ImGui::EndDisabled();
                Frame::end();
            }
        }
}

void checkbox_alignment(float height) {
    const auto view = fixture(false, false);
    Frame frame(height, view);
    bool checked = false, disabled = false, changed = false;
    ImRect bounds;
    const auto render = [&] {
        frame.begin();
        ImGui::BeginDisabled(disabled);
        const auto *draw = ImGui::GetWindowDrawList();
        const auto color = ImGui::GetColorU32(ImGuiCol_Text);
        const auto origin = [&](int first, char character) {
            for (int i = first; i < draw->VtxBuffer.Size; ++i)
                if (draw->VtxBuffer[i].col == color)
                    return draw->VtxBuffer[i].pos.y - ImGui::GetFont()->FindGlyph(character)->Y0;
            throw std::runtime_error("The control label was not rendered");
        };
        auto first = draw->VtxBuffer.Size;
        ern::ui::button("Dismiss");
        const float button_origin = origin(first, 'D');
        ImGui::SameLine();
        first = draw->VtxBuffer.Size;
        changed = ern::ui::checkbox("Show unavailable choices##alignment", &checked);
        bounds = {ImGui::GetItemRectMin(), ImGui::GetItemRectMax()};
        require(origin(first, 'S') == button_origin,
                "The checkbox label does not share the button's baseline");
        ImGui::EndDisabled();
        Frame::end();
    };
    for (const bool disable : {false, true}) {
        disabled = disable;
        for (const bool value : {false, true}) {
            checked = value;
            render();
            require(checked == value && !changed, "Rendering changed the checkbox value");
        }
        for (const bool label : {false, true}) {
            const bool before = checked;
            ImGui::GetIO().AddMousePosEvent(label ? bounds.Max.x - frame.appearance.space(2)
                                                  : bounds.Min.x + frame.appearance.space(2),
                                            (bounds.Min.y + bounds.Max.y) / 2);
            render();
            ImGui::GetIO().AddMouseButtonEvent(0, true);
            render();
            ImGui::GetIO().AddMouseButtonEvent(0, false);
            render();
            require(disabled ? checked == before && !changed : checked != before && changed,
                    "Clicking the checkbox square or label did not respect its enabled state");
            render();
            require(!changed, "The checkbox reported the same change twice");
        }
    }
}

void preset_padding(float height) {
    auto view = fixture(false, false);
    ern::Preset preset;
    preset.slots[0] = view.drafts[0]->effects;
    preset.slots[3] = view.drafts[0]->effects;
    preset.slots[3]->rows[2].curse = 900001;
    view.presets = std::make_shared<const std::vector<ern::SavedPreset>>(
        std::vector<ern::SavedPreset>{{"Fixture preset", preset}});
    Frame frame(height, view);
    ern::ui::PresetBrowser browser;
    browser.selected = "Fixture preset";
    std::optional<ern::EditorAction> action;
    const auto render = [&] {
        for (int i = 0; i < 3; ++i) {
            frame.begin();
            ern::ui::draw_presets(view, action, browser, frame.appearance,
                                  &std::get<ern::EquippedRelics>(view.inspection),
                                  ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
            Frame::end();
        }
    };
    render();
    for (const auto *window : ImGui::GetCurrentContext()->Windows)
        if (window->Active && std::strstr(window->Name, "/preset-slot_"))
            require(window->ScrollMax.y == 0, "A preset card clips its effects or curses");
    const auto &list = child("/saved-preset-list_");
    // Text vertices measure the visible glyphs, including the font's ascent and descent.
    float top = height, bottom = 0, card_top = height, card_bottom = 0, left = FLT_MAX, right = 0;
    const auto ivory = ImGui::ColorConvertFloat4ToU32(ern::ui::ivory);
    const auto muted = ImGui::ColorConvertFloat4ToU32(ern::ui::muted);
    const auto selected = ImGui::ColorConvertFloat4ToU32(ern::ui::rgb(32, 47, 72));
    for (const auto &vertex : list.DrawList->VtxBuffer) {
        if (vertex.col == ivory || vertex.col == muted) {
            top = std::min(top, vertex.pos.y);
            bottom = std::max(bottom, vertex.pos.y);
        }
        if (vertex.col == selected) {
            card_top = std::min(card_top, vertex.pos.y);
            card_bottom = std::max(card_bottom, vertex.pos.y);
            left = std::min(left, vertex.pos.x);
            right = std::max(right, vertex.pos.x);
        }
    }
    require(bottom > top && card_bottom > card_top, "Preset card was not rendered");
    std::cout << "height=" << height << " preset padding=" << top - card_top << "/"
              << card_bottom - bottom << '\n';
    require(std::abs((top - card_top) - (card_bottom - bottom)) <= 2.f,
            "Preset card has uneven visible top and bottom padding");
    require(std::abs(left - list.WorkRect.Min.x) <= 1 && std::abs(right - list.WorkRect.Max.x) <= 1,
            "The selected preset does not fill the sidebar width");
    std::copy_n("missing", sizeof("missing"), browser.filter.InputBuf);
    browser.filter.Build();
    render();
    const auto &filtered = child("/saved-preset-list_").DrawList->VtxBuffer;
    require(std::none_of(filtered.begin(), filtered.end(),
                         [&](const auto &v) { return v.col == selected; }),
            "Preset search left a nonmatching row visible");
    require(browser.selected == "Fixture preset", "Searching presets changed the selected preview");
    const auto &body = child("/preset-browser_");
    const float body_height = body.Size.y;
    browser.feedback = ern::ui::feedback("Preset saved", "Saved the equipped relics.",
                                         ern::ui::NoticeKind::Success);
    render();
    const auto &banner = child("##notification");
    const float banner_y = banner.Pos.y;
    require(body.Size.y == body_height &&
                body.Pos.y + body.Size.y <= banner_y - frame.appearance.space(2) + 1,
            "The preset notification overlapped its content instead of using reserved space");
    view.lock_reason = "Return to Roundtable Hold to edit relics.";
    render();
    require(banner.Active && banner.Pos.y == banner_y && body.Size.y == body_height,
            "The preset read-only notice did not use the common footer position");
}

void preset_replacement(float height) {
    auto view = fixture(false, false);
    ern::Preset preset;
    preset.slots[0] = view.drafts[0]->effects;
    view.presets = std::make_shared<const std::vector<ern::SavedPreset>>(
        std::vector<ern::SavedPreset>{{"Fixture preset", preset}});
    Frame frame(height, view);
    ern::ui::PresetBrowser browser;
    browser.selected = "Fixture preset";
    std::copy_n("fixture PRESET", sizeof("fixture PRESET"), browser.name);
    std::optional<ern::EditorAction> action;
    const auto render = [&] {
        for (int i = 0; i < 3; ++i) {
            frame.begin();
            ern::ui::draw_presets(view, action, browser, frame.appearance,
                                  &std::get<ern::EquippedRelics>(view.inspection),
                                  ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
            Frame::end();
        }
    };
    const auto activate = [&](const char *window, const char *label) {
        ImGui::ActivateItemByID(ImHashStr(label, 0, child(window).ID));
        render();
    };
    render();
    activate("render-check", "Replace preset");
    require(!action && browser.pending_replace == "Fixture preset" &&
                child("Replace preset?").Active,
            "A case-alias save skipped replacement confirmation");
    activate("Replace preset?", "Cancel");
    require(!action && browser.pending_replace.empty(),
            "Canceling preset replacement emitted a save command");
    activate("render-check", "Replace preset");
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, true);
    render();
    ImGui::GetIO().AddKeyEvent(ImGuiKey_Escape, false);
    render();
    require(!action && browser.pending_replace.empty(),
            "Escape from preset replacement emitted a save command");
    activate("render-check", "Replace preset");
    std::copy_n("Another name", sizeof("Another name"), browser.name);
    activate("Replace preset?", "Replace preset");
    const auto *replace = action ? std::get_if<ern::SavePreset>(&*action) : nullptr;
    require(replace && replace->name == "Fixture preset" &&
                replace->mode == ern::PresetSaveMode::Replace && browser.pending_replace.empty(),
            "Confirmed replacement lost its frozen target name or replacement intent");
    action.reset();
    activate("render-check", "Save preset");
    const auto *create = action ? std::get_if<ern::SavePreset>(&*action) : nullptr;
    require(create && create->name == "Another name" &&
                create->mode == ern::PresetSaveMode::Create &&
                ImGui::GetCurrentContext()->OpenPopupStack.empty(),
            "Saving a new name required confirmation or retained replacement intent");
}

void glyph_bounds(float height) {
    const auto view = fixture(false, false);
    Frame frame(height, view);
    frame.begin();
    for (const char *text : {"Ready", "Agjp", "Łódź", "\nStrength +3", "Agjp\r\nStrength +3",
                             "A wrapped effect description with several words and descenders"}) {
        auto *draw = ImGui::GetWindowDrawList();
        const int first_vertex = draw->VtxBuffer.Size;
        const auto cursor = ImGui::GetCursorScreenPos();
        const ImVec2 origin{std::floor(cursor.x), std::floor(cursor.y)};
        draw->AddText(nullptr, 0, origin, IM_COL32_WHITE, text, nullptr, 120);
        float top = height, bottom = 0;
        for (int i = first_vertex; i < draw->VtxBuffer.Size; ++i) {
            top = std::min(top, draw->VtxBuffer[i].pos.y - origin.y);
            bottom = std::max(bottom, draw->VtxBuffer[i].pos.y - origin.y);
        }
        const auto ink = ern::ui::text_ink(text, nullptr, 0, 120);
        require(std::abs(top - ink.x) < .01f && std::abs(bottom - ink.y) < .01f,
                "Text measurement differs from the rendered glyph bounds");
    }
    Frame::end();
}

void read_only_and_pending(float height) {
    auto view = fixture(false, true);
    view.hotkey_name = "F8";
    Frame frame(height, view);
    ern::EditorUi ui;
    ui.initialize(view, height);
    unsigned char *pixels = nullptr;
    int width = 0, atlas_height = 0;
    ImGui::GetIO().Fonts->GetTexDataAsRGBA32(&pixels, &width, &atlas_height);
    std::optional<ern::EditorAction> action;
    bool open = true;
    const auto draw = [&](bool pending) {
        for (int i = 0; i < 3; ++i) {
            ImGui::NewFrame();
            ui.draw(view, pending, action, open);
            ImGui::Render();
        }
    };
    draw(false);
    const auto page_y = child("/page-content_").Pos.y;
    draw(true);
    require(child("/page-content_").Pos.y == page_y,
            "The pending indicator moved the editor content");
    view.lock_reason =
        "Relic editing is only available in Roundtable Hold. Return there to make changes.";
    draw(false);
    const auto &grid = child("/relic-grid_");
    require(grid.Active && grid.ScrollMax.x == 0 && grid.ScrollMax.y == 0,
            "The read-only relic grid is hidden or requires scrolling");
    for (const auto *window : ImGui::GetCurrentContext()->Windows)
        require(!window->Active || !std::strstr(window->Name, "/equipped-effects_"),
                "The locked editor still displays the redundant equipped-effects panel");
    require(!child("/effect-browser_").Active,
            "The effect browser remained interactive in read-only mode");
    require(!action, "Read-only rendering emitted an editor action");
    require(child("/page-content_").Pos.y == page_y,
            "Read-only mode moved the page down for its notice");
    const auto &banner = child("##notification");
    require(banner.Active && banner.Pos.y > grid.Pos.y + grid.Size.y,
            "The Roundtable notice was not placed in the reserved footer area");
    float text_top = height, text_bottom = 0;
    const auto white = ImGui::GetIO().Fonts->TexUvWhitePixel;
    for (const auto &vertex : banner.DrawList->VtxBuffer)
        if (vertex.uv.x != white.x || vertex.uv.y != white.y) {
            text_top = std::min(text_top, vertex.pos.y);
            text_bottom = std::max(text_bottom, vertex.pos.y);
        }
    require(text_bottom > text_top && std::abs((text_top - banner.Pos.y) -
                                               (banner.Pos.y + banner.Size.y - text_bottom)) <= 2.f,
            "The editing lock notice has uneven visible padding");
}

void locked_relic_grid(float height) {
    auto view = fixture(true, true);
    auto &equipped = std::get<ern::EquippedRelics>(view.inspection);
    for (std::size_t i = 0; i < ern::relic_slot_count; ++i) {
        if (i == 2)
            continue;
        equipped.slots[i] = *equipped.slots[3];
        equipped.slots[i]->item_id = 0x80000000u | (i < 3 ? 102u : 2000000u);
        equipped.slots[i]->effects.rows[0].effect = 7000402;
        view.drafts[i] = ern::Draft{*equipped.slots[i], equipped.slots[i]->effects};
    }
    view.lock_reason = "Return to Roundtable Hold to make changes.";
    Frame frame(height, view);
    ern::ui::EditorState state;
    std::optional<ern::EditorAction> action;
    const auto draw = [&] {
        frame.begin();
        ern::ui::draw_editor(view, action, state, frame.appearance, &equipped,
                             ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
        Frame::end();
    };
    for (int i = 0; i < 3; ++i)
        draw();
    const auto &grid = child("/relic-grid_");
    require(!grid.ScrollbarX && !grid.ScrollbarY && grid.ScrollMax.x == 0 &&
                grid.ScrollMax.y == 0 && child("/editor-content_").ScrollMax.y == 0,
            "The locked grid requires scrolling");
    std::vector<ImRect> cards;
    const auto selected = ImGui::ColorConvertFloat4ToU32(ern::ui::rgb(32, 47, 72));
    const auto idle = ImGui::ColorConvertFloat4ToU32(ern::ui::rgb(9, 15, 26, .5f));
    const auto &vertices = grid.DrawList->VtxBuffer;
    for (int i = 0; i + 3 < vertices.Size; i += 4)
        if (vertices[i].col == selected || vertices[i].col == idle)
            cards.emplace_back(vertices[i].pos, vertices[i + 2].pos);
    require(cards.size() == ern::relic_slot_count,
            "The locked grid did not render all six equipped and empty slots");
    for (std::size_t i = 0; i < cards.size(); ++i) {
        const auto &card = cards[i];
        require(grid.ClipRect.Contains(card), "A locked relic card is clipped");
        require(std::abs(card.Min.y - cards[i < 3 ? 0 : 3].Min.y) <= 1.f &&
                    std::abs(card.Min.x - cards[i % 3].Min.x) <= 1.f,
                "Locked relic cards do not form three columns and two rows");
        if (i % 3 != 0)
            require(card.Min.x > cards[i - 1].Max.x, "Locked relic columns overlap");
    }
    require(cards[3].Min.y > cards[0].Max.y, "Locked relic rows overlap");
    require(child("##notification").Pos.y > cards.back().Max.y,
            "The editing lock banner covers the relic grid");
    const auto last = cards.back().GetCenter();
    ImGui::GetIO().AddMousePosEvent(last.x, last.y);
    draw();
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw();
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw();
    require(state.selected == 5 && !action,
            "The last grid slot cannot be inspected without emitting an edit action");
    view.lock_reason.clear();
    draw();
    draw();
    require(child("/relics_").ScrollbarX && child("/effect-browser_").Active,
            "Returning to Roundtable Hold did not restore the editable carousel and browser");
    require(child("/relics_").Scroll.x > 0,
            "Returning to Roundtable Hold left the selected Deep relic off screen");
}

void diagnostics_above_notifications(float height) {
    auto view = fixture(false, false);
    view.rule_diagnostics.requested_path = "fixture/regulation.bin";
    view.rule_diagnostics.resolved_path = "fixture/regulation.bin";
    Frame frame(height, view);
    std::optional<ern::EditorAction> action;
    bool opened = false;
    const auto draw = [&](ern::ui::Notice *message) {
        frame.begin();
        ImGui::SetCursorPosY(height - ImGui::GetFrameHeight() - frame.appearance.space(3));
        const auto footer = ImGui::GetCursorScreenPos();
        if (!opened) {
            ImGui::OpenPopup("Diagnostics");
            opened = true;
        }
        ern::ui::draw_diagnostics(view, action, frame.appearance,
                                  &std::get<ern::EquippedRelics>(view.inspection), 0,
                                  ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
        if (message)
            ern::ui::notification(*message, frame.appearance,
                                  {footer.x, footer.y, ImGui::GetWindowWidth(), 0});
        Frame::end();
    };
    // Exercise a banner first appearing after Diagnostics has already been opened.
    draw(nullptr);
    draw(nullptr);
    for (const auto kind : {ern::ui::NoticeKind::Info, ern::ui::NoticeKind::Success,
                            ern::ui::NoticeKind::Warning, ern::ui::NoticeKind::Error}) {
        auto message = ern::ui::feedback("Status", "A visible status banner", kind);
        for (int frame_index = 0; frame_index < 3; ++frame_index) {
            draw(&message);
            const auto &popup = child("##Popup_");
            const auto &banner = child("##notification");
            require(popup.Active && banner.Active && popup.Rect().Overlaps(banner.Rect()),
                    "The diagnostics layering check did not exercise an overlapping banner");
            int popup_index = -1, banner_index = -1;
            const auto &lists = ImGui::GetDrawData()->CmdLists;
            for (int i = 0; i < lists.Size; ++i) {
                if (lists[i] == popup.DrawList)
                    popup_index = i;
                if (lists[i] == banner.DrawList)
                    banner_index = i;
            }
            require(banner_index >= 0 && popup_index > banner_index,
                    "A status banner is drawn over Diagnostics");
        }
    }
}

int check_icon_alignment(const ern::GameAssets &assets) {
    const auto &atlas = *ImGui::GetIO().Fonts;
    const ImFontAtlasCustomRect *icons = nullptr;
    for (const auto &rect : atlas.CustomRects)
        if (rect.Width == assets.atlas.width && rect.Height == assets.atlas.height)
            icons = &rect;
    require(icons, "The test icon atlas was not uploaded to ImGui");
    const auto ivory = ImGui::ColorConvertFloat4ToU32(ern::ui::ivory);
    const auto amber = ImGui::ColorConvertFloat4ToU32(ern::ui::amber);
    const auto ice = ImGui::ColorConvertFloat4ToU32(ern::ui::ice);
    int checked = 0;
    for (const auto *window : ImGui::GetCurrentContext()->Windows) {
        if (!window->Active)
            continue;
        const auto &vertices = window->DrawList->VtxBuffer;
        for (unsigned int cell = 0; cell < 5; ++cell) {
            const float u =
                float(icons->X + cell * ern::IconAtlas::cell_size + ern::IconAtlas::gutter) /
                atlas.TexWidth;
            const float v = float(icons->Y + ern::IconAtlas::gutter) / atlas.TexHeight;
            const float u_end = u + float(ern::IconAtlas::icon_size) / atlas.TexWidth;
            for (int index = 0; index + 4 < vertices.Size; ++index) {
                if (std::abs(vertices[index].uv.x - u) > .00001f ||
                    std::abs(vertices[index].uv.y - v) > .00001f ||
                    std::abs(vertices[index + 1].uv.x - u_end) > .00001f)
                    continue;
                if (!window->InnerClipRect.Contains(vertices[index].pos) ||
                    !window->InnerClipRect.Contains(vertices[index + 2].pos))
                    continue;
                // Cell 0 deliberately has asymmetric transparent margins, like game artwork.
                const float visible_top = cell == 0 ? 24.f : 12.f;
                const float visible_bottom = cell == 0 ? 156.f : 148.f;
                const float icon_center =
                    vertices[index].pos.y + (vertices[index + 2].pos.y - vertices[index].pos.y) *
                                                (visible_top + visible_bottom) /
                                                (2 * ern::IconAtlas::icon_size);
                if (cell == 4) {
                    if (!std::strstr(window->Name, "/relics_"))
                        continue;
                    float top = FLT_MAX, bottom = -FLT_MAX;
                    const auto white = atlas.TexUvWhitePixel;
                    for (const auto &vertex : vertices)
                        if ((vertex.col == ivory ||
                             vertex.col == ImGui::ColorConvertFloat4ToU32(ern::ui::muted)) &&
                            vertex.pos.x > vertices[index + 1].pos.x &&
                            vertex.pos.x < vertices[index].pos.x + window->Size.x / 3 &&
                            vertex.pos.y > vertices[index].pos.y &&
                            vertex.pos.y < vertices[index + 2].pos.y &&
                            (vertex.uv.x != white.x || vertex.uv.y != white.y)) {
                            top = std::min(top, vertex.pos.y);
                            bottom = std::max(bottom, vertex.pos.y);
                        }
                    require(bottom > top && std::abs(icon_center - (top + bottom) / 2) <= 1.f,
                            "The relic portrait and its title block are misaligned");
                    continue;
                }
                const auto color = vertices[index + 4].col;
                require(color == ivory || color == amber || color == ice,
                        "An effect icon was not followed by its label");
                float top = FLT_MAX, bottom = -FLT_MAX;
                for (int glyph = index + 4; glyph < vertices.Size && vertices[glyph].col == color;
                     ++glyph) {
                    top = std::min(top, vertices[glyph].pos.y);
                    bottom = std::max(bottom, vertices[glyph].pos.y);
                }
                const float offset = icon_center - (top + bottom) / 2;
                if (std::abs(offset) > 1.f)
                    throw std::runtime_error(std::string("Icon/label center differs by ") +
                                             std::to_string(offset) + " px in " + window->Name);
                ++checked;
            }
        }
    }
    return checked;
}

void icon_alignment(float height, bool deep, bool wrapped) {
    auto view = fixture(deep, wrapped);
    Frame frame(height, view);
    ern::ui::EditorState state;
    state.selected = deep ? 3 : 0;
    draw_editor(frame, view, state);
    require(check_icon_alignment(view.assets()) >= 6, "Too few editor icon rows were checked");
    if (deep) {
        const auto &fields = child("/editor-content_");
        bool found = false;
        for (const auto &vertex : fields.DrawList->VtxBuffer)
            if (vertex.col == IM_COL32_WHITE &&
                vertex.pos.x > fields.Pos.x + fields.Size.x * .65f) {
                ImGui::GetIO().AddMousePosEvent(vertex.pos.x + 2, vertex.pos.y + 2);
                found = true;
                break;
            }
        require(found, "The paired curse column has no icon to select");
        ImGui::GetIO().AddMouseButtonEvent(0, true);
        draw_editor(frame, view, state);
        ImGui::GetIO().AddMouseButtonEvent(0, false);
        draw_editor(frame, view, state);
        require(state.selected_effect == ern::EffectPosition{2, ern::EffectField::Curse},
                "Selecting a paired curse opened the wrong field");
    }
    view.lock_reason = "Return to Roundtable Hold to edit relics.";
    draw_editor(frame, view, state);
    require(check_icon_alignment(view.assets()) >= 3, "Too few read-only icon rows were checked");
    view.lock_reason.clear();
    ern::Preset preset;
    preset.slots[state.selected] = view.drafts[state.selected]->effects;
    view.presets = std::make_shared<const std::vector<ern::SavedPreset>>(
        std::vector<ern::SavedPreset>{{"Fixture preset", preset}});
    ern::ui::PresetBrowser browser;
    browser.selected = "Fixture preset";
    std::optional<ern::EditorAction> action;
    for (int i = 0; i < 3; ++i) {
        frame.begin();
        ern::ui::draw_presets(view, action, browser, frame.appearance,
                              &std::get<ern::EquippedRelics>(view.inspection),
                              ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
        Frame::end();
    }
    require(check_icon_alignment(view.assets()) >= 3, "Too few preset icon rows were checked");
}

void validation_notice(float height) {
    auto view = fixture(true, false);
    view.drafts[3]->effects.rows[2].curse = UINT32_MAX;
    const auto &equipped = std::get<ern::EquippedRelics>(view.inspection);
    view.review = ern::review_drafts(view.drafts, &equipped, view.validator(), view.context);
    require(view.review.blocked.has_value(), "The invalid Deep pair was not rejected");
    const auto reason = ern::validation_reason(
        view.validate(view.drafts[3]->original.item_id, view.drafts[3]->effects));
    Frame frame(height, view);
    ern::ui::EditorState state;
    state.selected = 3;
    draw_editor(frame, view, state);
    frame.begin();
    ImGui::LogToBuffer();
    std::optional<ern::EditorAction> action;
    ern::ui::draw_editor(view, action, state, frame.appearance, &equipped,
                         ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
    const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    Frame::end();
    const auto first = text.find(reason);
    require(first != std::string::npos &&
                text.find(reason, first + reason.size()) == std::string::npos,
            "The roll validation warning is missing or duplicated");
    require(text.find("Dismiss") == std::string::npos &&
                text.find("Review slot") == std::string::npos,
            "The current slot's live validation error has redundant controls");
    const auto &banner = child("##notification");
    ImGui::GetIO().AddMousePosEvent(banner.Pos.x + banner.Size.x - frame.appearance.px(24),
                                    banner.Pos.y + banner.Size.y / 2);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw_editor(frame, view, state);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw_editor(frame, view, state);
    require(banner.Active, "A validation error was dismissed before its cause was resolved");
    view.drafts[3]->effects.rows[2].curse = 900002;
    view.review = ern::review_drafts(view.drafts, &equipped, view.validator(), view.context);
    draw_editor(frame, view, state);
    require(!view.review.blocked && !banner.Active,
            "The validation error remained after its cause was resolved");
}

void full_description_tooltip(float height) {
    auto view = fixture(false, true);
    view.drafts[0]->effects.rows[0].effect = 7000402;
    Frame frame(height, view);
    ern::ui::EditorState state;
    draw_editor(frame, view, state);
    hover_tag(child("/editor-content_"));
    draw_editor(frame, view, state);
    frame.begin();
    ImGui::LogToBuffer();
    std::optional<ern::EditorAction> action;
    ern::ui::draw_editor(view, action, state, frame.appearance,
                         &std::get<ern::EquippedRelics>(view.inspection),
                         ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
    const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    Frame::end();
    require(text.find(view.presentation().describe(7000402)) != std::string::npos &&
                text.find("Strength +1") != std::string::npos,
            "The compact field lost its full description or original value");
}

void card_changes_tooltip(float height) {
    auto view = fixture(false, true);
    view.drafts[0]->effects.rows[0].effect = 7000402;
    view.drafts[0]->effects.rows[1].effect = 7000602;
    Frame frame(height, view);
    ern::ui::EditorState state;
    draw_editor(frame, view, state);
    const auto &carousel = child("/relics_");
    const auto amber = ImGui::ColorConvertFloat4ToU32(ern::ui::amber);
    bool found = false;
    for (const auto &v : carousel.DrawList->VtxBuffer)
        if (v.col == amber && v.pos.y < carousel.Pos.y + frame.appearance.px(50)) {
            ImGui::GetIO().AddMousePosEvent(v.pos.x + 2, v.pos.y + 2);
            found = true;
            break;
        }
    require(found, "The card has no change-count badge to hover");
    draw_editor(frame, view, state);
    frame.begin();
    ImGui::LogToBuffer();
    std::optional<ern::EditorAction> action;
    ern::ui::draw_editor(view, action, state, frame.appearance,
                         &std::get<ern::EquippedRelics>(view.inspection),
                         ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
    const std::string text = ImGui::GetCurrentContext()->LogBuffer.c_str();
    ImGui::LogFinish();
    Frame::end();
    require(text.find("Relic changes") != std::string::npos &&
                text.find("Effect 1") != std::string::npos &&
                text.find("Strength +1") != std::string::npos &&
                text.find(view.presentation().describe(7000402)) != std::string::npos &&
                text.find(view.presentation().describe(7000602)) != std::string::npos,
            "The card's change-count tooltip lost part of the diff");
    require(text.find("Before:") == std::string::npos && text.find("After:") == std::string::npos,
            "The compact change summary retained code-style diff labels");
    const auto &tooltip = child("##Tooltip_");
    require(tooltip.WindowPadding.x > 0 && tooltip.WindowPadding.y > 0 &&
                tooltip.FontWindowScale == 1.f,
            "The card tooltip inherited the carousel's unpadded layout or smaller font");
}

void notification_layout(float height) {
    auto view = fixture(false, false);
    auto equipped = std::get<ern::EquippedRelics>(view.inspection);
    equipped.slots[1] = equipped.slots[0];
    view.inspection = equipped;
    view.drafts[1] = view.drafts[0];
    Frame frame(height, view);
    ern::ui::EditorState state;
    const auto plain = draw_editor(frame, view, state);
    const float content_height = child("/editor-content_").Size.y;
    for (auto kind :
         {ern::ui::NoticeKind::Success, ern::ui::NoticeKind::Warning, ern::ui::NoticeKind::Error}) {
        state.status = ern::ui::feedback("Fixture notification",
                                         "A result that leaves the editor in place.", kind);
        state.status.action = ern::ui::NoticeAction::ReviewSlot;
        state.status.slot = 1;
        const auto shown = draw_editor(frame, view, state);
        const auto &banner = child("##notification");
        const auto &root = child("render-check");
        require(shown.browser_y == plain.browser_y &&
                    shown.carousel_height == plain.carousel_height,
                "A notification changed the editor geometry");
        const auto &content = child("/editor-content_");
        require(content.Size.y == content_height &&
                    content.Pos.y + content.Size.y <= banner.Pos.y - frame.appearance.space(2) + 1,
                "The notification did not use its reserved space below the content");
        require(banner.Pos.x == root.WorkRect.Min.x &&
                    banner.Pos.x + banner.Size.x == root.WorkRect.Max.x,
                "The notification does not share the editor's content edges");
        int banner_layer = -1, results_layer = -1;
        const auto &lists = ImGui::GetDrawData()->CmdLists;
        for (int i = 0; i < lists.Size; ++i) {
            if (lists[i] == banner.DrawList)
                banner_layer = i;
            if (lists[i] == child("/effect-results_").DrawList)
                results_layer = i;
        }
        require(banner_layer > results_layer && results_layer >= 0,
                "The notification was covered by the editor");
    }
    const auto &banner = child("##notification");
    ImGui::GetIO().AddMousePosEvent(banner.Pos.x + banner.Size.x - frame.appearance.px(100),
                                    banner.Pos.y + banner.Size.y / 2);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw_editor(frame, view, state);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw_editor(frame, view, state);
    require(state.selected == 1, "The banner's Review slot action did not select its target");
    ImGui::GetIO().AddMousePosEvent(banner.Pos.x + banner.Size.x - frame.appearance.px(24),
                                    banner.Pos.y + banner.Size.y / 2);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw_editor(frame, view, state);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw_editor(frame, view, state);
    require(state.status.text.empty(), "The notification did not dismiss");
}

void sequential_choices(float height) {
    auto view = fixture(true, false);
    Frame frame(height, view);
    ern::ui::EditorState state;
    state.selected = 3;
    state.selected_effect = {2, ern::EffectField::Effect};
    draw_editor(frame, view, state);
    const auto icons = [&] {
        const auto &vertices = child("/effect-results_").DrawList->VtxBuffer;
        return std::count_if(vertices.begin(), vertices.end(),
                             [](const auto &v) { return v.col == IM_COL32_WHITE; }) /
               4;
    };
    require(icons() == 3, "Later rows show effects excluded by an earlier row");
    state.selected_effect = {0, ern::EffectField::Effect};
    draw_editor(frame, view, state);
    require(icons() == 7, "Later rows hid choices while revising the first effect");
    state.selected_effect = {0, ern::EffectField::Curse};
    draw_editor(frame, view, state);
    require(icons() == 0, "Curses incompatible with the current effect are still visible");
    state.selected_effect = {2, ern::EffectField::Curse};
    draw_editor(frame, view, state);
    require(icons() == 3, "Compatible curses were hidden from the paired row");
    view.drafts[3]->effects.rows[0].effect = 7001801;
    state.selected_effect = {1, ern::EffectField::Effect};
    draw_editor(frame, view, state);
    require(icons() == 6, "An earlier effect without its required curse hid later choices");
    state.invalid = true;
    state.selected_effect = {1, ern::EffectField::Effect};
    draw_editor(frame, view, state);
    require(icons() == static_cast<std::ptrdiff_t>(view.rules->effects().size()),
            "Invalid mode did not enable every effect");
}

void tooltip_hover_ownership(float height) {
    auto view = fixture(false, false);
    view.drafts[0]->effects.rows[0].effect = 7000402;
    Frame frame(height, view);
    ern::ui::EditorState state;
    draw_editor(frame, view, state);
    ImVec2 row_point{};
    for (const auto &v : child("/editor-content_").DrawList->VtxBuffer)
        if (v.col == IM_COL32_WHITE) {
            row_point = {v.pos.x + 2, v.pos.y + 2};
            ImGui::GetIO().AddMousePosEvent(row_point.x, row_point.y);
            break;
        }
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw_editor(frame, view, state);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw_editor(frame, view, state);
    require(!tooltip_visible(), "The changed row opened a diff without hovering its tag");
    hover_tag(child("/editor-content_"));
    draw_editor(frame, view, state);
    require(tooltip_visible(), "Hovering the Changed tag did not show its diff");
    ImGui::GetIO().AddMousePosEvent(row_point.x, row_point.y);
    draw_editor(frame, view, state);
    require(!tooltip_visible(), "The diff remained visible after leaving the tag within its row");
    ImGui::GetIO().AddMousePosEvent(0, height / 2);
    draw_editor(frame, view, state);
    require(!tooltip_visible(),
            "The focused field followed the mouse outside the editor with a tooltip");
}

void normal_deep_header(float height) {
    auto view = fixture(true, false);
    auto equipped = std::get<ern::EquippedRelics>(view.inspection);
    auto normal = *equipped.slots[3];
    normal.item_id = 0x80000066;
    normal.effects.rows[2].curse = UINT32_MAX;
    equipped.slots[0] = normal;
    view.inspection = equipped;
    view.drafts[0] = ern::Draft{normal, normal.effects};
    Frame frame(height, view);
    ern::ui::EditorState state;
    const auto normal_geometry = draw_editor(frame, view, state);
    state.selected = 3;
    const auto deep_geometry = draw_editor(frame, view, state);
    require(normal_geometry.browser_y == deep_geometry.browser_y,
            "The Effects heading moved the browser when switching Normal and Deep relics");
}

void centered_empty_state(float height) {
    auto view = fixture(false, false);
    Frame frame(height, view);
    frame.begin();
    const auto position = ImGui::GetCursorScreenPos(), available = ImGui::GetContentRegionAvail();
    ern::ui::empty_state(
        frame.appearance, "No matching effects",
        "Try another search or category to see more of the available relic effects.");
    Frame::end();
    const auto &vertices = child("render-check").DrawList->VtxBuffer;
    const auto ivory = ImGui::ColorConvertFloat4ToU32(ern::ui::ivory);
    const auto muted = ImGui::ColorConvertFloat4ToU32(ern::ui::muted);
    float top = FLT_MAX, bottom = -FLT_MAX;
    struct Line {
        float top, left, right;
    };
    std::vector<Line> lines;
    for (int i = 0; i + 3 < vertices.Size; i += 4) {
        const auto &v = vertices[i];
        if (v.col != ivory && v.col != muted)
            continue;
        top = std::min(top, v.pos.y);
        bottom = std::max(bottom, vertices[i + 2].pos.y);
        if (v.col != muted)
            continue;
        if (lines.empty() || v.pos.y > lines.back().top + 18 * frame.appearance.scale * .8f)
            lines.push_back({v.pos.y, v.pos.x, vertices[i + 2].pos.x});
        else {
            auto &line = lines.back();
            line.top = std::min(line.top, v.pos.y);
            line.left = std::min(line.left, v.pos.x);
            line.right = std::max(line.right, vertices[i + 2].pos.x);
        }
    }
    require(std::abs((top + bottom) / 2 - position.y - available.y / 2) <= 1.5f,
            "The empty-state text block is not vertically centered");
    require(lines.size() >= 2, "The empty-state alignment check did not exercise wrapped text");
    for (const auto &line : lines)
        require(std::abs((line.left + line.right) / 2 - position.x - available.x / 2) <= 1.5f,
                "A wrapped empty-state line is left aligned inside a centered block");
}

void badge_alignment(float height) {
    auto view = fixture(false, false);
    Frame frame(height, view);
    frame.begin();
    ImGui::PushFont(frame.appearance.relic_font);
    const auto position = ImGui::GetCursorScreenPos();
    const auto size = frame.appearance.badge_size("3 changes");
    frame.appearance.badge("3 changes", position, ern::ui::amber);
    ImGui::PopFont();
    Frame::end();
    float top = FLT_MAX, bottom = -FLT_MAX;
    const auto amber = ImGui::ColorConvertFloat4ToU32(ern::ui::amber);
    for (const auto &v : child("render-check").DrawList->VtxBuffer)
        if (v.col == amber) {
            top = std::min(top, v.pos.y);
            bottom = std::max(bottom, v.pos.y);
        }
    const float center_offset = (top + bottom) / 2 - position.y - size.y / 2;
    // The optical offset is rounded once, then the glyph origin is rounded to a pixel.
    require(bottom > top && (top + bottom) / 2 >= position.y + size.y / 2 - .5f &&
                std::abs(center_offset) <= frame.appearance.px(1) + .5f,
            "The shared badge label sits too high or has excessive optical offset");
}

void native_text_pixels(float height) {
    auto view = fixture(false, false);
    Frame frame(height, view);
    frame.begin();
    require(ImGui::GetFontSize() == ImGui::GetIO().FontDefault->FontSize,
            "The UI magnified its baked body font");
    auto *draw = ImGui::GetWindowDrawList();
    const int first = draw->VtxBuffer.Size;
    auto position = ImGui::GetCursorScreenPos();
    for (auto *font : {ImGui::GetIO().FontDefault, frame.appearance.tag_font,
                       frame.appearance.title_font, frame.appearance.name_font}) {
        ImGui::PushFont(font);
        draw->AddText({std::round(position.x), std::round(position.y)}, IM_COL32_WHITE,
                      "Relic changes +1");
        position.y += font->FontSize + frame.appearance.space(2);
        ImGui::PopFont();
    }
    const int end = draw->VtxBuffer.Size;
    Frame::end();
    const auto &atlas = *ImGui::GetIO().Fonts;
    require(end > first, "The font pixel check did not render any glyphs");
    for (int i = first; i + 3 < end; i += 4) {
        const auto &top = draw->VtxBuffer[i];
        const auto &bottom = draw->VtxBuffer[i + 2];
        require(
            std::abs(bottom.pos.x - top.pos.x - (bottom.uv.x - top.uv.x) * atlas.TexWidth) < .01f &&
                std::abs(bottom.pos.y - top.pos.y - (bottom.uv.y - top.uv.y) * atlas.TexHeight) <
                    .01f,
            "Font bitmap pixels were stretched or shrunk on screen");
        for (int corner = 0; corner < 4; ++corner) {
            const auto p = draw->VtxBuffer[i + corner].pos;
            require(std::abs(p.x - std::round(p.x)) < .01f &&
                        std::abs(p.y - std::round(p.y)) < .01f,
                    "A glyph was drawn between screen pixels");
        }
    }
}

void scrollbar_drag(float height) {
    auto view = fixture(false, false);
    Frame frame(height, view);
    ern::ui::EditorState state;
    draw_editor(frame, view, state);
    auto &carousel = const_cast<ImGuiWindow &>(child("/relics_"));
    const auto bar = ImGui::GetWindowScrollbarRect(&carousel, ImGuiAxis_X);
    require(bar.GetHeight() >= frame.appearance.space(4),
            "The carousel scrollbar target is too thin");
    const float before = carousel.Scroll.x;
    const ImVec2 grab{bar.Min.x + bar.GetWidth() * .25f, bar.GetCenter().y};
    ImGui::GetIO().AddMousePosEvent(grab.x, grab.y);
    ImGui::GetIO().AddMouseButtonEvent(0, true);
    draw_editor(frame, view, state);
    ImGui::GetIO().AddMousePosEvent(grab.x + bar.GetWidth() * .2f, grab.y);
    draw_editor(frame, view, state);
    ImGui::GetIO().AddMouseButtonEvent(0, false);
    draw_editor(frame, view, state);
    require(carousel.Scroll.x > before + frame.appearance.space(4),
            "Dragging the carousel scrollbar did not move the relic cards");
}

void pair_grouping(float height) {
    auto view = fixture(true, false);
    Frame frame(height, view);
    frame.begin();
    ImGui::PushFont(frame.appearance.relic_font);
    for (std::size_t i = 0; i < 2; ++i) {
        frame.appearance.effect_pair(view.presentation(), view.drafts[3]->effects.rows[i].effect,
                                     UINT32_MAX, true, false);
        ern::ui::vertical_gap(frame.appearance.space(2));
    }
    ImGui::PopFont();
    Frame::end();
    struct Line {
        ImU32 color;
        float top, bottom;
    };
    std::vector<Line> lines;
    const auto effect_color = ImGui::ColorConvertFloat4ToU32(ern::ui::ivory);
    const auto curse_color = ImGui::ColorConvertFloat4ToU32(ern::ui::muted);
    for (const auto &v : child("render-check").DrawList->VtxBuffer)
        if (v.col == effect_color || v.col == curse_color) {
            if (lines.empty() || lines.back().color != v.col)
                lines.push_back({v.col, v.pos.y, v.pos.y});
            lines.back().top = std::min(lines.back().top, v.pos.y);
            lines.back().bottom = std::max(lines.back().bottom, v.pos.y);
        }
    require(lines.size() == 4, "The pair grouping check did not render two complete pairs");
    const float inside = lines[1].top - lines[0].bottom;
    const float between = lines[2].top - lines[1].bottom;
    require(inside >= 0 && inside <= frame.appearance.space(1) + 1 && inside * 2 < between,
            "A curse is not visibly closer to its own effect than the following effect");
}

void popup_scale(float height) {
    auto view = fixture(false, false);
    Frame frame(height, view);
    frame.begin();
    ImGui::OpenPopup("Diagnostics");
    std::optional<ern::EditorAction> action;
    ern::ui::draw_diagnostics(view, action, frame.appearance,
                              &std::get<ern::EquippedRelics>(view.inspection), 0,
                              ern::editor_bounds(ImGui::GetIO().DisplaySize.x, height));
    Frame::end();
    require(std::abs(child("##Popup_").CalcFontSize() - ImGui::GetIO().FontDefault->FontSize) <
                .01f,
            "The diagnostics popup applied display scaling twice");
}

void tag_text_separation(float height, bool deep) {
    auto view = fixture(deep, true);
    const std::size_t slot = deep ? 3 : 0;
    view.drafts[slot]->effects.rows[0].effect = 7000402;
    if (deep)
        view.drafts[slot]->effects.rows[0].curse = 900001;
    Frame frame(height, view);
    ern::ui::EditorState state;
    state.selected = slot;
    draw_editor(frame, view, state);
    const auto fill = ImGui::ColorConvertFloat4ToU32(
        {ern::ui::amber.x, ern::ui::amber.y, ern::ui::amber.z, .13f});
    std::vector<ImRect> tags;
    bool in_tag = false;
    const auto &vertices = child("/editor-content_").DrawList->VtxBuffer;
    for (const auto &v : vertices) {
        if (v.col == fill) {
            if (!in_tag)
                tags.emplace_back(v.pos, v.pos);
            tags.back().Add(v.pos);
            in_tag = true;
        } else if (in_tag && v.col == (fill & ~IM_COL32_A_MASK))
            tags.back().Add(v.pos);
        else
            in_tag = false;
    }
    require(tags.size() == (deep ? 2u : 1u), "Expected changed tags were not rendered");
    const auto ivory = ImGui::ColorConvertFloat4ToU32(ern::ui::ivory);
    const auto ice = ImGui::ColorConvertFloat4ToU32(ern::ui::ice);
    for (const auto &v : vertices)
        if (v.col == ivory || v.col == ice)
            for (const auto &tag : tags)
                require(!tag.Contains(v.pos), "An effect or curse label extends behind its tag");
}

void change_alignment(float height) {
    auto view = fixture(false, false);
    view.drafts[0]->effects.rows[0].effect = 7000402;
    view.drafts[0]->effects.rows[1].effect = 7000602;
    Frame frame(height, view);
    ern::ui::EditorState state;
    draw_editor(frame, view, state);
    const auto &fields = child("/editor-content_");
    const auto fill = ImGui::ColorConvertFloat4ToU32(
        {ern::ui::amber.x, ern::ui::amber.y, ern::ui::amber.z, .13f});
    std::vector<ImVec2> points;
    for (const auto &v : fields.DrawList->VtxBuffer)
        if (v.col == fill)
            points.push_back(v.pos);
    std::sort(points.begin(), points.end(), [](auto a, auto b) { return a.y < b.y; });
    struct Tag {
        float top, right;
    };
    std::vector<Tag> tags;
    const float tag_height = frame.appearance.space(6);
    for (const auto &p : points)
        if (tags.empty() || p.y > tags.back().top + tag_height + 1)
            tags.push_back({p.y, p.x});
        else
            tags.back().right = std::max(tags.back().right, p.x);
    require(tags.size() == 2 && std::abs(tags[0].right - tags[1].right) <= .1f &&
                std::abs(tags[0].right + frame.appearance.space(2) - fields.WorkRect.Max.x) <= 1,
            "Changed tags do not share the right edge across different text lengths");
    hover_tag(fields);
    draw_editor(frame, view, state);
    const auto &vertices = child("##Tooltip_").DrawList->VtxBuffer;
    const auto text_x = [&](ImU32 color) {
        for (int i = 0; i + 4 < vertices.Size; ++i)
            if (vertices[i].col == color)
                return vertices[i + 4].pos.x;
        throw std::runtime_error("The change tooltip is missing a colored value");
    };
    require(text_x(ImGui::ColorConvertFloat4ToU32(ern::ui::added)) ==
                text_x(ImGui::ColorConvertFloat4ToU32(ern::ui::removed)),
            "Plus and minus signs shifted the effect text into different columns");
}
} // namespace

int main() {
    return test::run(
        "rendered icon alignment, compact layout, tooltips and validation across five scales", [] {
            for (const float height : {720.f, 900.f, 1080.f, 1440.f, 2160.f}) {
                for (const bool deep : {false, true})
                    for (const bool wrapped : {false, true}) {
                        icon_alignment(height, deep, wrapped);
                        comparison_space(height, deep, wrapped);
                    }
                button_alignment(height);
                checkbox_alignment(height);
                preset_padding(height);
                preset_replacement(height);
                glyph_bounds(height);
                read_only_and_pending(height);
                locked_relic_grid(height);
                diagnostics_above_notifications(height);
                validation_notice(height);
                full_description_tooltip(height);
                card_changes_tooltip(height);
                notification_layout(height);
                sequential_choices(height);
                tooltip_hover_ownership(height);
                normal_deep_header(height);
                centered_empty_state(height);
                badge_alignment(height);
                change_alignment(height);
                tag_text_separation(height, false);
                tag_text_separation(height, true);
                popup_scale(height);
                pair_grouping(height);
                native_text_pixels(height);
                scrollbar_drag(height);
            }
        });
}
