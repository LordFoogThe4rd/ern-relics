#include "support/check.hpp"
#include "support/synthetic_rules.hpp"
#include "ui/effect_choices.hpp"
#include <chrono>
#include <iomanip>
#include <string_view>

namespace {
using ern::ui::EffectChoiceInputs;
using ern::ui::PreparedEffectChoices;
using test::require;

EffectChoiceInputs fixture() {
    auto rules = synthetic_rules();
    rules.pools[30][7001802] = {false, true};
    ern::GameAssets assets;
    assets.effect_names = {{7000400, "Strength +1"},
                           {7000401, "Strength +1"},
                           {7000402, "Strength +3"},
                           {7000601, "Attack power"},
                           {7000602, "Poison attack"},
                           {7001801, "Recovery after attack"},
                           {7001802, "Restore HP"},
                           {900001, "Maximum HP reduced"},
                           {900002, "Damage taken increased"},
                           {900003, "Flask recovery reduced"}};
    EffectChoiceInputs inputs{};
    inputs.rules = std::make_shared<const ern::RelicValidator>(std::move(rules));
    inputs.resources = std::make_shared<const ern::GameAssets>(std::move(assets));
    inputs.item_id = 0x80000066;
    inputs.effects = {{7000401, UINT32_MAX}, {7000601, UINT32_MAX}, {7001801, UINT32_MAX}};
    inputs.context = ern::DlcState::Present;
    return inputs;
}

const ern::ui::EffectChoice &choice(const PreparedEffectChoices &choices, std::uint32_t id) {
    for (const auto &group : choices.groups)
        for (const auto &candidate : group)
            if (candidate.id == id)
                return candidate;
    throw std::runtime_error("Expected prepared choice is missing");
}

bool available(const ern::ui::EffectChoice &choice) {
    return std::holds_alternative<ern::AvailableChoice>(choice.validation);
}

void preparation() {
    auto inputs = fixture();
    const auto normal = ern::ui::prepare_effect_choices(inputs);
    require(!normal.deep && !normal.curses && !normal.can_clear,
            "Normal effect preparation exposed curse controls");
    const auto &attributes = normal.groups[static_cast<std::size_t>(ern::EffectGroup::Attributes)];
    require(attributes.size() == 3 && attributes[0].id == 7000400 && attributes[1].id == 7000401 &&
                attributes[2].id == 7000402,
            "Grouping changed alphabetical order or the ID tie-breaker");
    require(available(choice(normal, 7000400)) && choice(normal, 7000400).tag.empty(),
            "A normal first-row replacement was incorrectly blocked or tagged");
    inputs.position = {1, ern::EffectField::Effect};
    const auto later = ern::ui::prepare_effect_choices(inputs);
    require(!available(choice(later, 7000400)) && choice(later, 7000400).tag == "Unavailable" &&
                available(choice(later, 7000602)),
            "Preparation ignored earlier-row conflicts or discarded unavailable "
            "choices");
    inputs.position = {2, ern::EffectField::Effect};
    inputs.context = ern::DlcState::Absent;
    require(!available(choice(ern::ui::prepare_effect_choices(inputs), 7001802)),
            "A DLC-only effect was available without the DLC");
    inputs.context = ern::UnknownRules{"Synthetic rules unavailable"};
    const auto unknown = ern::ui::prepare_effect_choices(inputs);
    require(choice(unknown, 7001802).tag == "Rules unavailable" &&
                ern::validation_reason(choice(unknown, 7001802).validation) ==
                    "Synthetic rules unavailable",
            "Unknown-rule choices lost their diagnostic reason");

    inputs = fixture();
    inputs.item_id = 0x80000000u | 2000000u;
    inputs.effects.rows[2].curse = 900001;
    inputs.position = {2, ern::EffectField::Effect};
    const auto deep = ern::ui::prepare_effect_choices(inputs);
    require(deep.deep && !deep.curses && available(choice(deep, 7001802)) &&
                choice(deep, 7001802).tag == "Curse required",
            "Deep positive choices lost paired-curse availability or its tag");
    inputs.position.field = ern::EffectField::Curse;
    const auto curses = ern::ui::prepare_effect_choices(inputs);
    require(curses.deep && curses.curses && !curses.can_clear && available(choice(curses, 900002)),
            "Required curse preparation permitted clearing or blocked a legal "
            "paired curse");
    inputs.position = {0, ern::EffectField::Curse};
    require(ern::ui::prepare_effect_choices(inputs).can_clear,
            "An optional curse could not be cleared");
    inputs = fixture();
    inputs.position = {0, ern::EffectField::Curse};
    inputs.invalid = true;
    const auto invalid = ern::ui::prepare_effect_choices(inputs);
    require(invalid.curses && invalid.can_clear && available(choice(invalid, 900001)) &&
                available(choice(invalid, 8100000)) && choice(invalid, 900001).tag.empty(),
            "Invalid mode did not expose every effect and curse in any field");
}

void caching() {
    ern::ui::EffectChoiceCache cache;
    auto inputs = fixture();
    const auto first = cache.get(inputs);
    require(cache.get(inputs) == first, "Unchanged choice inputs rebuilt the prepared model");
    auto changed = inputs;
    auto revised_rules = synthetic_rules();
    revised_rules.effects.at(7000400).text_id = -1;
    changed.rules = std::make_shared<const ern::RelicValidator>(std::move(revised_rules));
    auto previous = cache.get(changed);
    require(previous != first && choice(*previous, 7000400).description == "Effect ID 7000400" &&
                choice(*first, 7000400).description == "Strength +1",
            "New immutable rules reused the previous choices or mutated a "
            "retained result");
    auto revised_assets = std::make_shared<ern::GameAssets>(*inputs.resources);
    revised_assets->effect_names[7000402] = "Strength +30";
    changed.resources = std::move(revised_assets);
    auto next = cache.get(changed);
    require(next != previous && choice(*next, 7000402).description == "Strength +30" &&
                choice(*previous, 7000402).description == "Strength +3",
            "New immutable assets reused old descriptions or mutated a retained "
            "result");
    previous = next;
    changed.item_id = 0x80000000u | 2000000u;
    next = cache.get(changed);
    require(next != previous && next->deep, "A different item reused the normal choice model");
    previous = next;
    changed.effects.rows[0].effect = 7000400;
    next = cache.get(changed);
    require(next != previous, "A changed draft reused old choice validation");
    previous = next;
    changed.position.row = 1;
    next = cache.get(changed);
    require(next != previous && !available(choice(*next, 7000401)),
            "A different selected row reused validation for the previous row");
    previous = next;
    changed.position.field = ern::EffectField::Curse;
    next = cache.get(changed);
    require(next != previous && next->curses, "A different selected field reused positive choices");
    previous = next;
    changed.context = ern::DlcState::Absent;
    next = cache.get(changed);
    require(next != previous, "Changed DLC ownership reused the previous rule context");
    previous = next;
    changed.context = ern::UnknownRules{"First reason"};
    next = cache.get(changed);
    require(next != previous &&
                ern::validation_reason(choice(*next, 900001).validation) == "First reason",
            "Unknown rules reused the available-choice model");
    previous = next;
    changed.context = ern::UnknownRules{"Second reason"};
    next = cache.get(changed);
    require(next != previous &&
                ern::validation_reason(choice(*next, 900001).validation) == "Second reason" &&
                cache.get(changed) == next,
            "Unknown-rule reasons did not invalidate by value or an equal value "
            "missed the cache");
    previous = next;
    changed.invalid = true;
    next = cache.get(changed);
    require(next != previous && available(choice(*next, 8100000)),
            "Invalid mode did not invalidate the cached choice model");

    std::weak_ptr<const ern::RelicValidator> retained_rules;
    std::weak_ptr<const ern::GameAssets> retained_assets;
    {
        auto temporary = fixture();
        retained_rules = temporary.rules;
        retained_assets = temporary.resources;
        cache.get(std::move(temporary));
    }
    require(!retained_rules.expired() && !retained_assets.expired(),
            "The cache borrowed immutable inputs from an expired published view");
    cache = {};
    require(retained_rules.expired() && retained_assets.expired(),
            "Discarding the cache retained unused rule or resource snapshots");
}

EffectChoiceInputs catalogue(std::uint32_t count, bool deep) {
    ern::RegulationRules rules;
    ern::GameAssets assets;
    constexpr std::array labels{
        "Strength +",        "Attack power ", "Restore HP ",      "Damage negation ",
        "Rune acquisition ", "Spell damage ", "[Fixture] Skill ", "Other ",
    };
    for (std::uint32_t index = 0; index < count; ++index) {
        const auto id = 1000u + index;
        rules.effects.emplace(id, ern::Effect{id, -1, static_cast<int>(id), static_cast<int>(id)});
        assets.effect_names[static_cast<int>(id)] =
            labels[index % labels.size()] + std::to_string(index);
        const auto table = 10 + static_cast<int>(index % 3) * 10;
        rules.pools[table][id] = {index % 5 != 0, true};
    }
    if (deep) {
        for (std::uint32_t index = 0; index < 64; ++index) {
            const auto id = 100000u + index;
            rules.effects.emplace(id,
                                  ern::Effect{id, -1, static_cast<int>(id), static_cast<int>(id)});
            assets.effect_names[static_cast<int>(id)] = "Damage taken " + std::to_string(index);
            rules.pools[40][id] = {true, true};
        }
    }
    rules.items.emplace(100,
                        ern::RelicInfo{100,
                                       ern::RelicKind::Rolled,
                                       0,
                                       0,
                                       ern::empty_effects,
                                       {10, 20, 30},
                                       deep ? std::array{40, -1, -1} : std::array{-1, -1, -1},
                                       deep ? ern::RelicFamily::Deep : ern::RelicFamily::Normal});
    EffectChoiceInputs inputs{};
    inputs.rules = std::make_shared<const ern::RelicValidator>(std::move(rules));
    inputs.resources = std::make_shared<const ern::GameAssets>(std::move(assets));
    inputs.item_id = 0x80000064;
    inputs.effects = {{1001, UINT32_MAX}, {1003, UINT32_MAX}, {1005, UINT32_MAX}};
    inputs.position = {2, ern::EffectField::Effect};
    inputs.context = ern::DlcState::Present;
    return inputs;
}

std::uint64_t checksum(const PreparedEffectChoices &choices) {
    std::uint64_t result = 0;
    for (const auto &group : choices.groups)
        for (const auto &candidate : group)
            result += candidate.id + candidate.description.size() + candidate.validation.index() +
                      candidate.tag.size();
    return result;
}

void benchmark() {
    using Clock = std::chrono::steady_clock;
    constexpr int preparations = 20, hits = 20000;
    for (const bool deep : {false, true}) {
        for (const auto count : {256u, 1024u, 4096u}) {
            auto inputs = catalogue(count, deep);
            const auto expected = checksum(ern::ui::prepare_effect_choices(inputs));
            std::uint64_t prepared_sum = 0;
            auto start = Clock::now();
            for (int iteration = 0; iteration < preparations; ++iteration)
                prepared_sum += checksum(ern::ui::prepare_effect_choices(inputs));
            const auto prepare_time = Clock::now() - start;
            ern::ui::EffectChoiceCache cache;
            const auto prepared = cache.get(inputs);
            std::uint64_t hits_sum = 0;
            start = Clock::now();
            for (int iteration = 0; iteration < hits; ++iteration) {
                const auto reused = cache.get(inputs);
                require(reused == prepared, "Benchmark cache hit rebuilt the model");
                hits_sum += reused->groups.front().size();
            }
            const auto hit_time = Clock::now() - start;
            require(prepared_sum == expected * preparations && checksum(*prepared) == expected &&
                        hits_sum == prepared->groups.front().size() * hits,
                    "Benchmark preparation and cached output disagree");
            std::cout << (deep ? "deep" : "normal") << " effects=" << count << " prepare_us="
                      << std::chrono::duration<double, std::micro>(prepare_time).count() /
                             preparations
                      << " cache_hit_us="
                      << std::chrono::duration<double, std::micro>(hit_time).count() / hits
                      << " checksum=" << expected << '\n';
        }
    }
}
} // namespace

int main(int argc, char **argv) {
    return test::run("effect choice preparation, cache ownership and invalidation", [&] {
        preparation();
        caching();
        if (argc == 2 && std::string_view(argv[1]) == "--benchmark")
            benchmark();
        else
            require(argc == 1, "Use --benchmark to measure synthetic choice preparation");
    });
}
