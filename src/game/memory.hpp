#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>

namespace ern {
using Address = std::uintptr_t;
using MemoryRead = std::function<bool(Address, std::span<std::byte>)>;
using MemoryWrite = std::function<bool(Address, std::span<const std::byte>)>;

template <typename T> std::optional<T> read_value(const MemoryRead &read, Address address) {
    T value{};
    if (!read(address, std::as_writable_bytes(std::span(&value, 1))))
        return std::nullopt;
    return value;
}

// A readable zero is a value. Only pointer reads reject a null address.
inline std::optional<Address> read_pointer(const MemoryRead &read, Address address) {
    const auto value = read_value<Address>(read, address);
    return value && *value ? value : std::nullopt;
}
} // namespace ern
