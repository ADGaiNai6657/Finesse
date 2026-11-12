#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using CsvRows = std::vector<std::vector<std::string>>;

//完整读取一个二进制文件
//输入：path：待读取的文件路径。
//输出：返回拥有全部文件字节的 string；打开、长度、读取或读取期间文件变化异常时抛异常。
auto readFile(const std::filesystem::path &path) -> std::string;
//将字节完整写入指定文件
//输入：path：目标文件路径；bytes：待写字节串。
//输出：无返回值；创建或截断目标文件并写入全部字节，I/O 失败抛异常。
auto writeFile(const std::filesystem::path &path, std::string_view bytes) -> void;
//计算字节串的 SHA-256 完整性摘要
//输入：bytes：完整只读字节串，可含零字节。
//输出：返回 64 个小写十六进制字符；OpenSSL 失败时抛异常，不承担精确去重。
auto sha256(std::string_view bytes) -> std::string;
//把 UTF-8 CSV 文本解析成行和字段
//输入：bytes：CSV 原始字节，允许带引号字段、转义引号和标准换行。
//输出：返回二维字符串数组，不自行解释表头；UTF-8 或 CSV 引号语法错误时抛异常。
auto parseCsv(std::string_view bytes) -> CsvRows;
//把行和字段编码成可重读的 CSV 文本
//输入：rows：二维字符串数组，行序和字段序由调用者决定。
//输出：返回按需加引号、转义双引号并以换行结束各行的文本，不修改原数据。
auto encodeCsv(const CsvRows &rows) -> std::string;
//严格解析十进制无符号整数
//输入：text：待解析的完整数字串。
//输出：返回 uint64 值；空串、非数字、尾随字符或越界时抛异常。
auto parseUnsigned(std::string_view text) -> std::uint64_t;
//检查溢出后执行无符号 64 位加法
//输入：left/right：两个 uint64 值。
//输出：返回和；溢出时抛异常，不允许回绕。
auto checkedAdd(std::uint64_t left, std::uint64_t right) -> std::uint64_t;
//检查溢出后执行无符号 64 位乘法
//输入：left/right：两个 uint64 值。
//输出：返回积；溢出时抛异常，不允许回绕。
auto checkedMultiply(std::uint64_t left, std::uint64_t right) -> std::uint64_t;
//按给定目录解析文本路径并进行规范化
//输入：directory：相对路径基准；text：路径文本，绝对路径直接使用。
//输出：返回 weakly_canonical 规范化后的路径；已有前缀解析别名，不要求末端存在；空文本或路径解析失败时抛异常。
auto pathFrom(const std::filesystem::path &directory, std::string_view text) -> std::filesystem::path;
//检查文本是否是有效且不含 NUL 的 UTF-8
//输入：bytes：待检查的完整文本字节。
//输出：无返回值；编码非法或含零字节时抛异常，不修改文本。
auto requireUtf8(std::string_view bytes) -> void;
