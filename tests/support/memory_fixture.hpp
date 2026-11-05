#pragma once
#include "game/relic_reader.hpp"
#include "relics/types.hpp"
#include "relics/effect_encoding.hpp"
#include <cstring>
#include <vector>

namespace test {
struct Fixture {
    static constexpr ern::Address base = 0x10000;
    std::vector<std::byte> bytes = std::vector<std::byte>(0x30000);
    ern::RelicRoots roots{base, base + 8, base + 16};
    template <typename T> void put(ern::Address address, const T &value) {
        std::memcpy(bytes.data() + address - base, &value, sizeof(value));
    }
    void put(ern::Address address, const ern::Effects &effects) {
        const auto fields = ern::encode_effects(effects);
        std::memcpy(bytes.data() + address - base, fields.data(), 12);
        std::memcpy(bytes.data() + address - base + 0x28, fields.data() + 3, 12);
    }
    bool read(ern::Address address, std::span<std::byte> output) const {
        if (address < base || address - base > bytes.size() ||
            output.size() > bytes.size() - (address - base))
            return false;
        std::memcpy(output.data(), bytes.data() + address - base, output.size());
        return true;
    }
    Fixture() {
        put(base, ern::Address(base + 0x100));
        put(base + 8, ern::Address(base + 0x300));
        put(base + 16, ern::Address(base + 0x8000));
        put(base + 0x8000 + 0x174E8, ern::Address(base + 0x5000));
        put(base + 0x308, ern::Address(base + 0x1000));
        for (std::size_t i = 3; i < ern::relic_slot_count; ++i)
            put(base + 0x1000 + 0x2F4 + i * 4, UINT16_MAX);
        for (std::uint16_t i = 0; i < 3; ++i) {
            put(base + 0x1000 + 0x2F4 + i * 4, std::uint16_t(i + 1));
            const ern::Address entry = base + 0x3000 + i * 0x100;
            put(base + 0x100 + 8 + (i + 1) * 8, entry);
            put(entry + 0x0C, std::uint32_t(0x80000000u | (102u + i * 3u)));
            put(entry + 0x18,
                ern::Effects{{7000401, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, UINT32_MAX}});
        }
    }
    void equip_deep(std::size_t slot = 3) {
        const auto index = static_cast<std::uint16_t>(slot + 1);
        const auto entry = base + 0x3000 + slot * 0x100;
        put(base + 0x1000 + 0x2F4 + slot * 4, index);
        put(base + 0x100 + 8 + index * 8, entry);
        put(entry + 0x0C, std::uint32_t(0x80000000u | 2000000u));
        put(entry + 0x18,
            ern::Effects{{7000401, UINT32_MAX}, {7000602, UINT32_MAX}, {7001802, 900001}});
    }
    ern::MemoryRead reader() {
        return [this](auto address, auto output) { return read(address, output); };
    }
};
} // namespace test
