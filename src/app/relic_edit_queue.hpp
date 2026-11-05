#pragma once

#include "game/relic_writer.hpp"
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace ern {
using EditAvailability = std::function<std::optional<std::string>()>;
// One owned edit crosses from the application worker to a native update callback.
class RelicEditQueue {
  public:
    EditResult submit(RelicEditRequest request,
                      std::chrono::milliseconds timeout = std::chrono::seconds(2),
                      const EditAvailability &availability = {});
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
