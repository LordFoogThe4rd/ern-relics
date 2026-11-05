#pragma once

#include "game/relic_equipment.hpp"
#include <atomic>
#include <mutex>
#include <string>

namespace ern {
// One trace belongs to one submit. Late callbacks retain that trace, never the next edit's.
class NativeEditDiagnostics {
  public:
    void entered() { ++entered_; }
    void returned() { ++returned_; }
    void observe(Address character, const RelicEquipReadResult &observed);
    void reject(std::string_view stage, RelicEquipFailure failure);
    std::string summary() const;

  private:
    struct Rejection {
        std::string_view stage;
        RelicEquipFailure failure;
    };
    void remember(Rejection rejection);
    std::atomic<std::uint64_t> entered_{}, returned_{};
    mutable std::mutex mutex_;
    std::uint64_t checks_{}, invalid_{}, other_{}, matched_{};
    std::optional<Rejection> first_, last_;
};
} // namespace ern
