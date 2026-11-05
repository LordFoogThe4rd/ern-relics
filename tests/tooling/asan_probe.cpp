#include <cstddef>

int main(int argc, char **) {
    auto *bytes = new unsigned char[1];
    // Keep the deliberate overflow observable in optimized sanitizer builds.
    volatile int index = argc;
    bytes[static_cast<std::size_t>(index)] = 42;
    volatile unsigned char value = bytes[static_cast<std::size_t>(index)];
    delete[] bytes;
    return value == 42 ? 0 : 1;
}
