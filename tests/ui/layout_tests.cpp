#include "support/check.hpp"
#include "support/synthetic_rules.hpp"
#include "support/synthetic_assets.hpp"
#include "data/catalog_presentation.hpp"
#include "ui/effect_groups.hpp"
#include "ui/ui_layout.hpp"
#include "relics/relic_validator.hpp"
#include "ui/edit_feedback.hpp"

#include <cmath>

using test::require;

namespace {
void check_bounds(ern::UiRect editor, ern::UiRect field, float desired_height, float padding) {
    const auto popup = ern::effect_popup_bounds(editor, field, desired_height, padding);
    constexpr float tolerance = .01f;
    require(popup.width > 0 && popup.height > 0, "Popup has no usable area");
    require(popup.x >= editor.x - tolerance && popup.y >= editor.y - tolerance,
            "Popup escaped the top or left editor edge");
    require(popup.x + popup.width <= editor.x + editor.width + tolerance,
            "Popup escaped the right editor edge");
    require(popup.y + popup.height <= editor.y + editor.height + tolerance,
            "Popup escaped the bottom editor edge");
    require(popup.width < editor.width && popup.height < editor.height,
            "Popup is not smaller than the editor");
}

void effect_groups() {
    using enum ern::EffectGroup;
    require(ern::effect_group("Arcane +3") == Attributes, "Attribute effect was not grouped");
    require(ern::effect_group("Improved Great Hammer Attack Power") == Attack,
            "Weapon attack was not grouped");
    require(ern::effect_group("FP Restoration upon Great Hammer Attacks") == Recovery,
            "Recovery was confused with attack");
    require(ern::effect_group("Dormant Power Helps Discover Great Hammers") == Discovery,
            "Discovery was not grouped");
    require(ern::effect_group("Improved Blood Loss Resistance") == Defense,
            "Resistance was confused with attack");
    require(ern::effect_group(
                "Changes compatible armament's incantation to Beast Claw at start of expedition") ==
                Skills,
            "Spell replacement was not grouped");
    require(ern::effect_group("[Wylder] Art gauge greatly filled when ability is activated") ==
                Character,
            "Character prefix was not grouped");
    require(ern::effect_group("New unclassified effect") == Other,
            "Unclassified effects disappeared");
    require(ern::effect_group("fp restoration upon GREAT HAMMER attacks") == Recovery,
            "Grouping is case sensitive");
}

void catalog_presentation() {
    auto rules = synthetic_rules();
    rules.effects.at(7000402).text_id = 55;
    rules.effects.at(7000402).icon = 77;
    ern::RelicValidator validator{std::move(rules)};
    auto assets = synthetic_assets();
    const ern::CatalogPresentation presentation{validator, assets};
    const auto requested = ern::requested_icons(validator);
    require(std::find(requested.begin(), requested.end(),
                      ern::IconKey{ern::IconKind::Item, 19200}) != requested.end() &&
                std::find(requested.begin(), requested.end(),
                          ern::IconKey{ern::IconKind::Item, 19201}) != requested.end(),
            "Empty normal and Deep artwork was not requested from the local game assets");
    require(!presentation.empty_relic_cell(false) && !presentation.empty_relic_cell(true),
            "Missing Flatstone artwork referenced an unrelated atlas cell");
    assets.cells[{ern::IconKind::Item, 19200}] = 0;
    assets.cells[{ern::IconKind::Item, 19201}] = 1;
    require(presentation.empty_relic_cell(false) == 0u && presentation.empty_relic_cell(true) == 1u,
            "Empty slots did not distinguish normal and Deep Flatstone artwork");
    std::size_t classified = 0;
    for (const auto *effect : validator.candidates(0x800F4AE4)) {
        const auto group = ern::effect_group(presentation.describe(effect->id));
        require(std::find(ern::effect_groups.begin(), ern::effect_groups.end(), group) !=
                    ern::effect_groups.end(),
                "Candidate has no visible group");
        ++classified;
    }
    require(classified >= 7, "Grouping did not exercise the real candidate list");

    assets.effect_names[55] = "Synthetic message selected by text ID";
    assets.cells[{ern::IconKind::Status, 77}] = 0;
    const auto &catalog = presentation;
    require(catalog.name(validator.items().find(0x800F4AE4)) == "Fixture random relic" &&
                catalog.describe(11000) == "[Fixture] Character bonus" &&
                catalog.describe(UINT32_MAX) == "Empty effect slot" &&
                catalog.describe(0xFEFEFEFE).find("Unknown effect") != std::string::npos,
            "Display data or numeric fallbacks changed the catalogue labels");
    require(catalog.describe(7000402) == "Synthetic message selected by text ID",
            "Effect text was selected by effect ID instead of its active text ID");
    require(catalog.effect_cell(7000402) == 0u, "Atlas cell zero was confused with a missing icon");
    require(!catalog.effect_cell(11000), "A missing icon referenced an unrelated cell");
    require(std::holds_alternative<ern::Obtainable>(validator.validate(
                0x800F4AE4, {{7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}},
                ern::DlcState::Unknown)),
            "Unavailable presentation metadata disabled valid edits");
}

void popup_geometry() {
    for (const float height : {360.f, 480.f, 640.f, 720.f, 800.f, 1080.f, 1440.f, 2160.f}) {
        const float scale = ern::editor_scale(height);
        for (const float screen_width : {640.f, 960.f, 1200.f, 1920.f, 2560.f, 3840.f}) {
            const auto editor = ern::editor_bounds(screen_width, height);
            require(editor.x == screen_width / 2 && editor.width == screen_width / 2 &&
                        editor.y == 0 && editor.height == height,
                    "Editor does not fill the right half of the viewport");
            for (const float fraction : {0.f, .2f, .5f, .8f, 1.f}) {
                const ern::UiRect field{editor.x + 20 * scale,
                                        editor.y + fraction * (editor.height - 40 * scale),
                                        editor.width - 40 * scale, 36 * scale};
                check_bounds(editor, field, 340 * scale, 12 * scale);
            }
        }
    }
    const ern::UiRect editor{100, 100, 800, 700};
    const auto below = ern::effect_popup_bounds(editor, {120, 200, 760, 40}, 300, 12);
    const auto above = ern::effect_popup_bounds(editor, {120, 700, 760, 40}, 300, 12);
    require(std::abs(below.y - 240) < .01f, "Picker did not open below a field with space");
    require(std::abs(above.y - 400) < .01f, "Picker did not open above a field near the bottom");
    check_bounds({0, 0, 120, 90}, {-20, 70, 300, 40}, 400, 12);
}

void recovery_messages() {
    require(ern::ui::failure_message(
                ern::WriteFailure{ern::WriteStage::Write, ern::Restoration::Verified})
                        .find("were restored") != std::string::npos &&
                ern::ui::failure_message(
                    ern::WriteFailure{ern::WriteStage::Write, ern::Restoration::Unconfirmed})
                        .find("Check the relic") != std::string::npos,
            "Recovery messages do not distinguish verified and uncertain restoration");
}

} // namespace

int main() {
    return test::run(
        "synthetic candidate grouping, text-ID and icon-ID mappings, optional metadata, and popup containment across 240 screen/anchor combinations",
        [] {
            effect_groups();
            catalog_presentation();
            popup_geometry();
            recovery_messages();
        });
}
