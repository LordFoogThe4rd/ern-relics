#pragma once

#include "data/regulation.hpp"
#include "relics/relic_validator.hpp"
#include "app/inspection.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace ern {
using RegulationRead =
    std::function<std::variant<FileContent, FileError>(const std::filesystem::path &)>;

class ActiveRegulation {
  public:
    ActiveRegulation(std::filesystem::path requested, std::filesystem::path game_directory = {},
                     RegulationRead read = read_file);
    ~ActiveRegulation();
    void start(std::chrono::milliseconds interval = std::chrono::milliseconds(500));
    // Poll only consumes an immutable observation. Verify serializes with the file worker.
    void poll();
    void verify();
    void lock_source();
    bool locked() const { return locked_hash_.has_value(); }
    const RelicValidator &validator() const { return *rules_; }
    std::shared_ptr<const RelicValidator> rules() const { return rules_; }
    const std::string &source_hash() const { return source_hash_; }
    const RegulationDiagnostics &diagnostics() const { return diagnostics_; }
    RuleContext context(DlcState dlc) const;

  private:
    struct Observation {
        std::shared_ptr<const RelicValidator> rules;
        std::string source_hash;
        RegulationDiagnostics diagnostics;
    };
    void read_and_publish();
    const std::filesystem::path requested_, game_directory_;
    RegulationRead read_;
    std::atomic<std::shared_ptr<const Observation>> latest_;
    std::mutex read_mutex_;
    std::mutex wait_mutex_;
    std::condition_variable_any wake_;
    std::shared_ptr<const RelicValidator> rules_ = std::make_shared<const RelicValidator>();
    std::string source_hash_;
    RegulationDiagnostics diagnostics_;
    std::optional<std::string> locked_hash_;
    std::jthread worker_;
};
} // namespace ern
