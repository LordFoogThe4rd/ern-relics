#include "app/active_regulation.hpp"

namespace ern {
ActiveRegulation::ActiveRegulation(std::filesystem::path requested,
                                   std::filesystem::path game_directory, RegulationRead read)
    : requested_(std::move(requested)),
      game_directory_(game_directory.empty() ? requested_.parent_path()
                                             : std::move(game_directory)),
      read_(std::move(read)) {
    diagnostics_ = {requested_, {}, {}, "Waiting for the active regulation.bin."};
}

ActiveRegulation::~ActiveRegulation() {
    if (worker_.joinable()) {
        worker_.request_stop();
        wake_.notify_all();
        worker_.join();
    }
}

void ActiveRegulation::read_and_publish() {
    // Only file readers take this lock. Polling consumes latest_ without waiting for I/O.
    std::lock_guard lock(read_mutex_);
    auto next = std::make_shared<Observation>();
    if (const auto previous = latest_.load()) {
        next->rules = previous->rules;
        next->source_hash = previous->source_hash;
    }
    next->diagnostics.requested_path = requested_;
    try {
        auto file = read_(requested_);
        if (const auto *error = std::get_if<FileError>(&file)) {
            next->diagnostics.error = "Cannot read the active regulation.bin. " + error->message;
        } else {
            const auto &content = std::get<FileContent>(file);
            next->diagnostics.resolved_path = content.resolved_path;
            next->diagnostics.detected_hash = content.sha256;
            if (!next->rules || next->source_hash != content.sha256) {
                auto parsed = read_regulation(content.bytes, game_directory_);
                if (const auto *parse_error = std::get_if<FileError>(&parsed)) {
                    next->diagnostics.error =
                        "Cannot load relic rules from the active regulation.bin. " +
                        parse_error->message;
                } else {
                    next->rules = std::make_shared<const RelicValidator>(
                        std::get<RegulationRules>(std::move(parsed)));
                    next->source_hash = content.sha256;
                }
            }
        }
    } catch (const std::exception &error) {
        next->diagnostics.error =
            "Cannot check the active regulation.bin. " + std::string(error.what());
    }
    latest_.store(std::move(next));
}

void ActiveRegulation::start(std::chrono::milliseconds interval) {
    if (worker_.joinable())
        return;
    worker_ = std::jthread([this, interval](std::stop_token stop) {
        std::unique_lock wait(wait_mutex_);
        while (!wake_.wait_for(wait, stop, interval, [] { return false; })) {
            if (stop.stop_requested())
                break;
            read_and_publish();
        }
    });
}

void ActiveRegulation::verify() {
    read_and_publish();
    poll();
}

void ActiveRegulation::poll() {
    const auto observation = latest_.load();
    if (!observation)
        return;
    diagnostics_ = observation->diagnostics;
    if (locked_hash_ && !diagnostics_.detected_hash.empty() &&
        diagnostics_.detected_hash != *locked_hash_) {
        diagnostics_.error =
            "The active regulation changed after the character loaded. Restart Nightreign to use the new rules.";
    }
    if (diagnostics_.error.empty() && observation->rules) {
        rules_ = observation->rules;
        source_hash_ = observation->source_hash;
    }
}

void ActiveRegulation::lock_source() {
    if (!source_hash_.empty() && diagnostics_.error.empty())
        locked_hash_ = source_hash_;
}

RuleContext ActiveRegulation::context(DlcState dlc) const {
    if (!diagnostics_.error.empty())
        return UnknownRules{diagnostics_.error};
    if (source_hash_.empty())
        return UnknownRules{"Relic roll data is unavailable."};
    return dlc;
}
} // namespace ern
