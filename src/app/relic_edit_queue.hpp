#pragma once

#include "game/relic_equipment.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ern {
using EditAvailability = std::function<std::optional<std::string>()>;
using ReadEquipContext = std::function<RelicEquipReadResult(RelicRoots)>;
class NativeEditDiagnostics;
// One owned edit crosses from the application worker to a native update callback.
class RelicEditQueue {
  public:
    EditResult submit(RelicEditRequest request,
                      std::chrono::milliseconds timeout = std::chrono::seconds(2),
                      const EditAvailability &availability = {});
    EditResult submit_native(RelicEditRequest request, const ReadEquipContext &read_context,
                             const EditAvailability &callback_error,
                             NativeEditDiagnostics &diagnostics,
                             std::chrono::milliseconds timeout = std::chrono::seconds(2));
    std::optional<RelicRoots> pending_roots() const;
    void process(const RelicCommit &commit);

  private:
    struct Waiting {};
    struct Executing {};
    struct Message {
        RelicEditRequest request;
        std::variant<Waiting, Executing, EditResult> state = Waiting{};
    };
    mutable std::mutex mutex_;
    std::condition_variable changed_;
    std::shared_ptr<Message> current_;
};
} // namespace ern
