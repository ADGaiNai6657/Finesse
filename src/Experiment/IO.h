#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using CsvRows = std::vector<std::vector<std::string>>;

auto readFile(const std::filesystem::path &path) -> std::string;
auto writeFile(const std::filesystem::path &path, std::string_view bytes) -> void;
auto sha256(std::string_view bytes) -> std::string;
auto parseCsv(std::string_view bytes) -> CsvRows;
auto encodeCsv(const CsvRows &rows) -> std::string;
auto parseUnsigned(std::string_view text) -> std::uint64_t;
auto checkedAdd(std::uint64_t left, std::uint64_t right) -> std::uint64_t;
auto checkedMultiply(std::uint64_t left, std::uint64_t right) -> std::uint64_t;
auto pathFrom(const std::filesystem::path &directory, std::string_view text) -> std::filesystem::path;
auto requireUtf8(std::string_view bytes) -> void;
