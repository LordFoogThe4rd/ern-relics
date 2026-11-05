#include "app/active_regulation.hpp"
#include "data/catalog_presentation.hpp"
#include "data/game_assets.hpp"
#include <fstream>
#include <iostream>

int wmain(int argc, wchar_t **argv) {
    if (argc != 4)
        return 2;
    ern::ActiveRegulation active{argv[2], argv[1]};
    active.verify();
    const auto context = active.context(ern::DlcState::Unknown);
    if (const auto *error = std::get_if<ern::UnknownRules>(&context)) {
        std::cerr << error->reason;
        return 1;
    }
    const auto assets = ern::read_game_assets(argv[1], ern::requested_icons(active.validator()));
    const std::filesystem::path directory = argv[3];
    std::filesystem::create_directories(directory);
    std::ofstream rgba(directory / "atlas.rgba", std::ios::binary);
    rgba.write(reinterpret_cast<const char *>(assets.atlas.rgba.data()),
               static_cast<std::streamsize>(assets.atlas.rgba.size()));
    std::ofstream metadata(directory / "assets.tsv");
    metadata << "ATLAS\t" << assets.atlas.width << '\t' << assets.atlas.height << '\n';
    for (const auto &[id, name] : assets.item_names)
        metadata << "ITEM\t" << id << '\t' << name << '\n';
    for (const auto &[id, name] : assets.effect_names)
        metadata << "EFFECT\t" << id << '\t' << name << '\n';
    for (const auto &[key, cell] : assets.cells)
        metadata << "ICON\t" << static_cast<int>(key.kind) << '\t' << key.id << '\t' << cell
                 << '\n';
    for (const auto &warning : assets.warnings)
        std::cerr << warning << '\n';
    std::cout << assets.item_names.size() << " item names, " << assets.effect_names.size()
              << " effect names, " << assets.cells.size() << " icons\n";
    return assets.item_names.empty() || assets.effect_names.empty() || assets.cells.empty() ||
                   !metadata || !rgba
               ? 1
               : 0;
}
