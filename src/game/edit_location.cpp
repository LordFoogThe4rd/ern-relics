#include "game/game_layout.hpp"
#include "game/edit_location.hpp"

namespace ern {
EditLocation read_edit_location(const MemoryRead &read, RelicRoots roots) {
    const auto game = read_value<Address>(read, roots.game_data_global);
    const auto world = read_value<Address>(read, roots.world_chr_global);
    if (!game || !world)
        return EditLocation::Unavailable;
    if (!*game || !*world)
        return EditLocation::NotLoaded;
    const auto player = read_value<Address>(read, *world + game_layout::live_player);
    if (!player)
        return EditLocation::Unavailable;
    if (!*player)
        return read_value<Address>(read, roots.game_data_global) == game &&
                       read_value<Address>(read, roots.world_chr_global) == world
                   ? EditLocation::NotLoaded
                   : EditLocation::Unavailable;
    if (!read_value<Address>(read, *player + game_layout::player_presence))
        return EditLocation::Unavailable;
    // App 1.03.3: GameDataMan world type 0 is the hub. The same field is used by
    // RConnectMod's InHub(), independently of its Seamless Co-op integration.
    const auto world_type = read_value<std::uint8_t>(read, *game + game_layout::world_type);
    if (!world_type || read_value<Address>(read, roots.game_data_global) != game ||
        read_value<Address>(read, roots.world_chr_global) != world ||
        read_value<Address>(read, *world + game_layout::live_player) != player ||
        read_value<std::uint8_t>(read, *game + game_layout::world_type) != world_type)
        return EditLocation::Unavailable;
    return *world_type == game_layout::roundtable_world ? EditLocation::RoundtableHold
                                                        : EditLocation::OutsideRoundtable;
}

const char *location_lock_reason(EditLocation location) {
    switch (location) {
    case EditLocation::RoundtableHold:
        return nullptr;
    case EditLocation::OutsideRoundtable:
        return "Relic editing is only available in Roundtable Hold. Return there to make changes.";
    case EditLocation::Unavailable:
    case EditLocation::NotLoaded:
        return "Relic editing is locked until your character is loaded in Roundtable Hold.";
    }
    return "Cannot confirm that your character is in Roundtable Hold.";
}
} // namespace ern
