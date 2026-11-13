#include "relics/preset.hpp"
#include "relics/effect_encoding.hpp"
#include <charconv>
#include <algorithm>

namespace ern {
PresetStage stage_preset(const Preset &preset, const EquippedRelics &equipped,
                         const RelicValidator &validator, const RuleContext &context,
                         bool invalid) {
    PresetStage result{};
    for (std::size_t i = 0; i < preset.slots.size(); ++i) {
        if (!preset.slots[i]) {
            result.skipped[i] = "No effect template in this preset slot.";
            continue;
        }
        if (!equipped.slots[i]) {
            result.skipped[i] = "No relic equipped.";
            continue;
        }
        const auto item_id = equipped.slots[i]->item_id;
        if (const auto *reason = relic_lock_reason(validator.items().find(item_id))) {
            result.skipped[i] = reason;
            continue;
        }
        if (invalid) {
            result.drafts.slots[i] = *preset.slots[i];
            continue;
        }
        const auto validation = validator.validate(item_id, *preset.slots[i], context);
        if (const auto *valid = std::get_if<Obtainable>(&validation))
            result.drafts.slots[i] = valid->effects;
        else
            result.skipped[i] = validation_reason(validation);
    }
    return result;
}
Preset capture_preset(const EquippedRelics &equipped, const RelicValidator &validator,
                      const RuleContext &context, bool invalid) {
    Preset current{};
    for (std::size_t i = 0; i < current.slots.size(); ++i)
        if (equipped.slots[i])
            current.slots[i] = equipped.slots[i]->effects;
    return stage_preset(current, equipped, validator, context, invalid).drafts;
}
std::string serialize_preset(const Preset &preset) {
    std::string text = "{\"effects\":[";
    for (std::size_t i = 0; i < preset.slots.size(); ++i) {
        if (i)
            text += ',';
        if (!preset.slots[i]) {
            text += "null";
            continue;
        }
        text += '[';
        const auto fields = encode_effects(*preset.slots[i]);
        for (std::size_t j = 0; j < fields.size(); ++j) {
            if (j)
                text += ',';
            text += std::to_string(fields[j]);
        }
        text += ']';
    }
    return text + "]}\n";
}

std::optional<Preset> parse_preset(std::string_view text) {
    const auto consume = [&text](std::string_view token) {
        const auto first = text.find_first_not_of(" \t\r\n");
        if (first == std::string_view::npos)
            return false;
        text.remove_prefix(first);
        if (!text.starts_with(token))
            return false;
        text.remove_prefix(token.size());
        return true;
    };
    if (!consume("{") || !consume("\"effects\"") || !consume(":") || !consume("["))
        return std::nullopt;
    Preset result{};
    for (std::size_t i = 0; i < result.slots.size(); ++i) {
        if (i && !consume(","))
            return std::nullopt;
        if (consume("null"))
            continue;
        if (!consume("["))
            return std::nullopt;
        EncodedEffects fields;
        for (std::size_t j = 0; j < fields.size(); ++j) {
            if (j && !consume(","))
                return std::nullopt;
            const auto first = text.find_first_not_of(" \t\r\n");
            if (first == std::string_view::npos)
                return std::nullopt;
            text.remove_prefix(first);
            if (text.size() > 1 && text[0] == '0' && text[1] >= '0' && text[1] <= '9')
                return std::nullopt;
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), fields[j]);
            if (parsed.ec != std::errc{} || parsed.ptr == text.data())
                return std::nullopt;
            text.remove_prefix(static_cast<std::size_t>(parsed.ptr - text.data()));
        }
        if (!consume("]"))
            return std::nullopt;
        if (std::any_of(fields.begin(), fields.end(), [](auto id) { return id != UINT32_MAX; }))
            result.slots[i] = decode_effects(fields);
    }
    if (!consume("]") || !consume("}") ||
        text.find_first_not_of(" \t\r\n") != std::string_view::npos)
        return std::nullopt;
    return result;
}

bool valid_preset_name(std::string_view text) {
    if (text.empty() || text.size() > 32 || text.front() == ' ' || text.back() == ' ' ||
        !std::all_of(text.begin(), text.end(), [](char ch) {
            return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                   (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == ' ';
        }))
        return false;
    // Windows device stems stay reserved when the preset's .json extension is appended.
    // https://learn.microsoft.com/windows/win32/fileio/naming-a-file
    std::string stem(text);
    std::transform(stem.begin(), stem.end(), stem.begin(), [](char ch) {
        return ch >= 'a' && ch <= 'z' ? static_cast<char>(ch - ('a' - 'A')) : ch;
    });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL")
        return false;
    return !(stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
             stem.back() >= '1' && stem.back() <= '9');
}
} // namespace ern
