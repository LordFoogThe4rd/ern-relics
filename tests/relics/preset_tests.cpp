#include "support/check.hpp"
#include "support/memory_fixture.hpp"
#include "support/synthetic_rules.hpp"
#include "relics/preset.hpp"

using test::Fixture;
using test::require;

int main() {
    return test::run("preset format, staging and names", [] {
        ern::RelicValidator validator{synthetic_rules()};
        const ern::RuleContext context = ern::DlcState::Unknown;
        const ern::Effects original{
            {7000401, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        const ern::Effects edited{
            {7000400, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        const ern::Effects boss_effects{
            {10000, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}};
        Fixture writable;
        writable.put(Fixture::base + 0x300C, std::uint32_t(0x80002710u));
        auto live = std::get<ern::RelicSnapshot>(
                        ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                        .relics;
        live = std::get<ern::RelicSnapshot>(
                   ern::read_equipped(writable.reader(), writable.roots, 0x2F4))
                   .relics;
        const ern::Preset lock_test{{original, edited, original}};
        const auto before_staging = writable.bytes;
        const auto staged = ern::stage_preset(lock_test, live, validator, context);
        require(!staged.drafts.slots[0] && staged.drafts.slots[1] && staged.drafts.slots[2],
                "Preset staged effects for a locked destination");
        require(writable.bytes == before_staging,
                "Staging a preset wrote memory or created an item");
        const auto captured = ern::capture_preset(live, validator, context);
        require(!captured.slots[0] && captured.slots[1] && captured.slots[2],
                "Preset captured an owned unique relic");
        require(ern::parse_preset(ern::serialize_preset(captured)) == captured,
                "Omitted preset slots did not round-trip");
        ern::Preset invalid_templates{{boss_effects, original, std::nullopt}};
        Fixture template_destination;
        const auto template_live =
            std::get<ern::RelicSnapshot>(ern::read_equipped(template_destination.reader(),
                                                            template_destination.roots, 0x2F4))
                .relics;
        const auto rejected_template =
            ern::stage_preset(invalid_templates, template_live, validator, context);
        require(!rejected_template.drafts.slots[0],
                "Boss-only effects were staged on a normal relic");
        const ern::Preset forced_templates{{boss_effects, boss_effects, std::nullopt}};
        const auto forced_stage =
            ern::stage_preset(forced_templates, live, validator, context, true);
        require(forced_stage.drafts.slots[1] && *forced_stage.drafts.slots[1] == boss_effects &&
                    !forced_stage.drafts.slots[0],
                "Invalid preset mode dropped an invalid slot or staged a locked relic");
        auto forced_live = live;
        forced_live.slots[1]->effects = boss_effects;
        const auto forced_capture = ern::capture_preset(forced_live, validator, context, true);
        require(forced_capture.slots[1] && *forced_capture.slots[1] == boss_effects &&
                    !forced_capture.slots[0],
                "Invalid preset capture kept a locked relic or dropped invalid effects");
        const auto unknown_stage = ern::stage_preset(lock_test, live, validator,
                                                     ern::UnknownRules{"Mismatched regulation"});
        require(!unknown_stage.drafts.slots[0] && !unknown_stage.drafts.slots[1] &&
                    !unknown_stage.drafts.slots[2],
                "Preset ignored unavailable rules");
        const ern::Preset preset{{original, edited, original}};
        const auto serialized = ern::serialize_preset(preset);
        require(ern::parse_preset(serialized) == preset, "Preset did not round-trip");
        require(!ern::parse_preset(serialized + "junk"), "Trailing preset data accepted");
        auto invalid_preset = serialized;
        invalid_preset.replace(invalid_preset.find("7000401"), 7, "4294967296");
        require(!ern::parse_preset(invalid_preset), "Overflowing effect ID accepted");
        const ern::Preset format_fixture{{ern::Effects{{1, 4}, {2, 5}, {3, 6}}}};
        constexpr std::string_view format_text =
            "{\"effects\":[[1,2,3,4,5,6],null,null,null,null,null]}\n";
        require(ern::serialize_preset(format_fixture) == format_text,
                "Preset output did not match the unversioned format");
        require(ern::parse_preset(format_text) == format_fixture,
                "Unversioned preset did not load");
        require(!ern::parse_preset("{\"effects\":[null,null,null]}"),
                "Preset with fewer than six slots accepted");
        require(!ern::parse_preset("{\"effects\":[[1,2,3],null,null,null,null,null]}"),
                "Preset with fewer than six effect and curse IDs accepted");
        require(ern::valid_preset_name("my-build_2") && ern::valid_preset_name("Katana Ironeye") &&
                    ern::valid_preset_name("COM10") && ern::valid_preset_name("LPT10") &&
                    ern::valid_preset_name("CON-build") && ern::valid_preset_name("NUL_backup") &&
                    ern::valid_preset_name("two  spaces") &&
                    ern::valid_preset_name(std::string(32, 'a')) &&
                    !ern::valid_preset_name(std::string(33, 'a')) &&
                    !ern::valid_preset_name("../outside") && !ern::valid_preset_name(" leading") &&
                    !ern::valid_preset_name("trailing ") && !ern::valid_preset_name("   ") &&
                    !ern::valid_preset_name("tab\tname") && !ern::valid_preset_name("bad/name") &&
                    !ern::valid_preset_name("bad\\name") && !ern::valid_preset_name(""),
                "Preset path validation failed");
        for (const auto *name : {"CON", "con", "CoN", "PRN", "prn", "AUX", "aux", "NUL", "nul"})
            require(!ern::valid_preset_name(name), "A reserved Windows device stem was accepted");
        for (char number = '1'; number <= '9'; ++number) {
            for (const auto *prefix : {"COM", "com", "CoM", "LPT", "lpt", "LpT"})
                require(!ern::valid_preset_name(std::string(prefix) + number),
                        "A reserved Windows port stem was accepted");
        }
    });
}
