#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <vector>

namespace reworkbench::bytecore {

inline constexpr std::size_t read_chunk_size = 64 * 1024;
inline constexpr std::size_t string_preview_limit = 4096;

struct ScanOptions {
    std::uint64_t min_length = 4;
    std::size_t max_strings = 10000;
};

struct ExtractedString {
    std::uint64_t offset = 0;
    std::string value;
};

struct ScanResult {
    std::uint64_t size = 0;
    double entropy = 0.0;
    std::vector<ExtractedString> strings;
    bool strings_truncated = false;
};

class ScanCancelled final : public std::runtime_error {
public:
    ScanCancelled() : std::runtime_error("Byte scan cancelled.") {}
};

// Open the input only for reading. Strings are maximal printable-ASCII runs;
// qualification uses their complete length even when a preview is shortened.
// Offsets and size count bytes, independent of the host's text encoding.
// Memory is bounded by the read buffer and the caller's output limits.
[[nodiscard]] ScanResult scan_file(
    const std::filesystem::path& path,
    const ScanOptions& options = {},
    std::stop_token stop = {}
);

} // namespace reworkbench::bytecore
