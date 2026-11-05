#include "data/fingerprint.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <array>
#include <algorithm>
#include <system_error>

namespace ern {
namespace {
FileError file_error(const char *operation, DWORD code = GetLastError()) {
    return {std::string(operation) + ": " + std::system_category().message(static_cast<int>(code)) +
            " (Windows error " + std::to_string(code) + ")."};
}

std::optional<std::string> hash_bytes(const std::vector<std::uint8_t> &bytes) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return std::nullopt;
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) < 0) {
        BCryptCloseAlgorithmProvider(algorithm, 0);
        return std::nullopt;
    }
    std::array<unsigned char, 32> digest{};
    const bool success =
        BCryptHashData(hash, const_cast<PUCHAR>(bytes.data()), static_cast<ULONG>(bytes.size()),
                       0) >= 0 &&
        BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!success)
        return std::nullopt;
    constexpr char digits[] = "0123456789abcdef";
    std::string result;
    for (const auto byte : digest) {
        result += digits[byte >> 4];
        result += digits[byte & 15];
    }
    return result;
}
} // namespace

std::variant<FileContent, FileError> read_file(const std::filesystem::path &path) {
    struct Handle {
        HANDLE value;
        ~Handle() {
            if (value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    } file{CreateFileW(path.c_str(), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, nullptr)};
    if (file.value == INVALID_HANDLE_VALUE)
        return file_error("Cannot open file");
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (!GetFileInformationByHandle(file.value, &before))
        return file_error("Cannot inspect file");
    constexpr std::uint32_t maximum_size = 128 * 1024 * 1024;
    if (before.nFileSizeHigh || before.nFileSizeLow > maximum_size ||
        (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        return FileError{"Expected a file no larger than 128 MiB."};
    std::wstring resolved(32768, L'\0');
    const auto length = GetFinalPathNameByHandleW(
        file.value, resolved.data(), static_cast<DWORD>(resolved.size()), FILE_NAME_NORMALIZED);
    if (!length || length >= resolved.size())
        return file_error("Cannot resolve opened file path");
    resolved.resize(length);
    FileContent content{
        std::filesystem::path(resolved), {}, std::vector<std::uint8_t>(before.nFileSizeLow)};
    std::size_t offset = 0;
    while (offset < content.bytes.size()) {
        DWORD read = 0;
        const auto count =
            static_cast<DWORD>(std::min<std::size_t>(65536, content.bytes.size() - offset));
        if (!ReadFile(file.value, content.bytes.data() + offset, count, &read, nullptr))
            return file_error("Cannot read file");
        if (!read)
            return FileError{"The file ended before the expected size was read."};
        offset += read;
    }
    if (!GetFileInformationByHandle(file.value, &after))
        return file_error("Cannot recheck file");
    if (before.nFileSizeHigh != after.nFileSizeHigh || before.nFileSizeLow != after.nFileSizeLow ||
        CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime) != 0)
        return FileError{"The file changed while it was being read. Try Refresh again."};
    const auto digest = hash_bytes(content.bytes);
    if (!digest)
        return FileError{"Windows could not calculate the file's SHA-256 fingerprint."};
    content.sha256 = *digest;
    return content;
}

std::optional<std::string> sha256_file(const std::filesystem::path &path) {
    const auto result = read_file(path);
    if (const auto *file = std::get_if<FileContent>(&result))
        return file->sha256;
    return std::nullopt;
}

} // namespace ern
