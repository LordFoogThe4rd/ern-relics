#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>

// Memory contract for App 1.03.3. Do not infer field offsets from
// regulation tables. See docs/architecture.md and the pinned references in
// ThirdPartyNotices.txt before adding support for another executable build.
namespace ern::game_layout {
inline constexpr std::string_view executable_version = "1.3.3.0";
inline constexpr std::uint32_t equipped_relics = 0x2F4;
inline constexpr std::size_t player_data = 8, inventory_entries = 8, inventory_stride = 8;
inline constexpr std::size_t item_id = 0x0C, item_effects = 0x18;
inline constexpr std::size_t item_curses = 0x40;
inline constexpr std::size_t live_player = 0x174E8, player_presence = 8, world_type = 0xD8;
inline constexpr std::uint8_t roundtable_world = 0;
inline constexpr std::uint32_t forsaken_hollows_app = 3531720;
// Native relic effect controller for executable 1.3.3.0.
inline constexpr std::size_t relic_effect_controller = 0x650;
inline constexpr std::uint32_t relic_effect_vtable_rva = 0x2C1AB28;
inline constexpr std::uint32_t relic_cache_initializer_rva = 0x4FF046;
inline constexpr std::uint32_t relic_cache_loop_limit_rva = 0x4FEE72;
inline constexpr std::uint32_t relic_scratch_bitmap_initializer_rva = 0x4FEABD;
// Native equipment update, slot setter, inventory lookup, and full equip refresh.
inline constexpr std::uint32_t equipment_update_rva = 0x675D20;
// CS::PlayerIns vtable, slot 17 (+0x88). Redirect data, leaving protected code intact.
inline constexpr std::uint32_t equipment_update_dispatch_rva = 0x2C30880;
inline constexpr std::uint32_t equipment_set_slot_rva = 0x1F3430;
inline constexpr std::uint32_t equipment_find_item_rva = 0x1F5550;
inline constexpr std::uint32_t equipment_refresh_rva = 0x670070;
inline constexpr std::size_t player_equipment = 0x1D8, equipment_inventory = 0x1C8;
inline constexpr std::uint32_t first_relic_equip_slot = 0x1A;
inline constexpr std::size_t character_data = 0x5A0, character_equipment = 0x670;
inline constexpr std::size_t character_effects = 0x190;
inline constexpr std::size_t effect_owner = 8, effect_cache = 0x18;
inline constexpr std::size_t effect_manager = 0x10, effect_equipment = 0x28;
inline constexpr std::size_t effect_count = 0x20, effect_mode = 0x38;
inline constexpr std::size_t effect_bitmap_words = 0x48, effect_bitmap_bits = 0x60;
inline constexpr std::string_view item_signature =
    "8B 03 89 44 24 40 48 8B 0D ?? ?? ?? ?? 48 85 C9";
inline constexpr std::string_view game_signature = "48 8B 05 ?? ?? ?? ?? 48 8B 70 08";
inline constexpr std::string_view world_signature = "48 8B 05 ?? ?? ?? ?? 0F 28 F1 48 85 C0";
inline constexpr std::size_t item_displacement = 9, game_displacement = 3, world_displacement = 3;
} // namespace ern::game_layout
