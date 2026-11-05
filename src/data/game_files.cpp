#include "data/game_files.hpp"
#include "game_keys.hpp"
#include "data/fingerprint.hpp"
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>
#include <zstd.h>
#include <algorithm>
#include <array>
#include <set>

namespace ern::game {
namespace {
struct Algorithm {
    BCRYPT_ALG_HANDLE value = nullptr;
    ~Algorithm() {
        if (value)
            BCryptCloseAlgorithmProvider(value, 0);
    }
};
struct Key {
    BCRYPT_KEY_HANDLE value = nullptr;
    ~Key() {
        if (value)
            BCryptDestroyKey(value);
    }
};

std::uint32_t big_number(View data, std::size_t at) {
    const auto b = slice(data, at, 4);
    return (std::uint32_t(b[0]) << 24) | (std::uint32_t(b[1]) << 16) | (std::uint32_t(b[2]) << 8) |
           b[3];
}

Bytes archive_header(View encrypted, std::string_view pem) {
    DWORD length = 0;
    require(CryptStringToBinaryA(pem.data(), static_cast<DWORD>(pem.size()),
                                 CRYPT_STRING_BASE64HEADER, nullptr, &length, nullptr,
                                 nullptr) != 0,
            "Cannot decode the archive public key.");
    Bytes der(length);
    require(CryptStringToBinaryA(pem.data(), static_cast<DWORD>(pem.size()),
                                 CRYPT_STRING_BASE64HEADER, der.data(), &length, nullptr,
                                 nullptr) != 0,
            "Cannot decode the archive public key.");
    DWORD blob_size = 0;
    require(CryptDecodeObjectEx(X509_ASN_ENCODING, RSA_CSP_PUBLICKEYBLOB, der.data(), length, 0,
                                nullptr, nullptr, &blob_size) != 0,
            "Cannot read the archive RSA key.");
    Bytes csp(blob_size);
    require(CryptDecodeObjectEx(X509_ASN_ENCODING, RSA_CSP_PUBLICKEYBLOB, der.data(), length, 0,
                                nullptr, csp.data(), &blob_size) != 0,
            "Cannot read the archive RSA key.");
    const auto rsa = number<RSAPUBKEY>(csp, sizeof(BLOBHEADER));
    require(rsa.bitlen == 2048 && rsa.pubexp != 0 && encrypted.size() % 256 == 0,
            "Unsupported archive RSA layout.");
    const auto modulus = slice(csp, sizeof(BLOBHEADER) + sizeof(RSAPUBKEY), 256);
    Bytes exponent;
    for (int shift = 24; shift >= 0; shift -= 8) {
        const auto b = static_cast<std::uint8_t>(rsa.pubexp >> shift);
        if (b || !exponent.empty())
            exponent.push_back(b);
    }
    BCRYPT_RSAKEY_BLOB header{
        BCRYPT_RSAPUBLIC_MAGIC, rsa.bitlen, static_cast<ULONG>(exponent.size()), 256, 0, 0};
    Bytes blob(sizeof(header));
    std::memcpy(blob.data(), &header, sizeof(header));
    blob.insert(blob.end(), exponent.begin(), exponent.end());
    blob.insert(blob.end(), modulus.rbegin(), modulus.rend());
    Algorithm algorithm;
    Key key;
    require(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_RSA_ALGORITHM, nullptr, 0) >= 0 &&
                BCryptImportKeyPair(algorithm.value, nullptr, BCRYPT_RSAPUBLIC_BLOB, &key.value,
                                    blob.data(), static_cast<ULONG>(blob.size()), 0) >= 0,
            "Cannot initialize archive RSA decoding.");
    Bytes result;
    result.reserve(encrypted.size() / 256 * 255);
    std::array<std::uint8_t, 256> block{};
    for (std::size_t at = 0; at < encrypted.size(); at += 256) {
        ULONG written = 0;
        require(BCryptEncrypt(key.value, const_cast<PUCHAR>(encrypted.data() + at), 256, nullptr,
                              nullptr, 0, block.data(), static_cast<ULONG>(block.size()), &written,
                              BCRYPT_PAD_NONE) >= 0 &&
                    written == 256 && block[0] == 0,
                "Archive RSA decoding failed.");
        result.insert(result.end(), block.begin() + 1, block.end());
    }
    require(starts(result, "BHD5"), "The archive index did not decode as BHD5.");
    return result;
}

Bytes archive_entry(const std::filesystem::path &path, std::uint64_t offset, std::uint32_t size) {
    require(size > 0 && size <= 128 * 1024 * 1024 && offset <= INT64_MAX,
            "Unsupported archive entry size or offset.");
    struct Handle {
        HANDLE value;
        ~Handle() {
            if (value != INVALID_HANDLE_VALUE)
                CloseHandle(value);
        }
    } file{CreateFileW(path.c_str(), GENERIC_READ,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
    require(file.value != INVALID_HANDLE_VALUE, "Cannot open a packed game archive.");
    LARGE_INTEGER position{}, length{};
    position.QuadPart = static_cast<LONGLONG>(offset);
    require(GetFileSizeEx(file.value, &length) && length.QuadPart >= 0 &&
                offset <= static_cast<std::uint64_t>(length.QuadPart) &&
                size <= static_cast<std::uint64_t>(length.QuadPart) - offset &&
                SetFilePointerEx(file.value, position, nullptr, FILE_BEGIN),
            "A packed asset lies outside its game archive.");
    Bytes bytes(size);
    DWORD read = 0;
    require(ReadFile(file.value, bytes.data(), size, &read, nullptr) && read == size,
            "Cannot read a packed game asset.");
    return bytes;
}

std::uint64_t path_hash(std::string_view path) {
    std::uint64_t hash = 0;
    for (const auto ch : path)
        hash = hash * 0x85 + static_cast<unsigned char>(ch == '\\' ? '/' : ch);
    return hash;
}
} // namespace

std::string utf8(std::wstring_view text) {
    if (text.empty())
        return {};
    const auto size =
        WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                            static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    require(size > 0, "Invalid UTF-16 game text.");
    std::string result(size, '\0');
    require(WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, text.data(),
                                static_cast<int>(text.size()), result.data(), size, nullptr,
                                nullptr) == size,
            "Cannot convert game text to UTF-8.");
    return result;
}

std::string string(View data, std::size_t offset, bool wide) {
    std::wstring text;
    std::string narrow;
    for (std::size_t i = 0; i < 65536; ++i) {
        const auto ch = wide ? number<std::uint16_t>(data, offset + i * 2)
                             : number<std::uint8_t>(data, offset + i);
        if (!ch)
            return wide ? utf8(text) : narrow;
        if (wide)
            text += static_cast<wchar_t>(ch);
        else
            narrow += static_cast<char>(ch);
    }
    throw std::runtime_error("Unterminated game string.");
}

Bytes decrypt_aes(View data, View key_bytes, View initial_iv) {
    require(data.size() <= ULONG_MAX && data.size() % 16 == 0 &&
                (key_bytes.size() == 16 || key_bytes.size() == 32) &&
                (initial_iv.empty() || initial_iv.size() == 16),
            "Unsupported AES data, key, or IV size.");
    Algorithm algorithm;
    Key key;
    auto *mode = initial_iv.empty() ? BCRYPT_CHAIN_MODE_ECB : BCRYPT_CHAIN_MODE_CBC;
    require(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_AES_ALGORITHM, nullptr, 0) >= 0 &&
                BCryptSetProperty(algorithm.value, BCRYPT_CHAINING_MODE,
                                  reinterpret_cast<PUCHAR>(const_cast<wchar_t *>(mode)),
                                  static_cast<ULONG>((wcslen(mode) + 1) * sizeof(wchar_t)),
                                  0) >= 0 &&
                BCryptGenerateSymmetricKey(algorithm.value, &key.value, nullptr, 0,
                                           const_cast<PUCHAR>(key_bytes.data()),
                                           static_cast<ULONG>(key_bytes.size()), 0) >= 0,
            "Windows could not initialize AES decoding.");
    Bytes iv(initial_iv.begin(), initial_iv.end()), output(data.size());
    ULONG written = 0;
    require(BCryptDecrypt(key.value, const_cast<PUCHAR>(data.data()),
                          static_cast<ULONG>(data.size()), nullptr,
                          iv.empty() ? nullptr : iv.data(), static_cast<ULONG>(iv.size()),
                          output.data(), static_cast<ULONG>(output.size()), &written, 0) >= 0 &&
                written == output.size(),
            "Game data AES decoding failed.");
    return output;
}

Bytes decompress(View data, const std::filesystem::path &directory) {
    if (!starts(data, std::string_view("DCX\0", 4)))
        return {data.begin(), data.end()};
    const auto size = big_number(data, 0x1C), packed = big_number(data, 0x20);
    require(size > 0 && size <= 512 * 1024 * 1024, "Unsupported decompressed game-file size.");
    const auto header = slice(data, 0, std::min<std::size_t>(data.size(), 256));
    const auto text =
        std::string_view(reinterpret_cast<const char *>(header.data()), header.size());
    const auto dca = text.find(std::string_view("DCA\0", 4), 0x2C);
    require(dca != std::string_view::npos && big_number(data, dca + 4) == 8,
            "Invalid DCX payload header.");
    const auto payload = slice(data, dca + 8, packed), codec = slice(data, 0x28, 4);
    Bytes output(size);
    if (starts(codec, "ZSTD")) {
        const auto count =
            ZSTD_decompress(output.data(), output.size(), payload.data(), payload.size());
        require(!ZSTD_isError(count) && count == output.size(),
                "Zstandard game-file decoding failed.");
    } else if (starts(codec, "KRAK")) {
        struct Library {
            HMODULE value;
            ~Library() {
                if (value)
                    FreeLibrary(value);
            }
        } library{LoadLibraryExW((directory / "oo2core_9_win64.dll").c_str(), nullptr,
                                 LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32)};
        require(library.value != nullptr, "Cannot load the installed game's Kraken decoder.");
        using Decode =
            std::intptr_t (*)(const void *, std::intptr_t, void *, std::intptr_t, int, int, int,
                              void *, std::intptr_t, void *, void *, void *, std::intptr_t, int);
        const auto decode =
            reinterpret_cast<Decode>(GetProcAddress(library.value, "OodleLZ_Decompress"));
        require(decode && decode(payload.data(), payload.size(), output.data(), output.size(), 1, 0,
                                 0, nullptr, 0, nullptr, nullptr, nullptr, 0,
                                 3) == static_cast<std::intptr_t>(output.size()),
                "Kraken game-file decoding failed.");
    } else
        throw std::runtime_error("Unsupported DCX codec. Expected ZSTD or KRAK.");
    return output;
}

Files binder(View input, const std::filesystem::path &directory,
             const std::function<bool(std::string_view)> &select) {
    const auto archive = decompress(input, directory);
    const View data = archive;
    slice(data, 0, 64);
    require(starts(data, "BND4") && data[9] == 0, "Unsupported BND4 layout.");
    const auto count = number<std::uint32_t>(data, 12);
    const auto stride = number<std::uint64_t>(data, 32);
    require(stride >= 24 && stride <= 64 && count <= (data.size() - 64) / stride,
            "Invalid binder entry table.");
    auto format = data[49];
    if (data[10] && !(format & 1 && !(format & 128))) {
        std::uint8_t reversed = 0;
        for (unsigned int bit = 0; bit < 8; ++bit)
            reversed |= ((format >> bit) & 1) << (7 - bit);
        format = reversed;
    }
    require(format & 12, "The binder has no named entries.");
    Files result;
    for (std::size_t index = 0; index < count; ++index) {
        const auto entry = slice(data, 64 + index * stride, stride);
        const auto size = number<std::uint64_t>(entry, 8);
        std::size_t field = 16 + (format & 32 ? 8 : 0);
        const auto at =
            format & 16 ? number<std::uint64_t>(entry, field) : number<std::uint32_t>(entry, field);
        field += (format & 16 ? 8 : 4) + (format & 2 ? 4 : 0);
        auto name = string(data, number<std::uint32_t>(entry, field), data[48] != 0);
        name = name.substr(name.find_last_of("/\\") + 1);
        if (select(name))
            require(result.emplace(name, decompress(slice(data, at, size), directory)).second,
                    "Duplicate selected binder entry.");
    }
    return result;
}

GameFiles read_game_files(const std::filesystem::path &directory,
                          std::span<const std::string> paths) {
    GameFiles result;
    std::unordered_map<std::uint64_t, std::string> pending;
    for (const auto &path : paths) {
        const auto read = read_file(directory / path.substr(1));
        if (const auto *file = std::get_if<FileContent>(&read))
            result.files.emplace(path, file->bytes);
        else
            pending.emplace(path_hash(path), path);
    }
    for (const auto &archive : archive_keys) {
        if (pending.empty())
            break;
        const auto read = read_file(directory / (std::string(archive.name) + ".bhd"));
        const auto *file = std::get_if<FileContent>(&read);
        if (!file)
            continue;
        try {
            const auto header = archive_header(file->bytes, archive.pem);
            const auto buckets = number<std::uint32_t>(header, 16),
                       at = number<std::uint32_t>(header, 20);
            require(at != 0, "Unsupported 64-bit archive index.");
            slice(header, at, std::size_t(buckets) * 8);
            for (std::size_t bucket = 0; bucket < buckets; ++bucket) {
                const auto count = number<std::uint32_t>(header, at + bucket * 8),
                           first = number<std::uint32_t>(header, at + bucket * 8 + 4);
                slice(header, first, std::size_t(count) * 40);
                for (std::size_t index = 0; index < count; ++index) {
                    const auto entry = slice(header, first + index * 40, 40);
                    const auto found = pending.find(number<std::uint64_t>(entry, 0));
                    if (found == pending.end())
                        continue;
                    const auto size = number<std::int32_t>(entry, 8),
                               unpadded = number<std::int32_t>(entry, 12);
                    const auto start = number<std::int64_t>(entry, 16),
                               aes = number<std::int64_t>(entry, 32);
                    require(size > 0 && start >= 0 && aes >= 0 && unpadded >= 0 && unpadded <= size,
                            "Invalid game archive entry.");
                    auto bytes = archive_entry(directory / (std::string(archive.name) + ".bdt"),
                                               start, size);
                    if (aes) {
                        const auto key = slice(header, aes, 16);
                        const auto ranges = number<std::int32_t>(header, aes + 16);
                        require(ranges >= 0 && ranges <= 65536, "Invalid archive AES range count.");
                        for (int range = 0; range < ranges; ++range) {
                            const auto left = number<std::int64_t>(header, aes + 20 + range * 16),
                                       right = number<std::int64_t>(header, aes + 28 + range * 16);
                            if (left == -1 || right == -1)
                                continue;
                            require(left >= 0 && right >= left, "Invalid archive AES range.");
                            const auto decoded = decrypt_aes(slice(bytes, left, right - left), key);
                            std::copy(decoded.begin(), decoded.end(), bytes.begin() + left);
                        }
                    }
                    if (unpadded)
                        bytes.resize(unpadded);
                    result.files.emplace(found->second, std::move(bytes));
                    pending.erase(found);
                }
            }
        } catch (const std::exception &error) {
            result.warnings.push_back(std::string(archive.name) + ": " + error.what());
        }
    }
    for (const auto &[hash, path] : pending)
        result.warnings.push_back("Asset unavailable: " + path);
    return result;
}
} // namespace ern::game
