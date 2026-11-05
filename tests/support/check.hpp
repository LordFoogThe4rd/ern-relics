#pragma once
#include <iostream>
#include <stdexcept>

namespace test {
inline void require(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
template <typename Check> int run(const char *name, Check check) {
    try {
        check();
        std::cout << "PASS: " << name << '\n';
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
} // namespace test
