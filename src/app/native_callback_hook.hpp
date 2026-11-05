#pragma once

#include "game/memory.hpp"
#include <string>

namespace ern {
// Replaces one verified dispatch pointer. The callback and binding stay pinned until exit.
class NativeCallbackHook {
  public:
    std::optional<std::string> install(MemoryRead read, Address slot, Address original,
                                       Address callback);
    std::optional<std::string> error() const;

  private:
    struct Binding {
        MemoryRead read;
        Address slot;
        Address callback;
    };
    std::optional<Binding> binding_;
};
} // namespace ern
