#include "app/bootstrap.hpp"
#include "app/host_protocol.hpp"
#include "app/editor_application.hpp"
#include "app/event_log.hpp"
#include "app/native_relic_equip.hpp"
#include "app/process_guard.hpp"
#include "game/pattern.hpp"
#include <charconv>
#include <sstream>

namespace ern {
namespace {
std::atomic<bool> fixture_attached = false;
std::filesystem::path module_path(HMODULE module) {
    std::wstring path(32768, L'\0');
    const auto size = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    path.resize(size);
    return path;
}

std::string file_version(const std::filesystem::path &path) {
    DWORD ignored = 0;
    const auto size = GetFileVersionInfoSizeW(path.c_str(), &ignored);
    std::vector<std::byte> data(size);
    if (size == 0 || !GetFileVersionInfoW(path.c_str(), 0, size, data.data()))
        return "Unknown";
    VS_FIXEDFILEINFO *info = nullptr;
    UINT info_size = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void **>(&info), &info_size) ||
        info_size < sizeof(*info))
        return "Unknown";
    return std::to_string(HIWORD(info->dwFileVersionMS)) + "." +
           std::to_string(LOWORD(info->dwFileVersionMS)) + "." +
           std::to_string(HIWORD(info->dwFileVersionLS)) + "." +
           std::to_string(LOWORD(info->dwFileVersionLS));
}

std::optional<std::uint32_t> ini_number(const wchar_t *section, const wchar_t *key,
                                        const std::filesystem::path &path) {
    wchar_t buffer[64]{};
    GetPrivateProfileStringW(section, key, L"", buffer, 64, path.c_str());
    std::string text;
    for (const auto ch : std::wstring_view(buffer)) {
        if (ch > 127)
            return std::nullopt;
        text.push_back(static_cast<char>(ch));
    }
    auto view = std::string_view(text);
    const int base = view.starts_with("0x") || view.starts_with("0X") ? 16 : 10;
    if (base == 16)
        view.remove_prefix(2);
    std::uint32_t value = 0;
    const auto result = std::from_chars(view.data(), view.data() + view.size(), value, base);
    if (view.empty() || result.ec != std::errc{} || result.ptr != view.data() + view.size())
        return std::nullopt;
    return value;
}

DlcState installed_dlc() {
    const auto steam = GetModuleHandleW(L"steam_api64.dll");
    if (!steam)
        return DlcState::Unknown;
    const auto apps =
        reinterpret_cast<void *(*)()>(GetProcAddress(steam, "SteamAPI_SteamApps_v008"));
    const auto installed = reinterpret_cast<bool (*)(void *, std::uint32_t)>(
        GetProcAddress(steam, "SteamAPI_ISteamApps_BIsDlcInstalled"));
    if (!apps || !installed)
        return DlcState::Unknown;
    auto *api = apps();
    if (!api)
        return DlcState::Unknown;
    // The Forsaken Hollows. Read the already initialized Steam interface only.
    return installed(api, game_layout::forsaken_hollows_app) ? DlcState::Present : DlcState::Absent;
}
bool read_live_memory(Address address, std::span<std::byte> destination) {
    if (address < 0x10000 || destination.empty() || address > UINTPTR_MAX - destination.size())
        return false;
    SIZE_T copied = 0;
    return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<const void *>(address),
                             destination.data(), destination.size(), &copied) &&
           copied == destination.size();
}

bool write_live_memory(Address address, std::span<const std::byte> bytes) {
    SIZE_T copied = 0;
    return WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<void *>(address), bytes.data(),
                              bytes.size(), &copied) &&
           copied == bytes.size();
}

std::variant<RelicRoots, std::string> locate_game_roots() {
    const auto base = reinterpret_cast<Address>(GetModuleHandleW(nullptr));
    IMAGE_DOS_HEADER dos{};
    if (!read_live_memory(base, std::as_writable_bytes(std::span(&dos, 1))) ||
        dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew <= 0)
        return std::string{"Cannot inspect the executable for relic signatures."};
    IMAGE_NT_HEADERS64 nt{};
    if (!read_live_memory(base + dos.e_lfanew, std::as_writable_bytes(std::span(&nt, 1))) ||
        nt.Signature != IMAGE_NT_SIGNATURE || nt.FileHeader.NumberOfSections > 96)
        return std::string{"Cannot inspect the executable for relic signatures."};
    const auto section_table = base + dos.e_lfanew + sizeof(DWORD) + sizeof(IMAGE_FILE_HEADER) +
                               nt.FileHeader.SizeOfOptionalHeader;
    for (unsigned int i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        if (!read_live_memory(section_table + i * sizeof(section),
                              std::as_writable_bytes(std::span(&section, 1))))
            return std::string{"Cannot inspect the executable for relic signatures."};
        if (std::string_view(reinterpret_cast<char *>(section.Name), 5) != ".text")
            continue;
        if (section.Misc.VirtualSize == 0 ||
            section.Misc.VirtualSize > nt.OptionalHeader.SizeOfImage ||
            section.VirtualAddress > nt.OptionalHeader.SizeOfImage - section.Misc.VirtualSize)
            return std::string{"Cannot inspect the executable for relic signatures."};
        std::vector<std::byte> code(section.Misc.VirtualSize);
        const auto section_address = base + section.VirtualAddress;
        if (!read_live_memory(section_address, code))
            return std::string{"Cannot inspect the executable for relic signatures."};
        const auto item_pattern = parse_pattern(game_layout::item_signature);
        const auto game_pattern = parse_pattern(game_layout::game_signature);
        const auto world_pattern = parse_pattern(game_layout::world_signature);
        const auto item_global = find_unique_rip_target(code, section_address, *item_pattern,
                                                        game_layout::item_displacement);
        const auto game_global = find_unique_rip_target(code, section_address, *game_pattern,
                                                        game_layout::game_displacement);
        auto world_global = find_unique_rip_target(code, section_address, *world_pattern,
                                                   game_layout::world_displacement);
        if (!item_global || !game_global) {
            return std::string{
                "Relic signatures are missing or ambiguous. This game build is not supported yet."};
        }
        if (*item_global < base || *game_global < base ||
            *item_global - base > nt.OptionalHeader.SizeOfImage - sizeof(Address) ||
            *game_global - base > nt.OptionalHeader.SizeOfImage - sizeof(Address))
            return std::string{"Cannot inspect the executable for relic signatures."};
        if (world_global &&
            (*world_global < base ||
             *world_global - base > nt.OptionalHeader.SizeOfImage - sizeof(Address)))
            world_global.reset();
        const auto roots = RelicRoots{*item_global, *game_global, world_global.value_or(0)};
        if (!world_global)
            application().log(
                "WorldChrMan signature unavailable. Roundtable-only editing is locked.");
        std::ostringstream message;
        message << "Relic signatures found. Globals at 0x" << std::hex << *item_global - base
                << " and 0x" << *game_global - base;
        application().log(message.str());
        return roots;
    }
    return std::string{"Cannot inspect the executable for relic signatures."};
}

bool startup_failure(const std::filesystem::path &directory, const char *message) {
    EventLog log;
    log.set_path(directory / "ern-relics.log");
    log.enqueue(std::string("ERN Relics " ERN_VERSION ": ") + message);
    log.flush();
    return false;
}
} // namespace

bool test_host_attached() { return fixture_attached.load(); }

bool bootstrap_application(HMODULE module) {
    RuntimeEnvironment environment;
    environment.directory = module_path(module).parent_path();
    const auto executable = module_path(nullptr);
    const bool host = _wcsicmp(executable.filename().c_str(), L"ern-relics-host.exe") == 0;
    if (!host && _wcsicmp(executable.filename().c_str(), L"nightreign.exe") != 0)
        return startup_failure(environment.directory,
                               "Unsupported executable. Initialization stopped.");
    if (!host && eac_running_or_unavailable())
        return startup_failure(
            environment.directory,
            "EAC is running, or process enumeration failed. Initialization stopped.");
    const auto ini = environment.directory / "ern-relics.ini";
    if (const auto key = ini_number(L"overlay", L"hotkey", ini);
        key && *key >= VK_F1 && *key <= VK_F24)
        environment.hotkey = static_cast<int>(*key);
    wchar_t open[16]{};
    GetPrivateProfileStringW(L"overlay", L"open_on_start", L"false", open, 16, ini.c_str());
    environment.open_on_start = host || _wcsicmp(open, L"false") != 0;
    if (const auto offset = ini_number(L"reader", L"equipped_offset", ini);
        offset && *offset >= 0x100 && *offset <= 0x2000 && *offset % 4 == 0)
        environment.equipped_offset = *offset;
    environment.executable_version = host ? "DX12 test host" : file_version(executable);
    environment.layout_verified =
        (host || environment.executable_version == game_layout::executable_version) &&
        environment.equipped_offset == game_layout::equipped_relics;
    environment.read = read_live_memory;
    environment.write = write_live_memory;
    if (host) {
        const auto get_fixture = reinterpret_cast<const HostFixture *(*)()>(
            GetProcAddress(GetModuleHandleW(nullptr), "ern_host_fixture"));
        const auto *fixture = get_fixture ? get_fixture() : nullptr;
        if (!fixture || fixture->magic != fixture_magic || !fixture->rules || !fixture->read ||
            !fixture->write)
            return startup_failure(environment.directory,
                                   "The test host did not provide synthetic rules.");
        environment.rules = *fixture->rules;
        environment.read = fixture->read;
        environment.write = fixture->write;
        if (fixture->assets)
            environment.assets = *fixture->assets;
        environment.locate_roots = [roots = fixture->roots] {
            return std::variant<RelicRoots, std::string>{roots};
        };
        environment.dlc = [] { return DlcState::Unknown; };
        environment.journal_name = "fixture-edits.jsonl";
        environment.source_label = "Synthetic test rules";
        wchar_t game[32768]{}, regulation[32768]{};
        const auto game_size =
            GetEnvironmentVariableW(L"ERN_RELICS_TEST_GAME_DIRECTORY", game, 32768);
        if (game_size > 0 && game_size < 32768)
            environment.game_directory = game;
        const auto rule_size =
            GetEnvironmentVariableW(L"ERN_RELICS_TEST_REGULATION", regulation, 32768);
        if (rule_size > 0 && rule_size < 32768) {
            environment.rules = FileRuleSource{regulation};
            environment.source_label = "Active regulation.bin";
        }
    } else {
        environment.game_directory = executable.parent_path();
        environment.rules = FileRuleSource{environment.game_directory / "regulation.bin"};
        environment.locate_roots = locate_game_roots;
        environment.dlc = installed_dlc;
        if (environment.layout_verified) {
            if (const auto error = install_native_relic_equip(environment.read, environment.write))
                environment.edit_lock = *error;
            environment.commit = commit_native_relic_edit;
            environment.native_edit_error = native_relic_equip_error;
        }
    }
    const bool native_refresh = !host && environment.layout_verified;
    const auto refresh_error = environment.edit_lock;
    application().initialize(std::move(environment));
    if (native_refresh)
        application().log(
            refresh_error.empty()
                ? "Native relic equipment callback installed through PlayerIns dispatch; game code unchanged."
                : refresh_error);
    fixture_attached.store(host);
    return true;
}
} // namespace ern
