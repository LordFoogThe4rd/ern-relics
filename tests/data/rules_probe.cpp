#include "app/active_regulation.hpp"
#include "relics/effect_encoding.hpp"
#include <iostream>

namespace {
int dump(const std::filesystem::path &path, const std::filesystem::path &game) {
    const auto file = ern::read_file(path);
    if (const auto *error = std::get_if<ern::FileError>(&file)) {
        std::cerr << error->message;
        return 1;
    }
    const auto &content = std::get<ern::FileContent>(file);
    const auto parsed = ern::read_regulation(content.bytes, game);
    if (const auto *error = std::get_if<ern::FileError>(&parsed)) {
        std::cerr << error->message;
        return 1;
    }
    const auto &rules = std::get<ern::RegulationRules>(parsed);
    std::cout << "SOURCE\t" << content.sha256 << '\n';
    for (const auto &[id, item] : rules.items) {
        // Keep the public probe protocol independent of internal classification enums.
        int kind = 0;
        switch (item.kind) {
        case ern::RelicKind::Rolled:
            kind = item.family == ern::RelicFamily::Deep ? 3 : 0;
            break;
        case ern::RelicKind::Unique:
            kind = 1;
            break;
        case ern::RelicKind::Fixed:
            kind = 2;
            break;
        }
        std::cout << "ITEM\t" << id << '\t' << kind << '\t' << item.color << '\t' << item.icon;
        for (const auto table : item.effect_tables)
            std::cout << '\t' << table;
        for (const auto effect : ern::encode_effects(item.fixed_effects))
            std::cout << '\t' << effect;
        for (const auto table : item.curse_tables)
            std::cout << '\t' << table;
        std::cout << '\t' << (item.family == ern::RelicFamily::Deep);
        std::cout << '\n';
    }
    for (const auto &[id, effect] : rules.effects)
        std::cout << "EFFECT\t" << id << '\t' << effect.compatibility << '\t' << effect.order
                  << '\t' << effect.exclusivity << '\n';
    for (const auto &[table, pool] : rules.pools)
        for (const auto &[effect, weight] : pool)
            std::cout << "POOL\t" << table << '\t' << effect << '\t' << weight.base << '\t'
                      << weight.dlc << '\n';
    return 0;
}
} // namespace

// Batch interface for tools.assets.verify_regulation. Uses the production validator.
int wmain(int argc, wchar_t **argv) {
    if (argc == 4 && std::wstring_view(argv[3]) == L"--dump")
        return dump(argv[1], argv[2]);
    if (argc != 3)
        return 2;
    ern::ActiveRegulation active{argv[1], argv[2]};
    active.verify();
    {
        const auto checked = active.context(ern::DlcState::Unknown);
        if (const auto *error = std::get_if<ern::UnknownRules>(&checked)) {
            std::cerr << error->reason;
            return 1;
        }
        const auto resolved = active.diagnostics().resolved_path.u8string();
        std::cerr << "Opened: " << std::string(resolved.begin(), resolved.end())
                  << "\nActive SHA-256: " << active.source_hash() << '\n';
    }
    std::uint32_t item = 0;
    unsigned int dlc = 0;
    ern::EncodedEffects fields;
    while (std::cin >> item >> dlc) {
        for (auto &id : fields)
            if (!(std::cin >> id))
                return 2;
        if (dlc > 2)
            return 2;
        const auto context = dlc == 0   ? ern::DlcState::Absent
                             : dlc == 1 ? ern::DlcState::Present
                                        : ern::DlcState::Unknown;
        const auto result =
            active.validator().validate(item | 0x80000000u, ern::decode_effects(fields), context);
        std::cout << ern::validation_label(result);
        if (const auto *valid = std::get_if<ern::Obtainable>(&result))
            for (const auto id : ern::encode_effects(valid->effects))
                std::cout << '\t' << id;
        std::cout << '\n';
    }
    return std::cin.eof() ? 0 : 2;
}
