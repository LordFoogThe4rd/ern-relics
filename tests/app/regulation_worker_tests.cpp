#include "app/active_regulation.hpp"
#include "support/check.hpp"
#include <future>
#include <algorithm>

int main() {
    return test::run("nonblocking regulation observations, fresh checks and worker shutdown", [] {
        using namespace std::chrono_literals;
        std::mutex gate;
        std::condition_variable changed;
        unsigned int reads = 0, concurrent = 0, maximum_concurrent = 0;
        bool release = false;
        ern::ActiveRegulation active{
            "unused.bin",
            {},
            [&](const std::filesystem::path &) -> std::variant<ern::FileContent, ern::FileError> {
                std::unique_lock lock(gate);
                const auto number = ++reads;
                maximum_concurrent = std::max(maximum_concurrent, ++concurrent);
                changed.notify_all();
                if (number == 3)
                    changed.wait(lock, [&] { return release; });
                --concurrent;
                return ern::FileError{"Read " + std::to_string(number)};
            }};
        active.verify();
        for (int i = 0; i < 100; ++i)
            active.poll();
        test::require(reads == 1, "Rendering polls performed file I/O");
        active.verify();
        test::require(reads == 2, "A fresh check reused a cached file observation");
        active.start(1ms);
        bool background_started = false;
        {
            std::unique_lock lock(gate);
            background_started = changed.wait_for(lock, 2s, [&] { return reads == 3; });
            if (!background_started)
                release = true;
        }
        changed.notify_all();
        test::require(background_started, "Background file check did not start");
        auto poll = std::async(std::launch::async, [&] { active.poll(); }).share();
        const bool nonblocking = poll.wait_for(1s) == std::future_status::ready;
        std::promise<void> verifying;
        auto verification_started = verifying.get_future();
        auto fresh = std::async(std::launch::async, [&, poll] {
            poll.wait();
            verifying.set_value();
            active.verify();
        });
        const bool started = verification_started.wait_for(2s) == std::future_status::ready;
        // Keep the reader blocked until the foreground call has had a chance to contend.
        const bool serialized = started && fresh.wait_for(1s) == std::future_status::timeout;
        {
            std::lock_guard lock(gate);
            release = true;
        }
        changed.notify_all();
        poll.get();
        fresh.get();
        test::require(nonblocking, "A rendering poll waited for a blocked file read");
        test::require(started, "The foreground file check did not start");
        test::require(serialized, "A fresh check completed while a background read was blocked");
        {
            std::lock_guard lock(gate);
            test::require(maximum_concurrent == 1, "A fresh check raced with a background reader");
        }
        test::require(active.diagnostics().error.find("Read 2") == std::string::npos,
                      "Fresh checks reused a stale observation");
        // Destruction requests stop and wakes the timed wait, including a long poll interval.
        ern::ActiveRegulation stopped{
            "unused.bin",
            {},
            [](const std::filesystem::path &) -> std::variant<ern::FileContent, ern::FileError> {
                throw std::runtime_error("Injected reader exception");
            }};
        stopped.verify();
        test::require(stopped.diagnostics().error.find("Injected reader exception") !=
                          std::string::npos,
                      "Reader exceptions escaped the source boundary");
        stopped.start(1h);
    });
}
