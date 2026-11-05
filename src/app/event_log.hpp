#pragma once
#include <chrono>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <ctime>
#include <vector>

namespace ern {
class EventLog {
  public:
    void set_path(const std::filesystem::path &path);
    void enqueue(std::string_view message);
    // Only the application worker, or bootstrap before it starts, writes the file.
    void flush();

  private:
    struct Entry {
        std::time_t time;
        std::string message;
    };
    std::mutex mutex_;
    std::vector<Entry> pending_;
    std::filesystem::path path_;
};
} // namespace ern
