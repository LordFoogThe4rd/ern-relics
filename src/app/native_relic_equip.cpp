#include "app/native_relic_equip.hpp"
#include "app/editor_application.hpp"
#include "app/native_edit_diagnostics.hpp"
#include "app/native_callback_hook.hpp"
#include "app/relic_edit_queue.hpp"
#include "game/game_layout.hpp"
#include "game/relic_equipment.hpp"
#include <Windows.h>
#include <sstream>

namespace ern {
namespace {
using NativeUpdate = void (*)(Address, Address);
using NativeSetSlot = void (*)(Address, std::uint32_t, std::int32_t, bool, bool);
using NativeFindItem = std::int32_t (*)(Address, const std::uint32_t *);
using NativeRefresh = void (*)(Address);
struct NativeEquipment {
    RelicEditQueue queue;
    Address module{};
    MemoryRead read;
    MemoryWrite write;
    NativeUpdate original_update{};
    NativeCallbackHook callback;
    std::atomic<std::shared_ptr<NativeEditDiagnostics>> diagnostics;
};
NativeEquipment &native_equipment() {
    // The hook and DLL stay pinned until process exit.
    static auto *value = new NativeEquipment;
    return *value;
}

EditResult commit_in_update(const RelicEditRequest &request, Address character) {
    auto &native = native_equipment();
    const auto diagnostics = native.diagnostics.load();
    const auto set_slot =
        reinterpret_cast<NativeSetSlot>(native.module + game_layout::equipment_set_slot_rva);
    const auto find_item =
        reinterpret_cast<NativeFindItem>(native.module + game_layout::equipment_find_item_rva);
    const auto refresh =
        reinterpret_cast<NativeRefresh>(native.module + game_layout::equipment_refresh_rva);
    const RelicEquipCalls calls{
        [find_item](Address inventory, std::uint32_t handle) {
            return find_item(inventory, &handle);
        },
        [set_slot](Address equipment, std::uint32_t slot, std::int32_t index) {
            set_slot(equipment, slot, index, false, true);
        },
        refresh};
    const auto report = [diagnostics](RelicEquipStep step, const RelicEquipFailure &failure) {
        if (diagnostics)
            diagnostics->reject(step == RelicEquipStep::Unequip ? "unequip" : "equip", failure);
    };
    return replace_equipped_native(native.read, native.write, request, native.module, character,
                                   calls, report);
}

void update_hook(Address character, Address update) {
    auto &native = native_equipment();
    const auto entered = native.diagnostics.load();
    if (entered)
        entered->entered();
    native.original_update(character, update);
    if (entered)
        entered->returned();
    const auto diagnostics = native.diagnostics.load();
    const auto roots = native.queue.pending_roots();
    if (!roots)
        return;
    const auto observed = read_relic_equip_context(native.read, *roots, native.module);
    if (diagnostics && native.diagnostics.load() == diagnostics)
        diagnostics->observe(character, observed);
    const auto *context = std::get_if<RelicEquipContext>(&observed);
    if (context && context->character == character)
        native.queue.process(
            [character](RelicEditRequest request) { return commit_in_update(request, character); });
}
} // namespace

std::optional<std::string> install_native_relic_equip(MemoryRead read, MemoryWrite write) {
    auto &native = native_equipment();
    native.module = reinterpret_cast<Address>(GetModuleHandleW(nullptr));
    struct Entry {
        std::uint32_t rva;
        std::array<std::uint8_t, 16> bytes;
    };
    constexpr std::array<Entry, 4> entries{{
        {game_layout::equipment_update_rva,
         {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x18, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41,
          0x56}},
        {game_layout::equipment_set_slot_rva,
         {0x48, 0x89, 0x5C, 0x24, 0x18, 0x55, 0x56, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x63, 0xDA,
          0x41}},
        {game_layout::equipment_find_item_rva,
         {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20,
          0x41}},
        {game_layout::equipment_refresh_rva,
         {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0xD9, 0xE8, 0x02, 0x41, 0xD6, 0xFF, 0x84,
          0xC0}},
    }};
    for (const auto &entry : entries) {
        auto actual = entry.bytes;
        if (!read(native.module + entry.rva, std::as_writable_bytes(std::span(actual))) ||
            actual != entry.bytes)
            return "The native relic equip layout is not supported. Editing is locked.";
    }
    native.read = std::move(read);
    native.write = std::move(write);
    native.original_update =
        reinterpret_cast<NativeUpdate>(native.module + game_layout::equipment_update_rva);
    return native.callback.install(
        native.read, native.module + game_layout::equipment_update_dispatch_rva,
        reinterpret_cast<Address>(native.original_update), reinterpret_cast<Address>(update_hook));
}

std::optional<std::string> native_relic_equip_error() {
    return native_equipment().callback.error();
}

EditResult commit_native_relic_edit(RelicEditRequest request) {
    auto &native = native_equipment();
    const auto diagnostics = std::make_shared<NativeEditDiagnostics>();
    std::ostringstream label;
    label << "Native relic edit slot=" << request.slot + 1 << " handle=0x" << std::hex
          << request.expected.handle << " player=0x" << request.owner.player;
    application().log(label.str() + " queued");
    native.diagnostics.store(diagnostics);
    const auto read_context = [&native](RelicRoots roots) {
        return read_relic_equip_context(native.read, roots, native.module);
    };
    auto result = native.queue.submit_native(std::move(request), read_context,
                                             native_relic_equip_error, *diagnostics);
    native.diagnostics.store(nullptr);
    std::string outcome;
    if (const auto *rejected = std::get_if<EditRejected>(&result))
        outcome = "rejected reason=" + std::to_string(static_cast<int>(rejected->reason)) +
                  " detail=" + rejected->detail;
    else if (const auto *failed = std::get_if<WriteFailure>(&result))
        outcome = "failed stage=" + std::to_string(static_cast<int>(failed->stage)) +
                  " restoration=" + std::to_string(static_cast<int>(failed->restoration));
    else
        outcome = "completed";
    application().log(label.str() + ' ' + outcome + ' ' + diagnostics->summary());
    return result;
}
} // namespace ern
