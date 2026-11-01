#include "IO.h"

#include <openssl/evp.h>

#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>

auto readFile(const std::filesystem::path &path) -> std::string {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) {
        throw std::runtime_error("cannot open file: " + path.string());
    }
    const auto length = input.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > std::numeric_limits<std::size_t>::max()
        || static_cast<std::uint64_t>(length) > static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max())) {
        throw std::runtime_error("invalid file size: " + path.string());
    }
    std::string bytes(static_cast<std::size_t>(length), '\0');
    input.seekg(0);
    if (!bytes.empty() && !input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()))) {
        throw std::runtime_error("short read: " + path.string());
    }
    if (input.peek() != std::char_traits<char>::eof() || input.bad()) {
        throw std::runtime_error("file changed or failed while reading: " + path.string());
    }
    return bytes;
}

auto writeFile(const std::filesystem::path &path, const std::string_view bytes) -> void {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
}

auto sha256(const std::string_view bytes) -> std::string {
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    unsigned char digest[EVP_MAX_MD_SIZE] {};
    unsigned int length = 0;
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1
        || EVP_DigestUpdate(context.get(), bytes.data(), bytes.size()) != 1
        || EVP_DigestFinal_ex(context.get(), digest, &length) != 1) {
        throw std::runtime_error("OpenSSL SHA-256 failed");
    }
    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (unsigned int i = 0; i < length; ++i) {
        text << std::setw(2) << static_cast<unsigned int>(digest[i]);
    }
    return text.str();
}

auto requireUtf8(const std::string_view bytes) -> void {
    for (std::size_t i = 0; i < bytes.size();) {
        const auto first = static_cast<unsigned char>(bytes[i++]);
        if (first == 0) {
            throw std::invalid_argument("NUL in UTF-8 text");
        }
        if (first < 0x80) {
            continue;
        }
        std::uint32_t value = 0;
        std::uint32_t minimum = 0;
        std::size_t continuation = 0;
        if (first >= 0xc2 && first <= 0xdf) {
            value = first & 0x1fU; minimum = 0x80; continuation = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            value = first & 0x0fU; minimum = 0x800; continuation = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            value = first & 0x07U; minimum = 0x10000; continuation = 3;
        } else {
            throw std::invalid_argument("invalid UTF-8 text");
        }
        for (std::size_t j = 0; j < continuation; ++j) {
            if (i >= bytes.size() || (static_cast<unsigned char>(bytes[i]) & 0xc0U) != 0x80U) {
                throw std::invalid_argument("truncated or invalid UTF-8 text");
            }
            value = (value << 6) | (static_cast<unsigned char>(bytes[i++]) & 0x3fU);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) {
            throw std::invalid_argument("invalid UTF-8 code point");
        }
    }
}

auto parseCsv(const std::string_view bytes) -> CsvRows {
    requireUtf8(bytes);
    CsvRows rows;
    std::vector<std::string> row;
    std::string field;
    bool quoted = false;
    bool closed = false;
    bool started = false;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        const char c = bytes[i];
        if (quoted) {
            if (c == '"') {
                if (i + 1 < bytes.size() && bytes[i + 1] == '"') {
                    field += '"'; ++i;
                } else {
                    quoted = false; closed = true;
                }
            } else {
                field += c;
            }
        } else if (c == ',' || c == '\n' || c == '\r') {
            row.push_back(field); field.clear(); closed = false; started = false;
            if (c != ',') {
                if (c == '\r' && i + 1 < bytes.size() && bytes[i + 1] == '\n') {
                    ++i;
                }
                rows.push_back(std::move(row)); row.clear();
            }
        } else if (closed) {
            throw std::invalid_argument("characters after CSV closing quote");
        } else if (c == '"') {
            if (started) {
                throw std::invalid_argument("quote inside unquoted CSV field");
            }
            quoted = true; started = true;
        } else {
            field += c; started = true;
        }
    }
    if (quoted) {
        throw std::invalid_argument("unterminated CSV quote");
    }
    if (started || closed || !field.empty() || !row.empty()) {
        row.push_back(field); rows.push_back(std::move(row));
    }
    return rows;
}

auto encodeCsv(const CsvRows &rows) -> std::string {
    std::string bytes;
    for (const auto &row : rows) {
        for (std::size_t i = 0; i < row.size(); ++i) {
            if (i != 0) {
                bytes += ',';
            }
            const auto &field = row[i];
            if (field.find_first_of(",\"\r\n") == std::string::npos) {
                bytes += field;
            } else {
                bytes += '"';
                for (const char c : field) {
                    bytes += c;
                    if (c == '"') {
                        bytes += c;
                    }
                }
                bytes += '"';
            }
        }
        bytes += '\n';
    }
    return bytes;
}

auto parseUnsigned(const std::string_view text) -> std::uint64_t {
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || error != std::errc {} || end != text.data() + text.size()) {
        throw std::invalid_argument("invalid unsigned decimal integer: " + std::string(text));
    }
    return result;
}

auto checkedAdd(const std::uint64_t left, const std::uint64_t right) -> std::uint64_t {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("byte/offset addition overflow");
    }
    return left + right;
}

auto checkedMultiply(const std::uint64_t left, const std::uint64_t right) -> std::uint64_t {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error("byte/count multiplication overflow");
    }
    return left * right;
}

auto pathFrom(const std::filesystem::path &directory, const std::string_view text) -> std::filesystem::path {
    if (text.empty()) {
        throw std::invalid_argument("empty file path");
    }
    const std::filesystem::path path {std::string(text)};
    // macOS 的 /var 与 /private/var 等别名必须先解析，否则导出的相对路径会错误地跨越根目录。
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path.is_absolute() ? path : directory / path));
}
