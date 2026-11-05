#include "app/active_regulation.hpp"
#include "support/check.hpp"
#include "support/regulation_fixture.hpp"
#include "data/catalog_presentation.hpp"
#include <chrono>
#include <cstring>
#include <fstream>

using test::require;

namespace {
using namespace test::regulation;
void write(const std::filesystem::path &path, const Bytes &data) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(reinterpret_cast<const char *>(data.data()),
               static_cast<std::streamsize>(data.size()));
    require(bool(file), "Cannot write regulation fixture");
}

void fixed_deep_effects() {
    const auto items = param(2, {{2000010, item(1, 1, {40, 41, 42}, {50, 51, 52})},
                                 {2000011, item(0, 1, {40, 41, 42}, {50, 51, 52})},
                                 {2000012, item(1, 1, {40, 41, 42}, {-1, 53, 54})},
                                 {2000013, item(0, 1, {40, 41, 42}, {55, 56, 57})},
                                 {1003005, item(1, 0, {40, 41, 42}, {50, 51, 52})}});
    const auto effects = param(3, {{101, effect(101)},
                                   {102, effect(102)},
                                   {103, effect(103)},
                                   {901, effect(901)},
                                   {902, effect(902)},
                                   {903, effect(903)}});
    const auto tables = param(1, {{40, weight(101)},
                                  {41, weight(102)},
                                  {42, weight(103)},
                                  {50, weight(901)},
                                  {50, weight(902, 0, 0)},
                                  {51, weight(902, 0, 1)},
                                  {52, weight(903)},
                                  {53, weight(-1)},
                                  {55, weight(901)},
                                  {55, weight(-1)},
                                  {56, weight(902)},
                                  {56, weight(903)},
                                  {57, weight(901, 0, 0)}});
    const auto parsed = ern::read_regulation(binder({items, effects, tables}), {});
    require(std::holds_alternative<ern::RegulationRules>(parsed),
            "Fixed Deep regulation fixture failed to decode");
    const ern::RelicValidator validator{std::get<ern::RegulationRules>(parsed)};
    ern::GameAssets assets;
    assets.effect_names = {
        {901, "Fixture curse one"}, {902, "Fixture curse two"}, {903, "Fixture curse three"}};
    const ern::CatalogPresentation presentation{validator, assets};
    const ern::Effects expected{{101, 901}, {102, 902}, {103, 903}};
    for (const auto &[id, kind] : {std::pair{2000010u, ern::RelicKind::Fixed},
                                   std::pair{2000011u, ern::RelicKind::Unique}}) {
        const auto *info = validator.items().find(0x80000000u | id);
        require(info && info->family == ern::RelicFamily::Deep && info->kind == kind &&
                    ern::relic_lock_reason(info) && info->fixed_effects == expected,
                "Fixed or unique Deep display data lost its effect/curse pairs or read-only kind");
        for (std::size_t slot = 0; slot < expected.rows.size(); ++slot)
            require(
                presentation.describe(info->fixed_effects.rows[slot].curse) ==
                    assets.effect_names.at(static_cast<std::int32_t>(expected.rows[slot].curse)),
                "A fixed Deep curse did not reach its display description");
    }
    const ern::Effects without_curses{{101, UINT32_MAX}, {102, UINT32_MAX}, {103, UINT32_MAX}};
    for (const auto id : {2000012u, 2000013u, 1003005u})
        require(
            validator.items().find(0x80000000u | id)->fixed_effects == without_curses,
            "An absent, empty, ambiguous, inactive, or normal relic curse was treated as fixed");
}
} // namespace

int main() {
    return test::run(
        "active rules, mod effects, Unicode paths, resolved files, full fingerprints, ordering, DLC, unique/fixed/Deep locks, corrupt data and read errors",
        [] {
            fixed_deep_effects();
            const auto items = param(2, {{1003002, item(1, 0, {10, 20, 30})},
                                         {10000, item(0, 0, {40, -1, -1})},
                                         {1003004, item(1, 0, {40, -1, -1})},
                                         {2000000, item(1, 1, {10, 20, 30}, {40, -1, -1})},
                                         {2000001, item(1, 1, {40, -1, -1})},
                                         {2000002, item(0, 1, {40, -1, -1})}});
            const auto effects = param(3, {{101, effect(101)},
                                           {102, effect(102)},
                                           {201, effect(201)},
                                           {202, effect(202)},
                                           {301, effect(301)},
                                           {302, effect(302)}});
            const auto tables = param(1, {{10, weight(101)},
                                          {10, weight(102)},
                                          {20, weight(201)},
                                          {20, weight(202)},
                                          {30, weight(301)},
                                          {30, weight(302, 0, 1)},
                                          {40, weight(101)}});
            const auto original = binder({items, effects, tables});
            const auto parsed = ern::read_regulation(original, {});
            require(std::holds_alternative<ern::RegulationRules>(parsed),
                    "Plain active regulation failed to decode");
            const auto &rules = std::get<ern::RegulationRules>(parsed);
            require(rules.items.at(1003002).kind == ern::RelicKind::Rolled,
                    "Rolled relic was not derived from its lotteries");
            require(rules.items.at(10000).kind == ern::RelicKind::Unique &&
                        rules.items.at(1003004).kind == ern::RelicKind::Fixed &&
                        rules.items.at(2000000).kind == ern::RelicKind::Rolled &&
                        rules.items.at(2000000).family == ern::RelicFamily::Deep,
                    "Active definitions lost their relic kinds");
            require(rules.items.at(2000000).curse_tables ==
                            std::array<std::int32_t, 3>{40, -1, -1} &&
                        rules.items.at(2000000).family == ern::RelicFamily::Deep &&
                        rules.items.at(2000001).kind == ern::RelicKind::Fixed &&
                        rules.items.at(2000002).kind == ern::RelicKind::Unique &&
                        ern::relic_lock_reason(&rules.items.at(2000001)) &&
                        ern::relic_lock_reason(&rules.items.at(2000002)),
                    "Curse tables were lost or fixed/unique Deep relics became editable");
            require(rules.items.at(10000).fixed_effects == ern::Effects{{101, UINT32_MAX},
                                                                        {UINT32_MAX, UINT32_MAX},
                                                                        {UINT32_MAX, UINT32_MAX}},
                    "Fixed effects were not derived from active tables");
            require(!rules.pools.at(30).at(302).base && rules.pools.at(30).at(302).dlc,
                    "DLC overrides were not decoded");
            const auto without_ids =
                ern::read_regulation(binder({items, effects, tables}, false), {});
            require(std::holds_alternative<ern::RegulationRules>(without_ids) &&
                        std::get<ern::RegulationRules>(without_ids).pools.at(30).at(302).dlc,
                    "Named binder entries without optional IDs were not decoded");
            auto unnamed = original;
            unnamed[49] = 0x22;
            require(std::holds_alternative<ern::FileError>(ern::read_regulation(unnamed, {})),
                    "An unnamed binder was accepted");

            const auto temporary =
                std::filesystem::temp_directory_path() /
                ("ern-relics-regulation-test-" +
                 std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
            std::filesystem::create_directories(temporary);
            const auto path = temporary / L"regulation-\u0142\u00f3\u017c.bin";
            write(path, original);
            ern::ActiveRegulation active{path};
            const auto context_for = [&](ern::DlcState dlc) {
                active.verify();
                return active.context(dlc);
            };
            const auto reference = active.source_hash();
            auto context = context_for(ern::DlcState::Absent);
            require(std::holds_alternative<ern::DlcState>(context),
                    "A different regulation was rejected instead of deriving its rules");
            require(active.source_hash() != reference &&
                        active.source_hash() == active.diagnostics().detected_hash &&
                        active.diagnostics().resolved_path.filename() == path.filename(),
                    "Active fingerprint or resolved Unicode path was lost");
            require(ern::CatalogPresentation{active.validator(), {}}.name(
                        active.validator().items().find(0x80000000u | 1003002)) ==
                        "Relic ID 1003002",
                    "Unknown item did not receive a numeric label");
            require(ern::CatalogPresentation{active.validator(), {}}.describe(101) ==
                        "Effect ID 101",
                    "New mod effects need numeric labels");
            const ern::Effects base{{101, UINT32_MAX}, {201, UINT32_MAX}, {301, UINT32_MAX}},
                dlc{{102, UINT32_MAX}, {202, UINT32_MAX}, {302, UINT32_MAX}};
            const auto valid = active.validator().validate(0x80000000u | 1003002, base, context);
            require(std::holds_alternative<ern::Obtainable>(valid) &&
                        std::get<ern::Obtainable>(valid).effects ==
                            ern::Effects{{301, UINT32_MAX}, {201, UINT32_MAX}, {101, UINT32_MAX}},
                    "Validation did not use the active tables and ordering");
            require(std::holds_alternative<ern::Impossible>(
                        active.validator().validate(0x80000000u | 1003002, dlc, context)),
                    "DLC-only effect was accepted without DLC");
            context = context_for(ern::DlcState::Present);
            require(std::holds_alternative<ern::Obtainable>(
                        active.validator().validate(0x80000000u | 1003002, dlc, context)),
                    "Installed DLC rules were ignored");
            require(std::holds_alternative<ern::UnknownRules>(active.validator().validate(
                        0x80000000u | 1003002, dlc, ern::DlcState::Unknown)),
                    "Unknown DLC status bypassed active weights");

            auto changed_effects = effects;
            put(changed_effects, 64 + 6 * 24 + 24, std::int32_t(1));
            put(changed_effects, 64 + 6 * 24 + 2 * 68 + 24, std::int32_t(1));
            const auto changed = binder({items, changed_effects, tables});
            const auto previous_source = active.source_hash();
            write(path, changed);
            context = context_for(ern::DlcState::Present);
            require(active.source_hash() != previous_source &&
                        active.source_hash() == ern::sha256_file(path),
                    "Changed regulation reused stale rules");
            require(ern::validation_reason(
                        active.validator().validate(0x80000000u | 1003002, base, context))
                            .find("exclusion") != std::string::npos,
                    "Active exclusion groups were ignored");

            for (const auto size : {std::size_t(0), std::size_t(1), std::size_t(15),
                                    std::size_t(63), std::size_t(127)})
                require(std::holds_alternative<ern::FileError>(
                            ern::read_regulation(std::span(original).first(size), {})),
                        "Truncated regulation was accepted");
            auto invalid = original;
            put(invalid, 64 + 24, UINT32_MAX);
            require(std::holds_alternative<ern::FileError>(ern::read_regulation(invalid, {})),
                    "Out-of-range binder offset was accepted");
            auto wrong_version = effects;
            put(wrong_version, 8, std::uint16_t(99));
            require(std::holds_alternative<ern::FileError>(
                        ern::read_regulation(binder({items, wrong_version, tables}), {})),
                    "Unsupported PARAM version was accepted");
            auto wrong_size = effects;
            put(wrong_size, 72, std::uint64_t(0));
            require(std::holds_alternative<ern::FileError>(
                        ern::read_regulation(binder({items, wrong_size, tables}), {})),
                    "Invalid PARAM row offsets were accepted");
            write(path, binder({items, wrong_version, tables}));
            context = context_for(ern::DlcState::Present);
            require(std::holds_alternative<ern::UnknownRules>(context) &&
                        !active.diagnostics().error.empty() &&
                        !active.diagnostics().detected_hash.empty() &&
                        std::holds_alternative<ern::UnknownRules>(
                            active.validator().validate(0x80000000u | 1003002, base, context)),
                    "Malformed data reused previously accepted rules or lost diagnostics");
            write(path, original);
            require(std::holds_alternative<ern::DlcState>(context_for(ern::DlcState::Present)),
                    "Valid rules did not recover after a read error");
            active.lock_source();
            const auto locked_hash = active.source_hash();
            write(path, changed);
            context = context_for(ern::DlcState::Present);
            require(std::holds_alternative<ern::UnknownRules>(context) &&
                        active.diagnostics().error.find("Restart") != std::string::npos &&
                        active.source_hash() == locked_hash &&
                        std::holds_alternative<ern::Obtainable>(active.validator().validate(
                            0x80000000u | 1003002, base, ern::DlcState::Absent)),
                    "Changing rules after character load did not require a restart");
            std::filesystem::remove(path);
            context = context_for(ern::DlcState::Present);
            require(std::holds_alternative<ern::UnknownRules>(context) &&
                        active.diagnostics().detected_hash.empty() &&
                        active.diagnostics().error.find("Cannot read") != std::string::npos,
                    "Missing files were misreported as hash mismatches");
            write(path, original);
            {
                ern::ActiveRegulation watched{path};
                watched.verify();
                watched.lock_source();
                watched.start(std::chrono::milliseconds(1));
                write(path, changed);
                const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
                do {
                    watched.poll();
                    if (std::holds_alternative<ern::UnknownRules>(
                            watched.context(ern::DlcState::Absent)))
                        break;
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                } while (std::chrono::steady_clock::now() < deadline);
                require(std::holds_alternative<ern::UnknownRules>(
                            watched.context(ern::DlcState::Absent)) &&
                            watched.source_hash() == locked_hash &&
                            watched.diagnostics().error.find("Restart") != std::string::npos,
                        "Background observations replaced the character's bound rules");
            }
            std::filesystem::remove(path);
            std::filesystem::remove(temporary);
        });
}
