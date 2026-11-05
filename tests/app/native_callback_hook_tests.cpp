#include "app/native_callback_hook.hpp"
#include "support/check.hpp"
#include <Windows.h>
#include <array>

using test::require;
namespace {
using Callback = void (*)(unsigned &);
void original(unsigned &calls) { ++calls; }
void replacement(unsigned &calls) {
    original(calls);
    calls += 10;
}
void other(unsigned &calls) { calls += 100; }
bool read(ern::Address address, std::span<std::byte> output) {
    SIZE_T copied{};
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), output.data(),
                             output.size(), &copied) &&
           copied == output.size();
}
struct Dispatch {
    ern::Address *slot = static_cast<ern::Address *>(
        VirtualAlloc(nullptr, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    Dispatch() {
        require(slot != nullptr, "Cannot allocate dispatch fixture");
        *slot = reinterpret_cast<ern::Address>(&original);
        DWORD ignored{};
        require(VirtualProtect(slot, sizeof(*slot), PAGE_READONLY, &ignored),
                "Cannot protect dispatch fixture");
    }
    ~Dispatch() { VirtualFree(slot, 0, MEM_RELEASE); }
    ern::Address address() const { return reinterpret_cast<ern::Address>(slot); }
    void replace(Callback callback) {
        DWORD protection{}, ignored{};
        require(VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &protection),
                "Cannot replace fixture callback");
        InterlockedExchangePointer(reinterpret_cast<PVOID volatile *>(slot),
                                   reinterpret_cast<void *>(callback));
        require(VirtualProtect(slot, sizeof(*slot), protection, &ignored),
                "Cannot restore fixture protection");
    }
    bool protected_page() const {
        MEMORY_BASIC_INFORMATION info{};
        return VirtualQuery(slot, &info, sizeof(info)) == sizeof(info) &&
               info.Protect == PAGE_READONLY;
    }
};
} // namespace

int main() {
    return test::run("native dispatch ownership, protection, callback execution and hook loss", [] {
        Dispatch dispatch;
        const auto original_address = reinterpret_cast<ern::Address>(&original);
        const auto callback_address = reinterpret_cast<ern::Address>(&replacement);
        std::array<std::byte, 16> code_before{}, code_after{};
        require(read(original_address, code_before), "Cannot read the original callback code");
        bool readable = true;
        ern::NativeCallbackHook hook;
        require(hook.error().has_value(), "An uninstalled callback was reported ready");
        require(!hook.install(
                    [&](auto address, auto output) { return readable && read(address, output); },
                    dispatch.address(), original_address, callback_address) &&
                    !hook.error() && dispatch.protected_page(),
                "Callback installation failed or left its page writable");
        unsigned calls{};
        reinterpret_cast<Callback>(*dispatch.slot)(calls);
        require(calls == 11 && read(original_address, code_after) && code_before == code_after,
                "Dispatch did not call the original first or changed its protected code");
        readable = false;
        require(hook.error() && hook.error()->find("Cannot read") != std::string::npos,
                "An unreadable callback stayed available");
        readable = true;
        dispatch.replace(other);
        require(hook.error() && hook.error()->find("replaced") != std::string::npos,
                "A replaced callback stayed available");
        reinterpret_cast<Callback>(*dispatch.slot)(calls);
        require(calls == 111, "Hook health checking overwrote another owner's callback");

        Dispatch concurrent;
        ern::NativeCallbackHook raced;
        const auto race_read = [&](ern::Address address, std::span<std::byte> output) {
            const bool result = read(address, output);
            concurrent.replace(other);
            return result;
        };
        require(
            raced.install(race_read, concurrent.address(), original_address, callback_address) &&
                *concurrent.slot == reinterpret_cast<ern::Address>(&other) &&
                concurrent.protected_page(),
            "Installation overwrote a concurrent owner or lost page protection");
    });
}
