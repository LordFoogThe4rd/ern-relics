#include "app/event_log.hpp"
#include "data/rotating_file.hpp"
#include "support/check.hpp"
#include <fstream>
#include <iterator>

namespace {
std::string read_file(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    test::require(bool(file), "Could not read the event log");
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
} // namespace

int main() {
    return test::run("event log rotates between queued records and across sessions", [] {
        const auto directory =
            std::filesystem::temp_directory_path() /
            ("event-log-test-" +
             std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(directory);
        const auto path = directory / "ern-relics.log";
        const auto previous = directory / "ern-relics.log.1";
        // Leave exactly enough room for the timestamp, separator, "first", and newline.
        const auto prefix = std::string(ern::rotating_file_limit - 27, 'X') + '\n';
        {
            std::ofstream file(path, std::ios::binary);
            file << prefix;
            test::require(bool(file), "Could not prepare the log size boundary");
        }
        {
            ern::EventLog log;
            log.set_path(path);
            log.enqueue("first");
            log.enqueue("second");
            log.flush();
            log.flush();
        }
        const auto archived = read_file(previous);
        const auto current = read_file(path);
        test::require(archived.size() == ern::rotating_file_limit && archived.starts_with(prefix) &&
                          archived.ends_with(" first\n") && current.size() == 27 &&
                          current.ends_with(" second\n"),
                      "A flush split a record, exceeded the limit, or duplicated queued events");
        {
            ern::EventLog log;
            log.set_path(path);
            log.enqueue("third");
            log.flush();
        }
        test::require(read_file(path).starts_with(current) &&
                          read_file(path).ends_with(" third\n") && read_file(previous) == archived,
                      "A new log session truncated history or rotated a small file");
        for (const auto &file : {path, previous})
            std::filesystem::remove(file);
        std::filesystem::remove(directory);
    });
}
