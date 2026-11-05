#include "app/event_log.hpp"
#include "data/rotating_file.hpp"
#include <iomanip>
#include <sstream>

namespace ern {
void EventLog::set_path(const std::filesystem::path &path) { path_ = path; }
void EventLog::enqueue(std::string_view message) {
    Entry entry{std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()),
                std::string(message)};
    std::lock_guard lock(mutex_);
    pending_.push_back(std::move(entry));
}
void EventLog::flush() {
    std::vector<Entry> entries;
    {
        std::lock_guard lock(mutex_);
        entries.swap(pending_);
    }
    for (const auto &entry : entries) {
        std::tm local{};
        localtime_s(&local, &entry.time);
        std::ostringstream record;
        record << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << ' ' << entry.message << '\n';
        append_rotating_file(path_, record.str());
    }
}
} // namespace ern
