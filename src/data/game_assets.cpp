#include "data/game_assets.hpp"
#include "data/game_files.hpp"
#include <Windows.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <wrl/client.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <set>

#pragma warning(push, 0)
#define BCDEC_IMPLEMENTATION
#include <bcdec.h>
#pragma warning(pop)

namespace ern {
namespace {
using namespace game;
using Microsoft::WRL::ComPtr;

std::unordered_map<std::int32_t, std::string> messages(View data) {
    require(number<std::uint8_t>(data, 2) == 2 && number<std::uint8_t>(data, 8) == 1,
            "Unsupported FMG layout.");
    const auto offsets = number<std::uint64_t>(data, 24);
    slice(data, offsets, 0);
    const auto count = number<std::int32_t>(data, 12);
    require(count >= 0 && count <= 65536, "Invalid FMG group count.");
    std::unordered_map<std::int32_t, std::string> result;
    for (int group = 0; group < count; ++group) {
        const auto entry = slice(data, 40 + std::size_t(group) * 16, 16);
        const auto index = number<std::int32_t>(entry, 0), first = number<std::int32_t>(entry, 4),
                   last = number<std::int32_t>(entry, 8);
        require(index >= 0 && first >= 0 && last >= first && std::int64_t(last) - first < 65536,
                "Invalid FMG group.");
        for (std::int64_t id = first; id <= last; ++id) {
            const auto offset =
                number<std::uint64_t>(data, offsets + 8 * (std::uint64_t(index) + id - first));
            if (!offset)
                continue;
            auto text = game::string(data, offset, true);
            for (auto &ch : text)
                if (ch == '\n' || ch == '\r' || ch == '\t')
                    ch = ' ';
            result[static_cast<std::int32_t>(id)] = std::move(text);
        }
    }
    return result;
}

struct Rect {
    IconKey key;
    unsigned int x, y, width, height;
};
struct ParsedLayout {
    struct Warning {
        IconKey key;
        std::string message;
    };
    std::vector<Rect> rectangles;
    std::vector<Warning> warnings;
};
ParsedLayout layout(View xml, const std::set<IconKey> &wanted) {
    require(xml.size() <= UINT_MAX, "The texture layout is too large.");
    ComPtr<IStream> stream;
    stream.Attach(SHCreateMemStream(xml.data(), static_cast<UINT>(xml.size())));
    ComPtr<IXmlReader> reader;
    require(
        stream &&
            SUCCEEDED(CreateXmlReader(__uuidof(IXmlReader),
                                      reinterpret_cast<void **>(reader.GetAddressOf()), nullptr)) &&
            SUCCEEDED(
                reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit)) &&
            SUCCEEDED(reader->SetInput(stream.Get())),
        "Cannot initialize the texture layout reader.");
    ParsedLayout result;
    XmlNodeType type{};
    HRESULT status;
    while ((status = reader->Read(&type)) == S_OK) {
        if (type != XmlNodeType_Element)
            continue;
        const wchar_t *tag = nullptr;
        require(SUCCEEDED(reader->GetLocalName(&tag, nullptr)), "Invalid texture layout element.");
        if (std::wstring_view(tag) != L"SubTexture")
            continue;
        std::unordered_map<std::string, std::string> attrs;
        if (reader->MoveToFirstAttribute() == S_OK) {
            do {
                const wchar_t *name = nullptr, *value = nullptr;
                require(SUCCEEDED(reader->GetLocalName(&name, nullptr)) &&
                            SUCCEEDED(reader->GetValue(&value, nullptr)),
                        "Invalid texture layout attribute.");
                attrs[utf8(name)] = utf8(value);
            } while (reader->MoveToNextAttribute() == S_OK);
            reader->MoveToElement();
        }
        const auto found = attrs.find("name");
        if (found == attrs.end())
            continue;
        auto name = std::string_view(found->second);
        IconKind kind;
        if (name.starts_with("MENU_ItemIcon_")) {
            kind = IconKind::Item;
            name.remove_prefix(14);
        } else if (name.starts_with("MENU_StatusIcon_")) {
            kind = IconKind::Status;
            name.remove_prefix(16);
        } else
            continue;
        if (const auto extension = name.find_last_of('.'); extension != std::string_view::npos)
            name = name.substr(0, extension);
        std::uint32_t id = 0;
        const auto parsed = std::from_chars(name.data(), name.data() + name.size(), id);
        if (parsed.ec != std::errc{} || parsed.ptr != name.data() + name.size() ||
            !wanted.contains({kind, id}))
            continue;
        const auto integer = [&](const char *attribute) {
            const auto it = attrs.find(attribute);
            require(it != attrs.end(), "A texture rectangle is missing a coordinate.");
            unsigned int value = 0;
            const auto read =
                std::from_chars(it->second.data(), it->second.data() + it->second.size(), value);
            require(read.ec == std::errc{} && read.ptr == it->second.data() + it->second.size(),
                    "Invalid texture rectangle coordinate.");
            return value;
        };
        try {
            result.rectangles.push_back(
                {{kind, id}, integer("x"), integer("y"), integer("width"), integer("height")});
        } catch (const std::exception &error) {
            result.warnings.push_back({{kind, id}, error.what()});
        }
    }
    require(status == S_FALSE, "The texture layout XML is malformed.");
    return result;
}

std::unordered_map<std::string, View> textures(View data) {
    require(starts(data, std::string_view("TPF\0", 4)) && number<std::uint8_t>(data, 12) == 0,
            "Unsupported TPF platform.");
    const auto count = number<std::uint32_t>(data, 8);
    slice(data, 16, std::size_t(count) * 20);
    std::unordered_map<std::string, View> result;
    for (std::size_t i = 0; i < count; ++i) {
        const auto entry = slice(data, 16 + i * 20, 20);
        require(number<std::uint32_t>(entry, 16) == 0, "Unsupported TPF extended header.");
        auto name = game::string(data, number<std::uint32_t>(entry, 12), data[14] == 1);
        require(result
                    .emplace(std::move(name), slice(data, number<std::uint32_t>(entry, 0),
                                                    number<std::uint32_t>(entry, 4)))
                    .second,
                "Duplicate texture name.");
    }
    return result;
}

Bytes resize_icon(View rgba, unsigned int width, unsigned int height) {
    Bytes resized(IconAtlas::icon_size * IconAtlas::icon_size * 4);
    for (unsigned int y = 0; y < IconAtlas::icon_size; ++y)
        for (unsigned int x = 0; x < IconAtlas::icon_size; ++x) {
            const auto sx =
                std::clamp((x + .5) * width / IconAtlas::icon_size - .5, 0., double(width - 1));
            const auto sy =
                std::clamp((y + .5) * height / IconAtlas::icon_size - .5, 0., double(height - 1));
            const auto x0 = static_cast<unsigned int>(sx), y0 = static_cast<unsigned int>(sy);
            const auto x1 = std::min(x0 + 1, width - 1), y1 = std::min(y0 + 1, height - 1);
            for (unsigned int channel = 0; channel < 4; ++channel) {
                const auto pixel = [&](unsigned int px, unsigned int py) {
                    return rgba[(std::size_t(py) * width + px) * 4 + channel];
                };
                const auto a = pixel(x0, y0) * (1 - (sx - x0)) + pixel(x1, y0) * (sx - x0);
                const auto b = pixel(x0, y1) * (1 - (sx - x0)) + pixel(x1, y1) * (sx - x0);
                resized[(y * IconAtlas::icon_size + x) * 4 + channel] =
                    static_cast<std::uint8_t>(std::lround(a * (1 - (sy - y0)) + b * (sy - y0)));
            }
        }
    return resized;
}

Bytes crop(View dds, const Rect &rect) {
    require(starts(dds, "DDS ") && number<std::uint32_t>(dds, 4) == 124, "Unsupported DDS header.");
    const auto width = number<std::uint32_t>(dds, 16), height = number<std::uint32_t>(dds, 12);
    require(width > 0 && height > 0 && width <= 16384 && height <= 16384 && rect.x <= width &&
                rect.y <= height && rect.width > 0 && rect.height > 0 &&
                rect.width <= width - rect.x && rect.height <= height - rect.y,
            "An icon rectangle is outside its DDS image.");
    const auto fourcc = number<std::uint32_t>(dds, 84);
    std::size_t start = 128;
    unsigned int format = 0;
    if (fourcc == 0x30315844) { // DX10
        format = number<std::uint32_t>(dds, 128);
        require(number<std::uint32_t>(dds, 132) == 3 && number<std::uint32_t>(dds, 140) == 1,
                "Unsupported DDS resource dimension.");
        start = 148;
    } else if (fourcc == 0x31545844)
        format = 71; // DXT1
    else if (fourcc == 0x33545844)
        format = 74; // DXT3
    else if (fourcc == 0x35545844)
        format = 77; // DXT5
    const bool raw = format == 28 || format == 29 || format == 87 || format == 91;
    const unsigned int block_size = format == 71 || format == 72 ? 8 : 16;
    require(raw || format == 71 || format == 72 || format == 74 || format == 75 || format == 77 ||
                format == 78 || format == 98 || format == 99,
            "Unsupported DDS icon format. Expected RGBA, BC1, BC2, BC3, or BC7.");
    if (raw)
        slice(dds, start, std::size_t(width) * height * 4);
    else
        slice(dds, start, std::size_t((width + 3) / 4) * ((height + 3) / 4) * block_size);
    Bytes source(std::size_t(rect.width) * rect.height * 4);
    if (raw) {
        for (unsigned int y = 0; y < rect.height; ++y)
            for (unsigned int x = 0; x < rect.width; ++x) {
                const auto at = start + (std::size_t(rect.y + y) * width + rect.x + x) * 4;
                const auto dest = (std::size_t(y) * rect.width + x) * 4;
                std::copy_n(dds.data() + at, 4, source.data() + dest);
                if (format == 87 || format == 91)
                    std::swap(source[dest], source[dest + 2]);
            }
    } else {
        std::array<std::uint8_t, 64> block;
        for (unsigned int by = rect.y / 4; by <= (rect.y + rect.height - 1) / 4; ++by)
            for (unsigned int bx = rect.x / 4; bx <= (rect.x + rect.width - 1) / 4; ++bx) {
                const auto *input =
                    dds.data() + start + (std::size_t(by) * ((width + 3) / 4) + bx) * block_size;
                if (format == 71 || format == 72)
                    bcdec_bc1(input, block.data(), 16);
                else if (format == 74 || format == 75)
                    bcdec_bc2(input, block.data(), 16);
                else if (format == 77 || format == 78)
                    bcdec_bc3(input, block.data(), 16);
                else
                    bcdec_bc7(input, block.data(), 16);
                for (unsigned int y = 0; y < 4; ++y)
                    for (unsigned int x = 0; x < 4; ++x) {
                        const auto px = bx * 4 + x, py = by * 4 + y;
                        if (px >= rect.x && py >= rect.y && px - rect.x < rect.width &&
                            py - rect.y < rect.height)
                            std::copy_n(block.data() + (y * 4 + x) * 4, 4,
                                        source.data() +
                                            (std::size_t(py - rect.y) * rect.width + px - rect.x) *
                                                4);
                    }
            }
    }
    return resize_icon(source, rect.width, rect.height);
}
} // namespace

GameAssets read_game_assets(const std::filesystem::path &directory,
                            std::span<const IconKey> icons) {
    const std::array<std::string, 5> paths{
        "/msg/engus/item_dlc01.msgbnd.dcx", "/menu/01_common_h.sblytbnd.dcx",
        "/menu/01_common_h.tpf.dcx", "/menu/21_common_dlc01_h.sblytbnd.dcx",
        "/menu/21_common_dlc01_h.tpf.dcx"};
    auto files = read_game_files(directory, paths);
    GameAssets result;
    result.warnings = std::move(files.warnings);
    try {
        const auto found = files.files.find(paths[0]);
        if (found != files.files.end()) {
            const auto tables = binder(found->second, directory, [](auto name) {
                return name.starts_with("AntiqueName") || name.starts_with("AttachEffectName");
            });
            std::set<std::string> names;
            for (const auto &[name, bytes] : tables)
                names.insert(name);
            for (const auto &name : names) {
                try {
                    for (auto &[id, text] : messages(tables.at(name))) {
                        if (name.starts_with("AntiqueName"))
                            result.item_names[static_cast<std::uint32_t>(id)] = std::move(text);
                        else
                            result.effect_names[id] = std::move(text);
                    }
                } catch (const std::exception &error) {
                    result.warnings.push_back("Messages: " + name + ": " + error.what());
                }
            }
        }
    } catch (const std::exception &error) {
        result.warnings.push_back(std::string("Messages: ") + error.what());
    }
    const std::set<IconKey> wanted(icons.begin(), icons.end());
    std::map<IconKey, Bytes> crops;
    for (std::size_t index : {std::size_t(1), std::size_t(3)}) {
        try {
            const auto layout_file = files.files.find(paths[index]),
                       texture_file = files.files.find(paths[index + 1]);
            if (layout_file == files.files.end() || texture_file == files.files.end())
                continue;
            const auto layouts = binder(layout_file->second, directory,
                                        [](auto name) { return name.ends_with(".layout"); });
            const auto unpacked = decompress(texture_file->second, directory);
            const auto images = textures(unpacked);
            for (const auto &[name, xml] : layouts) {
                const auto warn_icon = [&](IconKey key, const char *message) {
                    const auto kind = key.kind == IconKind::Item ? "item" : "status";
                    result.warnings.push_back(paths[index] + ": " + name + ", " + kind + " icon " +
                                              std::to_string(key.id) + ": " + message);
                };
                try {
                    const auto parsed = layout(xml, wanted);
                    for (const auto &warning : parsed.warnings)
                        warn_icon(warning.key, warning.message.c_str());
                    if (parsed.rectangles.empty())
                        continue;
                    const auto image = images.find(name.substr(0, name.size() - 7));
                    require(image != images.end(), "The icon layout references a missing texture.");
                    for (const auto &rect : parsed.rectangles) {
                        try {
                            crops[rect.key] = crop(image->second, rect);
                        } catch (const std::exception &error) {
                            warn_icon(rect.key, error.what());
                        }
                    }
                } catch (const std::exception &error) {
                    result.warnings.push_back(paths[index] + ": " + name + ": " + error.what());
                }
            }
        } catch (const std::exception &error) {
            result.warnings.push_back(paths[index] + ": " + error.what());
        }
    }
    require(crops.size() <= 1024, "Too many icon cells for the editor atlas.");
    if (!crops.empty()) {
        result.atlas.width = IconAtlas::columns * IconAtlas::cell_size;
        result.atlas.height = static_cast<unsigned int>((crops.size() + IconAtlas::columns - 1) /
                                                        IconAtlas::columns) *
                              IconAtlas::cell_size;
        result.atlas.rgba.resize(std::size_t(result.atlas.width) * result.atlas.height * 4);
        for (const auto &[key, rgba] : crops) {
            const auto cell = static_cast<unsigned int>(result.cells.size());
            result.cells[key] = cell;
            const auto x = cell % IconAtlas::columns * IconAtlas::cell_size + IconAtlas::gutter;
            const auto y = cell / IconAtlas::columns * IconAtlas::cell_size + IconAtlas::gutter;
            for (unsigned int row = 0; row < IconAtlas::icon_size; ++row)
                std::copy_n(rgba.data() + row * IconAtlas::icon_size * 4, IconAtlas::icon_size * 4,
                            result.atlas.rgba.data() +
                                (std::size_t(y + row) * result.atlas.width + x) * 4);
        }
    }
    return result;
}
} // namespace ern
