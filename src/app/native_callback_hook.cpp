#include "app/native_callback_hook.hpp"
#include <Windows.h>

namespace ern {
std::optional<std::string> NativeCallbackHook::install(MemoryRead read, Address slot,
                                                       Address original, Address callback) {
    if (binding_)
        return "The equipment update callback is already installed.";
    if (!slot || slot % alignof(Address) != 0 || !original || !callback ||
        read_value<Address>(read, slot) != original)
        return "The equipment update callback layout is not supported. Editing is locked.";
    DWORD protection{};
    if (!VirtualProtect(reinterpret_cast<void *>(slot), sizeof(Address), PAGE_READWRITE,
                        &protection))
        return "Cannot access the equipment update callback. Editing is locked.";
    auto *pointer = reinterpret_cast<PVOID volatile *>(slot);
    const auto previous = InterlockedCompareExchangePointer(
        pointer, reinterpret_cast<void *>(callback), reinterpret_cast<void *>(original));
    DWORD ignored{};
    if (!VirtualProtect(reinterpret_cast<void *>(slot), sizeof(Address), protection, &ignored)) {
        // Roll back only our pointer; never overwrite a concurrent owner's replacement.
        InterlockedCompareExchangePointer(pointer, reinterpret_cast<void *>(original),
                                          reinterpret_cast<void *>(callback));
        VirtualProtect(reinterpret_cast<void *>(slot), sizeof(Address), protection, &ignored);
        return "Cannot restore the equipment callback's protection. Editing is locked.";
    }
    if (reinterpret_cast<Address>(previous) != original)
        return "The equipment update callback changed during installation. Editing is locked.";
    binding_ = Binding{std::move(read), slot, callback};
    return std::nullopt;
}

std::optional<std::string> NativeCallbackHook::error() const {
    if (!binding_)
        return "The equipment update callback is not installed. Editing is locked.";
    const auto observed = read_value<Address>(binding_->read, binding_->slot);
    if (!observed)
        return "Cannot read the equipment update callback. Editing is locked.";
    if (*observed != binding_->callback)
        return "The equipment update callback was replaced. Editing is locked. Restart Nightreign.";
    return std::nullopt;
}
} // namespace ern
