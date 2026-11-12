#include "IO.h"

#include <openssl/evp.h>

#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>

//完整读取一个二进制文件
//输入：path：待读取的文件路径。
//输出：返回拥有全部文件字节的 string；打开、长度、读取或读取期间文件变化异常时抛异常。
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

//将字节完整写入指定文件
//输入：path：目标文件路径；bytes：待写字节串。
//输出：无返回值；创建或截断目标文件并写入全部字节，I/O 失败抛异常。
auto writeFile(const std::filesystem::path &path, const std::string_view bytes) -> void {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.exceptions(std::ios::failbit | std::ios::badbit);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
}

//计算字节串的 SHA-256 完整性摘要
//输入：bytes：完整只读字节串，可含零字节。
//输出：返回 64 个小写十六进制字符；OpenSSL 失败时抛异常，不承担精确去重。
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

//检查文本是否是有效且不含 NUL 的 UTF-8
//输入：bytes：待检查的完整文本字节。
//输出：无返回值；编码非法或含零字节时抛异常，不修改文本。
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

//把 UTF-8 CSV 文本解析成行和字段
//输入：bytes：CSV 原始字节，允许带引号字段、转义引号和标准换行。
//输出：返回二维字符串数组，不自行解释表头；UTF-8 或 CSV 引号语法错误时抛异常。
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

//把行和字段编码成可重读的 CSV 文本
//输入：rows：二维字符串数组，行序和字段序由调用者决定。
//输出：返回按需加引号、转义双引号并以换行结束各行的文本，不修改原数据。
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

//严格解析十进制无符号整数
//输入：text：待解析的完整数字串。
//输出：返回 uint64 值；空串、非数字、尾随字符或越界时抛异常。
auto parseUnsigned(const std::string_view text) -> std::uint64_t {
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), result);
    if (text.empty() || error != std::errc {} || end != text.data() + text.size()) {
        throw std::invalid_argument("invalid unsigned decimal integer: " + std::string(text));
    }
    return result;
}

//检查溢出后执行无符号 64 位加法
//输入：left/right：两个 uint64 值。
//输出：返回和；溢出时抛异常，不允许回绕。
auto checkedAdd(const std::uint64_t left, const std::uint64_t right) -> std::uint64_t {
    if (right > std::numeric_limits<std::uint64_t>::max() - left) {
        throw std::overflow_error("byte/offset addition overflow");
    }
    return left + right;
}

//检查溢出后执行无符号 64 位乘法
//输入：left/right：两个 uint64 值。
//输出：返回积；溢出时抛异常，不允许回绕。
auto checkedMultiply(const std::uint64_t left, const std::uint64_t right) -> std::uint64_t {
    if (left != 0 && right > std::numeric_limits<std::uint64_t>::max() / left) {
        throw std::overflow_error("byte/count multiplication overflow");
    }
    return left * right;
}

//按给定目录解析文本路径并进行规范化
//输入：directory：相对路径基准；text：路径文本，绝对路径直接使用。
//输出：返回 weakly_canonical 规范化后的路径；已有前缀解析别名，不要求末端存在；空文本或路径解析失败时抛异常。
auto pathFrom(const std::filesystem::path &directory, const std::string_view text) -> std::filesystem::path {
    if (text.empty()) {
        throw std::invalid_argument("empty file path");
    }
    const std::filesystem::path path {std::string(text)};
    // macOS 的 /var 与 /private/var 等别名必须先解析，否则导出的相对路径会错误地跨越根目录。
    return std::filesystem::weakly_canonical(std::filesystem::absolute(path.is_absolute() ? path : directory / path));
}
